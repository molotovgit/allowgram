/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "core/update_checker.h"

#include "platform/platform_specific.h"
#include "base/platform/base_platform_info.h"
#include "base/platform/base_platform_file_utilities.h"
#include "base/timer.h"
#include "base/bytes.h"
#include "base/flat_set.h"
#include "base/unixtime.h"
#include "storage/localstorage.h"
#include "core/application.h"
#include "calls/calls_instance.h"
#include "core/changelogs.h"
#include "core/click_handler_types.h"
#include "core/update_channel.h"
#include "core/update_feed.h"
#include "core/update_keys.h"
#include "core/update_mandatory.h"
#include "core/update_stage.h"
#include "core/update_verify.h"
#include "core/version.h"
#include "data/data_channel.h"
#include "data/data_session.h"
#include "mainwindow.h"
#include "main/main_account.h"
#include "main/main_session.h"
#include "main/main_domain.h"
#include "info/info_memento.h"
#include "info/info_controller.h"
#include "window/window_controller.h"
#include "window/window_session_controller.h"
#include "settings/sections/settings_advanced.h"
#include "settings/settings_intro.h"
#include "ui/layers/box_content.h"
#include "ui/boxes/confirm_box.h"

#include <QtCore/QJsonDocument>
#include <QtCore/QJsonObject>
#include <QtCore/QCryptographicHash>
#include <QtCore/QSet>
#include <QtCore/QFileSystemWatcher>

#include <ksandbox.h>

#if !defined Q_OS_WIN && !defined Q_OS_MAC
#include "base/platform/linux/base_linux_xdp_utilities.h"

#include <flatpakportal/flatpakportal.hpp>
#endif // !Q_OS_WIN && !Q_OS_MAC

extern "C" {
#include <openssl/rsa.h>
#include <openssl/pem.h>
#include <openssl/bio.h>
#include <openssl/err.h>
} // extern "C"

#ifndef TDESKTOP_DISABLE_AUTOUPDATE
#if defined Q_OS_WIN && !defined TDESKTOP_USE_PACKAGED // use Lzma SDK for win
#include <LzmaLib.h>
#else // Q_OS_WIN && !TDESKTOP_USE_PACKAGED
#include <lzma.h>
#endif // else of Q_OS_WIN && !TDESKTOP_USE_PACKAGED
#endif // !TDESKTOP_DISABLE_AUTOUPDATE

#ifndef Q_OS_WIN
#include <unistd.h>
#endif // !Q_OS_WIN

namespace Core {
namespace {

constexpr auto kUpdaterTimeout = 10 * crl::time(1000);
constexpr auto kMaxResponseSize = 1024 * 1024;

// tdata/version marker for verified v2 packages, followed by the full
// 64-bit (base << 32 | sequence) Allowgram update version.
constexpr auto kVersionFilePackedMarker = quint32(0x7FFFFFFE);

#if !defined Q_OS_WIN && !defined Q_OS_MAC
constexpr auto kFlatpakPortalService = "org.freedesktop.portal.Flatpak";
constexpr auto kFlatpakPortalObjectPath = "/org/freedesktop/portal/Flatpak";
constexpr auto kFlatpakUpdated = "/app/.updated"_cs;
#endif // !Q_OS_WIN && !Q_OS_MAC

#ifdef TDESKTOP_DISABLE_AUTOUPDATE
bool UpdaterIsDisabled = true;
#else // TDESKTOP_DISABLE_AUTOUPDATE
bool UpdaterIsDisabled = false;
#endif // TDESKTOP_DISABLE_AUTOUPDATE

std::weak_ptr<Updater> UpdaterInstance;
base::weak_qptr<Ui::GenericBox> MandatoryUpdateLockBox;
bool MandatoryUpdateLockShown = false;
QByteArray CheckedReadyUpdateStageHash;

using Progress = UpdateChecker::Progress;
using State = UpdateChecker::State;

#ifdef Q_OS_WIN
using VersionInt = DWORD;
using VersionChar = WCHAR;
#else // Q_OS_WIN
using VersionInt = int;
using VersionChar = wchar_t;
#endif // Q_OS_WIN

using Loader = MTP::AbstractDedicatedLoader;

#if !defined Q_OS_WIN && !defined Q_OS_MAC
using namespace gi::repository;
namespace GObject = gi::repository::GObject;
#endif // !Q_OS_WIN && !Q_OS_MAC

struct BIODeleter {
	void operator()(BIO *value) {
		BIO_free(value);
	}
};

inline auto MakeBIO(const void *buf, int len) {
	return std::unique_ptr<BIO, BIODeleter>{
		BIO_new_mem_buf(buf, len),
	};
}

class Checker : public base::has_weak_ptr {
public:
	Checker(bool testing);

	virtual void start() = 0;

	virtual bool poll() const;

	rpl::producer<std::shared_ptr<Loader>> ready() const;
	rpl::producer<Updates::StableReleaseAsset> mandatoryRelease() const;
	rpl::producer<> failed() const;

	rpl::lifetime &lifetime();

	virtual ~Checker() = default;

protected:
	bool testing() const;
	void done(std::shared_ptr<Loader> result);
	void notifyMandatoryRelease(Updates::StableReleaseAsset asset);
	void fail();

private:
	bool _testing = false;
	rpl::event_stream<std::shared_ptr<Loader>> _ready;
	rpl::event_stream<Updates::StableReleaseAsset> _mandatoryRelease;
	rpl::event_stream<> _failed;

	rpl::lifetime _lifetime;

};

struct Implementation {
	std::unique_ptr<Checker> checker;
	std::shared_ptr<Loader> loader;
	bool failed = false;

};

class HttpChecker : public Checker {
public:
	HttpChecker(bool testing);

	void start() override;

	~HttpChecker();

private:
	void gotResponse();
	void gotFailure(QNetworkReply::NetworkError e);
	void clearSentRequest();
	bool handleResponse(const QByteArray &response);

	std::unique_ptr<QNetworkAccessManager> _manager;
	QNetworkReply *_reply = nullptr;

};

class HttpLoaderActor;

class HttpLoader : public Loader {
public:
	HttpLoader(
		const QString &url,
		quint64 expectedSize,
		QByteArray expectedSha256);

	~HttpLoader();

private:
	void startLoading() override;
	bool validateChunk(const QByteArray &data, int64 totalSize) const;
	bool validateAlreadyComplete() const;

	friend class HttpLoaderActor;

	QString _url;
	QString _filePath;
	quint64 _expectedSize = 0;
	QByteArray _expectedSha256;
	std::unique_ptr<QThread> _thread;
	HttpLoaderActor *_actor = nullptr;

};

class HttpLoaderActor : public QObject {
public:
	HttpLoaderActor(
		not_null<HttpLoader*> parent,
		not_null<QThread*> thread,
		const QString &url);

private:
	void start();
	void sendRequest();

	void gotMetaData();
	void partFinished(qint64 got, qint64 total);
	void partFailed(QNetworkReply::NetworkError e);

	not_null<HttpLoader*> _parent;
	QString _url;
	QNetworkAccessManager _manager;
	std::unique_ptr<QNetworkReply> _reply;

};

class MtpChecker : public Checker {
public:
	MtpChecker(base::weak_ptr<Main::Session> session, bool testing);

	void start() override;

private:
	using FileLocation = MTP::DedicatedLoader::Location;

	using Checker::fail;
	Fn<void(const MTP::Error &error)> failHandler();

	void gotMessage(const MTPmessages_Messages &result);
	std::optional<FileLocation> parseMessage(
		const MTPmessages_Messages &result) const;
	std::optional<FileLocation> parseText(const QByteArray &text) const;
	FileLocation validateLatestLocation(
		uint64 availableVersion,
		const FileLocation &location) const;

	void startCanary();
	void requestCanaryMetadata(
		const MTPInputChannel &channel,
		int messageId,
		bool fallbackToPinned);
	void requestCanaryPinnedFallback(const MTPInputChannel &channel);
	void gotCanaryMessage(
		const MTPInputChannel &channel,
		const MTPmessages_Messages &result,
		int messageId,
		bool fallbackToPinned);
	void parseCanaryMetadata(
		const MTPInputChannel &channel,
		const QByteArray &text);

	MTP::WeakInstance _mtp;

};

#if !defined Q_OS_WIN && !defined Q_OS_MAC
class FlatpakChecker : public Checker {
public:
	FlatpakChecker(bool testing);

	void start() override;

	bool poll() const override;

	~FlatpakChecker();

private:
	FlatpakPortal::Flatpak _interface;
	FlatpakPortal::FlatpakUpdateMonitor _monitor;
	QFileSystemWatcher _watcher;
	ulong _updateAvailableSignal = 0;

};

class FlatpakLoader : public Loader {
public:
	FlatpakLoader(FlatpakPortal::FlatpakUpdateMonitor monitor);

	~FlatpakLoader();

private:
	void startLoading() override;

	FlatpakPortal::FlatpakUpdateMonitor _monitor;
	ulong _progressSignal = 0;

};
#endif // !Q_OS_WIN && !Q_OS_MAC

std::shared_ptr<Updater> GetUpdaterInstance() {
	if (const auto result = UpdaterInstance.lock()) {
		return result;
	}
	const auto result = std::make_shared<Updater>();
	UpdaterInstance = result;
	return result;
}

[[nodiscard]] base::weak_ptr<Main::Session> LookupCanaryPrivateSession(
		base::weak_ptr<Main::Session> fallback) {
	if (BuildUpdateChannel != Updates::Channel::CanaryPrivate
		|| !CanaryPrivateChannelId
		|| !IsAppLaunched()
		|| !App().domain().started()) {
		return fallback;
	}
	for (const auto &[index, account] : App().domain().accounts()) {
		if (const auto session = account->maybeSession()) {
			const auto channel = session->data().channelLoaded(
				ChannelId(BareId(CanaryPrivateChannelId)));
			if (channel && channel->amIn()) {
				return base::make_weak(session);
			}
		}
	}
	return fallback;
}

QString UpdatesFolder() {
	return cWorkingDir() + u"tupdates"_q;
}

void ClearAll() {
	base::Platform::DeleteDirectory(UpdatesFolder());
}

QString FindUpdateFile() {
	QDir updates(UpdatesFolder());
	if (!updates.exists()) {
		return QString();
	}
	const auto list = updates.entryInfoList(QDir::Files);
	for (const auto &info : list) {
		static const auto RegExp = QRegularExpression(
			"^allowgram-update-stable-(win|mac|linux)-(x86|x64|arm)-"
			"\\d+\\.\\d+\\.\\d+\\.\\d+\\.tdup$"
		);
		if (RegExp.match(info.fileName()).hasMatch()) {
			return info.absoluteFilePath();
		}
	}
	return QString();
}

[[nodiscard]] std::optional<Updates::Manifest> HeldManifest() {
	auto error = QString();
	auto result = Updates::ParseVerifiedManifest(
		Updates::EmbeddedManifest(),
		Updates::EmbeddedManifestSignature(),
		Updates::RootPublicKeyPem(),
		&error);
	if (!result) {
		LOG(("Update Error: Bad embedded manifest: %1").arg(error));
	}
	auto manifest = QByteArray();
	auto signature = QByteArray();
	if (Local::readUpdateManifest(&manifest, &signature)) {
		auto persisted = Updates::ParseVerifiedManifest(
			manifest,
			signature,
			Updates::RootPublicKeyPem());
		if (persisted && (!result || persisted->version > result->version)) {
			result = std::move(persisted);
		}
	}
	return result;
}

void AdoptManifest(const Updates::Manifest &manifest) {
	const auto held = HeldManifest();
	if (!held || manifest.version > held->version) {
		LOG(("Update Info: Adopting manifest version %1."
			).arg(manifest.version));
		Local::writeUpdateManifest(manifest.bytes, manifest.signature);
	}
}

QString ExtractFilename(const QString &url) {
	const auto expression = QRegularExpression(u"/([^/\\?]+)(\\?|$)"_q);
	if (const auto match = expression.match(url); match.hasMatch()) {
		return match.captured(1).replace(
			QRegularExpression(u"[^a-zA-Z0-9_.\\-]"_q),
			QString());
	}
	return QString();
}

#ifndef TDESKTOP_DISABLE_AUTOUPDATE

// The data must point to the exact v1 post-signature layout, which is also
// the v2 payload layout: [lzma props on Windows,] original size, compressed
// bytes. Callers only pass authenticated bytes here.
[[nodiscard]] std::optional<QByteArray> DecompressUpdatePayload(
		const char *data,
		int32 size) {
#if defined Q_OS_WIN && !defined TDESKTOP_USE_PACKAGED // use Lzma SDK for win
	const int32 hPropsLen = LZMA_PROPS_SIZE;
#else // Q_OS_WIN && !TDESKTOP_USE_PACKAGED
	const int32 hPropsLen = 0;
#endif // Q_OS_WIN && !TDESKTOP_USE_PACKAGED
	const int32 hOriginalSizeLen = sizeof(int32);
	const int32 hSize = hPropsLen + hOriginalSizeLen;
	const int32 compressedLen = size - hSize;
	if (compressedLen <= 0) {
		LOG(("Update Error: bad compressed size: %1").arg(size));
		return std::nullopt;
	}

	QByteArray uncompressed;

	int32 uncompressedLen;
	memcpy(&uncompressedLen, data + hPropsLen, hOriginalSizeLen);
	if (uncompressedLen <= 0 || uncompressedLen > 1024 * 1024 * 1024) {
		LOG(("Update Error: bad uncompressed size: %1").arg(uncompressedLen));
		return std::nullopt;
	}
	uncompressed.resize(uncompressedLen);

	size_t resultLen = uncompressed.size();
#if defined Q_OS_WIN && !defined TDESKTOP_USE_PACKAGED // use Lzma SDK for win
	SizeT srcLen = compressedLen;
	int uncompressRes = LzmaUncompress((uchar*)uncompressed.data(), &resultLen, (const uchar*)(data + hSize), &srcLen, (const uchar*)data, LZMA_PROPS_SIZE);
	if (uncompressRes != SZ_OK) {
		LOG(("Update Error: could not uncompress lzma, code: %1").arg(uncompressRes));
		return std::nullopt;
	}
#else // Q_OS_WIN && !TDESKTOP_USE_PACKAGED
	lzma_stream stream = LZMA_STREAM_INIT;

	lzma_ret ret = lzma_stream_decoder(&stream, UINT64_MAX, LZMA_CONCATENATED);
	if (ret != LZMA_OK) {
		const char *msg;
		switch (ret) {
		case LZMA_MEM_ERROR: msg = "Memory allocation failed"; break;
		case LZMA_OPTIONS_ERROR: msg = "Specified preset is not supported"; break;
		case LZMA_UNSUPPORTED_CHECK: msg = "Specified integrity check is not supported"; break;
		default: msg = "Unknown error, possibly a bug"; break;
		}
		LOG(("Error initializing the decoder: %1 (error code %2)").arg(msg).arg(ret));
		return std::nullopt;
	}

	stream.avail_in = compressedLen;
	stream.next_in = (uint8_t*)(data + hSize);
	stream.avail_out = resultLen;
	stream.next_out = (uint8_t*)uncompressed.data();

	lzma_ret res = lzma_code(&stream, LZMA_FINISH);
	if (stream.avail_in) {
		LOG(("Error in decompression, %1 bytes left in _in of %2 whole.").arg(stream.avail_in).arg(compressedLen));
		return std::nullopt;
	} else if (stream.avail_out) {
		LOG(("Error in decompression, %1 bytes free left in _out of %2 whole.").arg(stream.avail_out).arg(resultLen));
		return std::nullopt;
	}
	lzma_end(&stream);
	if (res != LZMA_OK && res != LZMA_STREAM_END) {
		const char *msg;
		switch (res) {
		case LZMA_MEM_ERROR: msg = "Memory allocation failed"; break;
		case LZMA_FORMAT_ERROR: msg = "The input data is not in the .xz format"; break;
		case LZMA_OPTIONS_ERROR: msg = "Unsupported compression options"; break;
		case LZMA_DATA_ERROR: msg = "Compressed file is corrupt"; break;
		case LZMA_BUF_ERROR: msg = "Compressed data is truncated or otherwise corrupt"; break;
		default: msg = "Unknown error, possibly a bug"; break;
		}
		LOG(("Error in decompression: %1 (error code %2)").arg(msg).arg(res));
		return std::nullopt;
	}
#endif // Q_OS_WIN && !TDESKTOP_USE_PACKAGED

	return uncompressed;
}

constexpr auto kMaxUpdateFilesCount = quint32(32);

[[nodiscard]] bool ExtractUpdateFiles(
		QDataStream &stream,
		quint32 filesCount,
		const QString &tempDirPath,
		not_null<std::vector<Updates::StagedUpdateFile>*> stagedFiles) {
	if (!filesCount || filesCount > kMaxUpdateFilesCount) {
		LOG(("Update Error: bad update files count: %1").arg(filesCount));
		return false;
	}
	auto seen = base::flat_set<QString>();
	for (uint32 i = 0; i < filesCount; ++i) {
		QString relativeName;
		quint32 fileSize;
		QByteArray fileInnerData;
		bool executable = false;

		stream >> relativeName >> fileSize >> fileInnerData;
#ifndef Q_OS_WIN
		stream >> executable;
#endif // !Q_OS_WIN
		if (stream.status() != QDataStream::Ok) {
			LOG(("Update Error: cant read file from downloaded stream, status: %1").arg(stream.status()));
			return false;
		}
		const auto normalized = Updates::NormalizeUpdatePayloadPath(relativeName);
		if (!normalized || !Updates::UpdatePayloadFileAllowed(*normalized)) {
			LOG(("Update Error: update file path is not allowed: '%1'"
				).arg(relativeName));
			return false;
		}
		const auto collisionKey = normalized->toCaseFolded();
		if (seen.contains(collisionKey)) {
			LOG(("Update Error: duplicate update file path: '%1'"
				).arg(*normalized));
			return false;
		}
		seen.insert(collisionKey);
		if (fileSize != quint32(fileInnerData.size())) {
			LOG(("Update Error: bad file size %1 not matching data size %2").arg(fileSize).arg(fileInnerData.size()));
			return false;
		}

		if (!tempDirPath.isEmpty()) {
			QFile f(tempDirPath + '/' + *normalized);
			if (!QDir().mkpath(QFileInfo(f).absolutePath())) {
				LOG(("Update Error: cant mkpath for file '%1'").arg(tempDirPath + '/' + *normalized));
				return false;
			}
			if (!f.open(QIODevice::WriteOnly)) {
				LOG(("Update Error: cant open file '%1' for writing").arg(tempDirPath + '/' + *normalized));
				return false;
			}
			auto writtenBytes = f.write(fileInnerData);
			if (writtenBytes != fileSize) {
				f.close();
				LOG(("Update Error: cant write file '%1', desiredSize: %2, write result: %3").arg(tempDirPath + '/' + *normalized).arg(fileSize).arg(writtenBytes));
				return false;
			}
			f.close();
			if (executable) {
				QFileDevice::Permissions p = f.permissions();
				p |= QFileDevice::ExeOwner | QFileDevice::ExeUser | QFileDevice::ExeGroup | QFileDevice::ExeOther;
				f.setPermissions(p);
			}
		}
		stagedFiles->push_back({
			*normalized,
			quint64(fileInnerData.size()),
			Updates::Sha256Bytes(fileInnerData),
		});
	}
	return true;
}
[[nodiscard]] bool WriteUpdateVersionFile(
		QDir &tempDir,
		const QString &tempDirPath,
		quint64 packedVersion) {
	tempDir.mkdir(QDir(tempDirPath + u"/tdata"_q).absolutePath());
	const auto versionNum = VersionInt(kVersionFilePackedMarker);

	QFile fVersion(tempDirPath + u"/tdata/version"_q);
	if (!fVersion.open(QIODevice::WriteOnly)) {
		LOG(("Update Error: cant write version file '%1'"
			).arg(tempDirPath + u"/version"_q));
		return false;
	}
	fVersion.write((const char*)&versionNum, sizeof(VersionInt));
	fVersion.write((const char*)&packedVersion, sizeof(quint64));
	fVersion.close();
	return true;
}

[[nodiscard]] bool WriteUpdateReadyFile(const QString &readyFilePath) {
	QFile readyFile(readyFilePath);
	if (readyFile.open(QIODevice::WriteOnly)) {
		if (readyFile.write("1", 1)) {
			readyFile.close();
		} else {
			LOG(("Update Error: cant write ready file '%1'").arg(readyFilePath));
			return false;
		}
	} else {
		LOG(("Update Error: cant create ready file '%1'").arg(readyFilePath));
		return false;
	}
	return true;
}

void SetUpdateError(QString *error, const QString &text) {
	if (error) {
		*error = text;
	}
}

void PrepareMandatoryUpdateApply() {
	if (!IsAppLaunched()) {
		return;
	}
	App().materializeLocalDrafts();
	App().calls().discardCurrentForUpdate();
}

[[nodiscard]] bool WriteSignedUpdatePackage(
		const QString &tempDirPath,
		const QByteArray &content) {
	const auto path = Updates::StagePackagePath(tempDirPath);
	if (!QDir().mkpath(QFileInfo(path).absolutePath())) {
		LOG(("Update Error: cant create signed package path."));
		return false;
	}
	auto file = QFile(path);
	if (!file.open(QIODevice::WriteOnly)
		|| file.write(content) != content.size()) {
		LOG(("Update Error: cant write signed update package."));
		return false;
	}
	return true;
}

[[nodiscard]] std::optional<Updates::StagedUpdateManifest>
BuildStageManifestFromVerifiedPackage(
		const Updates::VerifiedUpdate &verified,
		const QByteArray &content,
		const QString &tempDirPath,
		QString *error) {
	const auto &payload = verified.envelope.payload;
	const auto uncompressed = DecompressUpdatePayload(
		payload.constData(),
		payload.size());
	if (!uncompressed) {
		SetUpdateError(error, QStringLiteral("Could not decompress signed package."));
		return std::nullopt;
	}
	auto stagedFiles = std::vector<Updates::StagedUpdateFile>();
	{
		QDataStream stream(*uncompressed);
		stream.setVersion(QDataStream::Qt_5_1);

		quint32 version = 0;
		stream >> version;
		if (stream.status() != QDataStream::Ok
			|| version != Updates::UpdateVersionBase(
				verified.envelope.version)) {
			SetUpdateError(error, QStringLiteral(
				"v2 inner version does not match envelope."));
			return std::nullopt;
		}

		quint32 filesCount = 0;
		stream >> filesCount;
		if (stream.status() != QDataStream::Ok || !filesCount) {
			SetUpdateError(error, QStringLiteral("Could not read v2 files count."));
			return std::nullopt;
		}
		if (!ExtractUpdateFiles(
				stream,
				filesCount,
				tempDirPath,
				&stagedFiles)) {
			SetUpdateError(error, QStringLiteral("Could not read v2 files."));
			return std::nullopt;
		}
	}
	const auto displayVersion = Updates::DisplayUpdateVersion(
		verified.envelope.version);
	if (displayVersion.isEmpty()) {
		SetUpdateError(error, QStringLiteral("Bad v2 display version."));
		return std::nullopt;
	}
	return Updates::StagedUpdateManifest{
		.packedVersion = verified.envelope.version,
		.displayVersion = displayVersion,
		.packageSha256 = Updates::Sha256Bytes(content),
		.files = std::move(stagedFiles),
	};
}

[[nodiscard]] bool ExpectedStageManifestHashFromSignedPackage(
		const QString &readyPath,
		quint64 readyPackedVersion,
		not_null<QByteArray*> result,
		QString *error) {
	const auto packagePath = Updates::StagePackagePath(readyPath);
	auto package = QFile(packagePath);
	if (!package.open(QIODevice::ReadOnly)
		|| package.size() <= 0
		|| package.size() > Loader::kMaxFileSize) {
		SetUpdateError(error, QStringLiteral(
			"Could not read retained signed update package."));
		return false;
	}
	const auto content = package.readAll();
	if (content.size() != package.size()) {
		SetUpdateError(error, QStringLiteral(
			"Could not read complete retained signed update package."));
		return false;
	}
	const auto target = Updates::TargetFromPlatformKey(
		Platform::AutoUpdateKey().toLatin1());
	if (!target) {
		SetUpdateError(error, QStringLiteral("No v2 target for platform key."));
		return false;
	}
	const auto verified = Updates::VerifyUpdate(
		content,
		BuildUpdateChannel,
		AppBetaVersion,
		*target,
		RunningUpdateVersion(),
		HeldManifest(),
		Updates::RootPublicKeyPem(),
		base::unixtime::now(),
		error);
	if (!verified) {
		return false;
	} else if (verified->envelope.version != readyPackedVersion) {
		SetUpdateError(error, QStringLiteral(
			"Retained signed package version does not match ready marker."));
		return false;
	}
	const auto expected = BuildStageManifestFromVerifiedPackage(
		*verified,
		content,
		QString(),
		error);
	if (!expected) {
		return false;
	}
	const auto bytes = Updates::SerializeStageManifest(*expected);
	if (bytes.isEmpty()) {
		SetUpdateError(error, QStringLiteral(
			"Could not serialize authenticated staged manifest."));
		return false;
	}
	*result = Updates::Sha256Bytes(bytes);
	return true;
}

[[nodiscard]] bool UnpackUpdateV2(
		const QString &filepath,
		const QByteArray &content) {
	// The expected target follows the feed key, not the build: an x64
	// build under Rosetta asks for armac and must accept that package.
	const auto target = Updates::TargetFromPlatformKey(
		Platform::AutoUpdateKey().toLatin1());
	if (!target) {
		LOG(("Update Error: No v2 target for platform key '%1'."
			).arg(Platform::AutoUpdateKey()));
		return false;
	}

	// The full verification happens before any decompression, so no
	// unauthenticated bytes ever reach the LZMA or QDataStream parsers.
	auto error = QString();
	const auto verified = Updates::VerifyUpdate(
		content,
		BuildUpdateChannel,
		AppBetaVersion,
		*target,
		RunningUpdateVersion(),
		HeldManifest(),
		Updates::RootPublicKeyPem(),
		base::unixtime::now(),
		&error);
	if (!verified) {
		LOG(("Update Error: v2 update rejected: %1").arg(error));
		return false;
	}
	if (verified->adoptManifest) {
		crl::on_main([manifest = verified->manifest] {
			AdoptManifest(manifest);
		});
	}

	const auto tempDirPath = cWorkingDir() + u"tupdates/temp"_q;
	const auto readyFilePath = cWorkingDir() + u"tupdates/temp/ready"_q;
	base::Platform::DeleteDirectory(tempDirPath);

	QDir tempDir(tempDirPath);
	if (tempDir.exists() || QFile(readyFilePath).exists()) {
		LOG(("Update Error: cant clear tupdates/temp dir!"));
		return false;
	}

	tempDir.mkdir(tempDir.absolutePath());

	auto stageError = QString();
	const auto stageManifest = BuildStageManifestFromVerifiedPackage(
		*verified,
		content,
		tempDirPath,
		&stageError);
	if (!stageManifest) {
		LOG(("Update Error: cant build staged manifest: %1").arg(stageError));
		return false;
	}
	if (!WriteUpdateVersionFile(
			tempDir,
			tempDirPath,
			verified->envelope.version)
		|| !WriteSignedUpdatePackage(tempDirPath, content)) {
		return false;
	}
	if (!Updates::WriteStageManifest(
			tempDirPath,
			*stageManifest,
			&stageError)) {
		LOG(("Update Error: cant write staged manifest: %1").arg(stageError));
		return false;
	}
	if (!WriteUpdateReadyFile(readyFilePath)) {
		return false;
	}
	QFile(filepath).remove();

	return true;
}

#endif // !TDESKTOP_DISABLE_AUTOUPDATE

bool UnpackUpdate(const QString &filepath) {
#ifndef TDESKTOP_DISABLE_AUTOUPDATE
	if (filepath.isEmpty()) {
		return true;
	}

	QFile input(filepath);
	if (!input.open(QIODevice::ReadOnly)) {
		LOG(("Update Error: cant read updates file!"));
		return false;
	} else if (input.size() > Loader::kMaxFileSize) {
		LOG(("Update Error: updates file is too large: %1").arg(input.size()));
		return false;
	}

	const auto content = input.readAll();
	input.close();
	if (!Updates::IsV2UpdateFile(content)) {
		LOG(("Update Error: Allowgram accepts only v2 signed updates."));
		return false;
	}
	return UnpackUpdateV2(filepath, content);
#else // !TDESKTOP_DISABLE_AUTOUPDATE
	return false;
#endif // TDESKTOP_DISABLE_AUTOUPDATE
}

template <typename Callback>
bool ParseCommonMap(
		const QByteArray &json,
		bool testing,
		Callback &&callback) {
	auto error = QJsonParseError{ 0, QJsonParseError::NoError };
	const auto document = QJsonDocument::fromJson(json, &error);
	if (error.error != QJsonParseError::NoError) {
		LOG(("Update Error: MTP failed to parse JSON, error: %1"
			).arg(error.errorString()));
		return false;
	} else if (!document.isObject()) {
		LOG(("Update Error: MTP not an object received in JSON."));
		return false;
	}
	const auto platforms = document.object();
	const auto platform = Platform::AutoUpdateKey();
	const auto it = platforms.constFind(platform);
	if (it == platforms.constEnd()) {
		LOG(("Update Error: MTP platform '%1' not found in response."
			).arg(platform));
		return false;
	} else if (!(*it).isObject()) {
		LOG(("Update Error: MTP not an object found for platform '%1'."
			).arg(platform));
		return false;
	}
	const auto types = (*it).toObject();
	const auto list = [&]() -> std::vector<QString> {
		if (cAlphaVersion()) {
			return { "alpha", "beta", "stable" };
		} else if (cInstallBetaVersion()) {
			return { "beta", "stable" };
		}
		return { "stable" };
	}();
	auto bestIsAvailableAlpha = false;
	auto bestAvailableVersion = 0ULL;
	for (const auto &type : list) {
		const auto it = types.constFind(type);
		if (it == types.constEnd()) {
			continue;
		} else if (!(*it).isObject()) {
			LOG(("Update Error: Not an object found for '%1:%2'."
				).arg(platform).arg(type));
			return false;
		}
		const auto map = (*it).toObject();
		const auto key = testing ? "testing" : "released";
		const auto version = map.constFind(key);
		if (version == map.constEnd()) {
			continue;
		}
		const auto isAvailableAlpha = (type == "alpha");
		const auto availableVersion = [&] {
			if ((*version).isString()) {
				const auto string = (*version).toString();
				if (const auto index = string.indexOf(':'); index > 0) {
					return base::StringViewMid(string, 0, index).toULongLong();
				}
				return string.toULongLong();
			} else if ((*version).isDouble()) {
				return uint64(base::SafeRound((*version).toDouble()));
			}
			return 0ULL;
		}();
		if (!availableVersion) {
			LOG(("Update Error: Version is not valid for '%1:%2:%3'."
				).arg(platform).arg(type).arg(key));
			return false;
		}
		const auto compare = isAvailableAlpha
			? availableVersion
			: availableVersion * 1000;
		const auto bestCompare = bestIsAvailableAlpha
			? bestAvailableVersion
			: bestAvailableVersion * 1000;
		if (compare > bestCompare) {
			bestAvailableVersion = availableVersion;
			bestIsAvailableAlpha = isAvailableAlpha;
			if (!callback(availableVersion, isAvailableAlpha, map)) {
				return false;
			}
		}
	}
	if (!bestAvailableVersion) {
		LOG(("Update Error: No valid entry found for platform '%1'."
			).arg(platform));
		return false;
	}
	return true;
}

Checker::Checker(bool testing) : _testing(testing) {
}

rpl::producer<std::shared_ptr<Loader>> Checker::ready() const {
	return _ready.events();
}

rpl::producer<Updates::StableReleaseAsset> Checker::mandatoryRelease() const {
	return _mandatoryRelease.events();
}

rpl::producer<> Checker::failed() const {
	return _failed.events();
}

bool Checker::poll() const {
	return true;
}

bool Checker::testing() const {
	return _testing;
}

void Checker::done(std::shared_ptr<Loader> result) {
	_ready.fire(std::move(result));
}

void Checker::notifyMandatoryRelease(Updates::StableReleaseAsset asset) {
	_mandatoryRelease.fire(std::move(asset));
}

void Checker::fail() {
	_failed.fire({});
}

rpl::lifetime &Checker::lifetime() {
	return _lifetime;
}

HttpChecker::HttpChecker(bool testing) : Checker(testing) {
}

void HttpChecker::start() {
	const auto url = QUrl(Updates::StableReleaseFeedUrl());
	if (!url.isValid()
		|| url.scheme() != QStringLiteral("https")
		|| url.host() != QStringLiteral("github.com")) {
		LOG(("Update Error: Bad Allowgram release feed URL."));
		crl::on_main(this, [=] { fail(); });
		return;
	}

	DEBUG_LOG(("Update Info: requesting Allowgram update feed"));
	auto request = QNetworkRequest(url);
	request.setAttribute(
		QNetworkRequest::RedirectPolicyAttribute,
		QNetworkRequest::NoLessSafeRedirectPolicy);
	request.setMaximumRedirectsAllowed(5);
	request.setTransferTimeout(30 * 1000);
	_manager = std::make_unique<QNetworkAccessManager>();
	_reply = _manager->get(request);
	_reply->connect(_reply, &QNetworkReply::finished, [=] {
		gotResponse();
	});
	_reply->connect(_reply, &QNetworkReply::errorOccurred, [=](auto e) {
		gotFailure(e);
	});
}

void HttpChecker::gotResponse() {
	if (!_reply) {
		return;
	}

	const auto statusCode = _reply->attribute(
		QNetworkRequest::HttpStatusCodeAttribute);
	const auto status = statusCode.isValid() ? statusCode.toInt() : 0;
	cSetLastUpdateCheck(base::unixtime::now());
	const auto response = _reply->readAll();
	clearSentRequest();

	if (status == 404) {
		LOG(("Update Info: No Allowgram release feed is published."));
		done(nullptr);
		return;
	} else if (statusCode.isValid() && status != 200) {
		LOG(("Update Error: Bad Allowgram feed HTTP status: %1").arg(status));
		fail();
		return;
	}

	if (response.size() >= kMaxResponseSize || !handleResponse(response)) {
		LOG(("Update Error: Bad Allowgram update feed size: %1"
			).arg(response.size()));
		fail();
	}
}

bool HttpChecker::handleResponse(const QByteArray &response) {
	auto error = QString();
	const auto parsed = Updates::ParseStableReleaseFeed(
		response,
		Platform::AutoUpdateKey().toLatin1(),
		RunningUpdateVersion(),
		HeldManifest(),
		base::unixtime::now(),
		&error);
	if (!parsed) {
		LOG(("Update Error: Bad Allowgram release feed: %1").arg(error));
		return false;
	}
	if (!parsed->updateAvailable) {
		done(nullptr);
		return true;
	}
	const auto &asset = parsed->asset;
	notifyMandatoryRelease(asset);
	done(std::make_shared<HttpLoader>(
		asset.url,
		asset.size,
		asset.sha256));
	return true;
}

void HttpChecker::clearSentRequest() {
	const auto reply = base::take(_reply);
	if (!reply) {
		return;
	}
	reply->disconnect(reply, &QNetworkReply::finished, nullptr, nullptr);
	reply->disconnect(reply, &QNetworkReply::errorOccurred, nullptr, nullptr);
	reply->abort();
	reply->deleteLater();
	_manager = nullptr;
}

void HttpChecker::gotFailure(QNetworkReply::NetworkError e) {
	if (!_reply) {
		return;
	}
	const auto statusCode = _reply->attribute(
		QNetworkRequest::HttpStatusCodeAttribute);
	if (statusCode.isValid() && statusCode.toInt() == 404) {
		LOG(("Update Info: No Allowgram release feed is published."));
		clearSentRequest();
		done(nullptr);
		return;
	}
	LOG(("Update Error: "
		"could not get Allowgram release feed %1").arg(e));
	clearSentRequest();
	fail();
}

HttpChecker::~HttpChecker() {
	clearSentRequest();
}

HttpLoader::HttpLoader(
		const QString &url,
		quint64 expectedSize,
		QByteArray expectedSha256)
: Loader(UpdatesFolder() + '/' + ExtractFilename(url), kChunkSize)
, _url(url)
, _filePath(UpdatesFolder() + '/' + ExtractFilename(url))
, _expectedSize(expectedSize)
, _expectedSha256(std::move(expectedSha256)) {
}

bool HttpLoader::validateChunk(
		const QByteArray &data,
		int64 totalSize) const {
	if (!_expectedSize || _expectedSha256.isEmpty()) {
		return true;
	} else if (totalSize > 0 && quint64(totalSize) != _expectedSize) {
		LOG(("Update Error: Downloaded size differs from feed: %1 / %2."
			).arg(totalSize
			).arg(_expectedSize));
		return false;
	}
	const auto already = alreadySize();
	const auto next = already + int64(data.size());
	if (next < 0 || quint64(next) > _expectedSize) {
		LOG(("Update Error: Download exceeds feed size: %1 / %2."
			).arg(next
			).arg(_expectedSize));
		return false;
	} else if (quint64(next) != _expectedSize) {
		return true;
	}

	auto hash = QCryptographicHash(QCryptographicHash::Sha256);
	if (already > 0) {
		QFile file(_filePath);
		if (!file.open(QIODevice::ReadOnly) || file.size() != already) {
			LOG(("Update Error: Could not read partial update for hashing."));
			return false;
		}
		hash.addData(file.readAll());
	}
	hash.addData(data);
	const auto actual = hash.result().toHex();
	if (actual != _expectedSha256) {
		LOG(("Update Error: Download SHA-256 mismatch."));
		return false;
	}
	return true;
}

bool HttpLoader::validateAlreadyComplete() const {
	return validateChunk(QByteArray(), int64(_expectedSize));
}

void HttpLoader::startLoading() {
	LOG(("Update Info: Loading using HTTP from '%1'.").arg(_url));

	_thread = std::make_unique<QThread>();
	_actor = new HttpLoaderActor(this, _thread.get(), _url);
	_thread->start();
}

HttpLoader::~HttpLoader() {
	if (const auto thread = base::take(_thread)) {
		if (const auto actor = base::take(_actor)) {
			QObject::connect(
				thread.get(),
				&QThread::finished,
				actor,
				&QObject::deleteLater);
		}
		thread->quit();
		thread->wait();
	}
}

HttpLoaderActor::HttpLoaderActor(
		not_null<HttpLoader*> parent,
		not_null<QThread*> thread,
		const QString &url)
: _parent(parent) {
	_url = url;
	moveToThread(thread);
	_manager.moveToThread(thread);

	connect(thread, &QThread::started, this, [=] { start(); });
}

void HttpLoaderActor::start() {
	sendRequest();
}

void HttpLoaderActor::sendRequest() {
	auto request = QNetworkRequest(_url);
	const auto rangeHeaderValue = "bytes="
		+ QByteArray::number(_parent->alreadySize())
		+ "-";
	request.setRawHeader("Range", rangeHeaderValue);
	request.setAttribute(
		QNetworkRequest::HttpPipeliningAllowedAttribute,
		true);
	_reply.reset(_manager.get(request));
	connect(
		_reply.get(),
		&QNetworkReply::downloadProgress,
		this,
		&HttpLoaderActor::partFinished);
	connect(
		_reply.get(),
		&QNetworkReply::errorOccurred,
		this,
		&HttpLoaderActor::partFailed);
	connect(
		_reply.get(),
		&QNetworkReply::metaDataChanged,
		this,
		&HttpLoaderActor::gotMetaData);
}

void HttpLoaderActor::gotMetaData() {
	const auto pairs = _reply->rawHeaderPairs();
	for (const auto &pair : pairs) {
		if (QString::fromUtf8(pair.first).toLower() == "content-range") {
			const auto m = QRegularExpression(u"/(\\d+)([^\\d]|$)"_q).match(QString::fromUtf8(pair.second));
			if (m.hasMatch()) {
				_parent->writeChunk({}, m.captured(1).toLongLong());
			}
		}
	}
}

void HttpLoaderActor::partFinished(qint64 got, qint64 total) {
	if (!_reply) return;

	const auto statusCode = _reply->attribute(
		QNetworkRequest::HttpStatusCodeAttribute);
	if (statusCode.isValid()) {
		const auto status = statusCode.toInt();
		if (status != 200 && status != 206 && status != 416) {
			LOG(("Update Error: "
				"Bad HTTP status received in partFinished(): %1"
				).arg(status));
			_parent->threadSafeFailed();
			return;
		}
	}

	DEBUG_LOG(("Update Info: part %1 of %2").arg(got).arg(total));

	const auto data = _reply->readAll();
	const auto totalSize = _parent->totalSize()
		? _parent->totalSize()
		: total;
	if (!_parent->validateChunk(data, totalSize)) {
		QFile(_parent->_filePath).remove();
		_parent->threadSafeFailed();
		return;
	}
	_parent->writeChunk(bytes::make_span(data), totalSize);
}

void HttpLoaderActor::partFailed(QNetworkReply::NetworkError e) {
	if (!_reply) return;

	const auto statusCode = _reply->attribute(
		QNetworkRequest::HttpStatusCodeAttribute);
	_reply.release()->deleteLater();
	if (statusCode.isValid()) {
		const auto status = statusCode.toInt();
		if (status == 416) { // Requested range not satisfiable
			if (_parent->validateAlreadyComplete()) {
				_parent->writeChunk({}, _parent->alreadySize());
			} else {
				QFile(_parent->_filePath).remove();
				_parent->threadSafeFailed();
			}
			return;
		}
	}
	LOG(("Update Error: failed to download part after %1, error %2"
		).arg(_parent->alreadySize()
		).arg(e));
	_parent->threadSafeFailed();
}

MtpChecker::MtpChecker(
	base::weak_ptr<Main::Session> session,
	bool testing)
: Checker(testing)
, _mtp(session) {
}

void MtpChecker::start() {
	if (!BuildIsCanary) {
		LOG(("Update Info: Allowgram stable updates skip MTP discovery."));
		crl::on_main(this, [=] { fail(); });
		return;
	}
	if (!_mtp.valid()) {
		LOG(("Update Info: MTP is unavailable."));
		crl::on_main(this, [=] { fail(); });
		return;
	}
	startCanary();
}

void MtpChecker::gotMessage(const MTPmessages_Messages &result) {
	const auto location = parseMessage(result);
	if (!location) {
		fail();
		return;
	} else if (location->username.isEmpty()) {
		done(nullptr);
		return;
	}
	const auto ready = [=](std::unique_ptr<MTP::DedicatedLoader> loader) {
		if (loader) {
			done(std::move(loader));
		} else {
			fail();
		}
	};
	MTP::StartDedicatedLoader(&_mtp, *location, UpdatesFolder(), ready);
}

auto MtpChecker::parseMessage(const MTPmessages_Messages &result) const
-> std::optional<FileLocation> {
	const auto message = MTP::GetMessagesElement(result);
	if (!message || message->type() != mtpc_message) {
		LOG(("Update Error: MTP feed message not found."));
		return std::nullopt;
	}
	return parseText(message->c_message().vmessage().v);
}

auto MtpChecker::parseText(const QByteArray &text) const
-> std::optional<FileLocation> {
	auto bestAvailableVersion = 0ULL;
	auto bestLocation = FileLocation();
	const auto accumulate = [&](
			uint64 version,
			bool isAlpha,
			const QJsonObject &map) {
		if (isAlpha) {
			LOG(("Update Error: MTP closed alpha found."));
			return false;
		}
		bestAvailableVersion = version;
		const auto key = testing() ? "testing" : "released";
		const auto entry = map.constFind(key);
		if (entry == map.constEnd()) {
			LOG(("Update Error: MTP entry not found for version %1."
				).arg(version));
			return false;
		} else if (!(*entry).isString()) {
			LOG(("Update Error: MTP entry is not a string for version %1."
				).arg(version));
			return false;
		}
		const auto full = (*entry).toString();
		const auto start = full.indexOf(':');
		const auto post = full.indexOf('#');
		if (start <= 0 || post < start) {
			LOG(("Update Error: MTP entry '%1' is bad for version %2."
				).arg(full
				).arg(version));
			return false;
		}
		bestLocation.username = full.mid(start + 1, post - start - 1);
		bestLocation.postId = base::StringViewMid(full, post + 1).toInt();
		if (bestLocation.username.isEmpty() || !bestLocation.postId) {
			LOG(("Update Error: MTP entry '%1' is bad for version %2."
				).arg(full
				).arg(version));
			return false;
		}
		return true;
	};
	const auto result = ParseCommonMap(text, testing(), accumulate);
	if (!result) {
		return std::nullopt;
	}
	return validateLatestLocation(bestAvailableVersion, bestLocation);
}

auto MtpChecker::validateLatestLocation(
		uint64 availableVersion,
		const FileLocation &location) const -> FileLocation {
	const auto myVersion = uint64(AppVersion);
	return (availableVersion <= myVersion) ? FileLocation() : location;
}

void MtpChecker::startCanary() {
	if (!CanaryMetadataMessageId) {
		LOG(("Update Error: Canary metadata message id is not set."));
		crl::on_main(this, [=] { fail(); });
		return;
	}
	// The branches are compile-time: the private one exists only in a
	// build that has a real channel id, so no other build compiles a
	// channelLoaded() whose result the optimizer can fold to nullptr and
	// then report as a null 'this' at the inputChannel() call below.
	if constexpr (BuildUpdateChannel == Updates::Channel::CanaryPublic) {
		const auto username = QString::fromLatin1(
			CanaryPublicChannelUsername);
		if (username.isEmpty()) {
			LOG(("Update Error: Canary channel username is not set."));
			crl::on_main(this, [=] { fail(); });
			return;
		}
		MTP::ResolveChannel(&_mtp, username, [=](
				const MTPInputChannel &channel) {
			requestCanaryMetadata(channel, CanaryMetadataMessageId, true);
		}, [=] { fail(); });
	} else if constexpr (CanaryPrivateChannelId != 0) {
		// Membership is enrollment for the private canary channel, so it
		// is located by numeric id and cached access hash on this
		// session, never through a username resolve.
		const auto session = _mtp.session().get();
		const auto channel = session
			? session->data().channelLoaded(
				ChannelId(BareId(CanaryPrivateChannelId)))
			: nullptr;
		if (!channel || !channel->amIn()) {
			LOG(("Update Error: Canary private channel is not available."));
			crl::on_main(this, [=] { fail(); });
			return;
		}
		requestCanaryMetadata(
			channel->inputChannel(),
			CanaryMetadataMessageId,
			true);
	} else {
		LOG(("Update Error: Canary private channel id is not set."));
		crl::on_main(this, [=] { fail(); });
	}
}

void MtpChecker::requestCanaryMetadata(
		const MTPInputChannel &channel,
		int messageId,
		bool fallbackToPinned) {
	_mtp.send(
		MTPchannels_GetMessages(
			channel,
			MTP_vector<MTPInputMessage>(
				1,
				MTP_inputMessageID(MTP_int(messageId)))),
		[=](const MTPmessages_Messages &result) {
			gotCanaryMessage(channel, result, messageId, fallbackToPinned);
		},
		failHandler());
}

void MtpChecker::requestCanaryPinnedFallback(const MTPInputChannel &channel) {
	_mtp.send(
		MTPchannels_GetFullChannel(channel),
		[=](const MTPmessages_ChatFull &result) {
			const auto pinnedId = result.data().vfull_chat().match([](
					const MTPDchannelFull &data) {
				return data.vpinned_msg_id().value_or_empty();
			}, [](const auto &) {
				return 0;
			});
			if (!pinnedId) {
				LOG(("Update Error: Canary pinned message not found."));
				fail();
				return;
			}
			requestCanaryMetadata(channel, pinnedId, false);
		},
		failHandler());
}

void MtpChecker::gotCanaryMessage(
		const MTPInputChannel &channel,
		const MTPmessages_Messages &result,
		int messageId,
		bool fallbackToPinned) {
	const auto message = MTP::GetMessagesElement(result, messageId);
	if (!message || message->type() != mtpc_message) {
		if (fallbackToPinned) {
			requestCanaryPinnedFallback(channel);
		} else {
			LOG(("Update Error: Canary metadata message not found."));
			fail();
		}
		return;
	}
	parseCanaryMetadata(channel, message->c_message().vmessage().v);
}

void MtpChecker::parseCanaryMetadata(
		const MTPInputChannel &channel,
		const QByteArray &text) {
	auto parseError = QJsonParseError();
	const auto document = QJsonDocument::fromJson(text, &parseError);
	if (parseError.error != QJsonParseError::NoError
		|| !document.isObject()) {
		LOG(("Update Error: Could not parse canary metadata JSON."));
		fail();
		return;
	}
	const auto object = document.object();
	if (object.value(u"format"_q).toDouble() != 1.) {
		LOG(("Update Error: Unknown canary metadata format."));
		fail();
		return;
	}

	const auto decode = [](const QJsonValue &value) {
		if (!value.isString()) {
			return QByteArray();
		}
		const auto decoded = QByteArray::fromBase64Encoding(
			value.toString().toLatin1(),
			QByteArray::AbortOnBase64DecodingErrors);
		return decoded ? decoded.decoded : QByteArray();
	};
	const auto manifest = decode(object.value(u"manifest"_q));
	const auto manifestSig = decode(object.value(u"manifest_sig"_q));
	if (!manifest.isEmpty() && !manifestSig.isEmpty()) {
		auto error = QString();
		if (auto parsed = Updates::ParseVerifiedManifest(
				manifest,
				manifestSig,
				Updates::RootPublicKeyPem(),
				&error)) {
			AdoptManifest(*parsed);
		} else {
			LOG(("Update Error: Bad canary metadata manifest: %1").arg(error));
		}
	}

	const auto channels = object.value(u"channels"_q).toObject();
	const auto platform = Platform::AutoUpdateKey();
	auto bestVersion = quint64(0);
	auto bestPostId = 0;
	const auto readU32 = [](const QJsonValue &value, quint32 *result) {
		const auto number = value.toDouble();
		if (!(number >= 0.) || !(number <= 4294967295.)) {
			return false;
		}
		*result = quint32(number);
		return (double(*result) == number);
	};
	const auto consider = [&](const QByteArray &name) {
		const auto entry = channels.value(QLatin1String(name)).toObject();
		if (entry.isEmpty()) {
			return;
		}
		auto base = quint32(0);
		auto counter = quint32(0);
		auto postId = quint32(0);
		if (!readU32(entry.value(u"base"_q), &base)
			|| !base
			|| !readU32(entry.value(u"counter"_q), &counter)
			|| !readU32(
				entry.value(u"posts"_q).toObject().value(platform),
				&postId)
			|| !postId
			|| postId > quint32(0x7FFFFFFF)) {
			return;
		}
		const auto version = Updates::MakeUpdateVersion(base, counter);
		if (version > bestVersion) {
			bestVersion = version;
			bestPostId = int(postId);
		}
	};
	consider(Updates::ChannelName(BuildUpdateChannel));
	if (BuildUpdateChannel == Updates::Channel::CanaryPublic) {
		// Dormancy rescue: a stale public canary channel may point to a
		// newer stable or beta release. Package verification still
		// enforces the strictly-greater-base channel policy on whatever
		// is downloaded.
		consider(Updates::ChannelName(Updates::Channel::Stable));
		consider(Updates::ChannelName(Updates::Channel::Beta));
	}
	if (!bestVersion || bestVersion <= RunningUpdateVersion()) {
		done(nullptr);
		return;
	}
	auto location = FileLocation();
	location.channelId = uint64(channel.c_inputChannel().vchannel_id().v);
	location.accessHash = uint64(channel.c_inputChannel().vaccess_hash().v);
	location.postId = bestPostId;
	const auto ready = [=](std::unique_ptr<MTP::DedicatedLoader> loader) {
		if (loader) {
			done(std::move(loader));
		} else {
			fail();
		}
	};
	MTP::StartDedicatedLoader(&_mtp, location, UpdatesFolder(), ready);
}

Fn<void(const MTP::Error &error)> MtpChecker::failHandler() {
	return [=](const MTP::Error &error) {
		LOG(("Update Error: MTP check failed with '%1'"
			).arg(QString::number(error.code()) + ':' + error.type()));
		fail();
	};
}

#if !defined Q_OS_WIN && !defined Q_OS_MAC
FlatpakChecker::FlatpakChecker(bool testing)
: Checker(testing)
, _watcher({u"/app"_q}) {
	FlatpakPortal::FlatpakProxy::new_for_bus(
			Gio::BusType::SESSION_,
			Gio::DBusProxyFlags::NONE_,
			kFlatpakPortalService,
			kFlatpakPortalObjectPath,
			crl::guard(this, [=](GObject::Object, Gio::AsyncResult res) {
		auto result = FlatpakPortal::FlatpakProxy::new_for_bus_finish(res);
		if (!result) {
			Gio::DBusErrorNS_::strip_remote_error(result.error());
			LOG(("Update Error: %1").arg(result.error().message_().c_str()));
			return;
		}

		_interface = *result;
		_interface.call_create_update_monitor(
				GLib::Variant::new_array(
					GLib::VariantType::new_("{sv}"),
					{}),
				[=](GObject::Object, Gio::AsyncResult res) {
			const auto result = _interface.call_create_update_monitor_finish(
				res);

			if (!result) {
				Gio::DBusErrorNS_::strip_remote_error(result.error());
				LOG(("Update Error: %1").arg(
					result.error().message_().c_str()));
				fail();
				return;
			}

			FlatpakPortal::FlatpakUpdateMonitorProxy::new_for_bus(
					Gio::BusType::SESSION_,
					Gio::DBusProxyFlags::NONE_,
					kFlatpakPortalService,
					std::get<1>(*result),
					crl::guard(this, [=](GObject::Object, Gio::AsyncResult res) {
				using FlatpakPortal::FlatpakUpdateMonitorProxy;
				auto result = FlatpakUpdateMonitorProxy::new_for_bus_finish(
					res);

				if (!result) {
					Gio::DBusErrorNS_::strip_remote_error(result.error());
					LOG(("Update Error: %1").arg(
						result.error().message_().c_str()));
					fail();
					return;
				}

				_monitor = *result;
				_updateAvailableSignal
					= _monitor.signal_update_available().connect([=](
							FlatpakPortal::FlatpakUpdateMonitor,
							GLib::Variant updateInfo) {
						done(std::make_shared<FlatpakLoader>(_monitor));
					});
			}));
		});
	}));

	QObject::connect(
		&_watcher,
		&QFileSystemWatcher::directoryChanged,
		[=](const QString &path) {
			start();
		});
}

void FlatpakChecker::start() {
	if (QFileInfo::exists(kFlatpakUpdated.utf16())) {
		done(std::make_shared<FlatpakLoader>(_monitor));
	}
}

bool FlatpakChecker::poll() const {
	return false;
}

FlatpakChecker::~FlatpakChecker() {
	if (_monitor) {
		_monitor.disconnect(_updateAvailableSignal);
		_monitor.call_close(nullptr);
	}
}

FlatpakLoader::FlatpakLoader(FlatpakPortal::FlatpakUpdateMonitor monitor)
: Loader({}, kChunkSize)
, _monitor(monitor) {
	if (!_monitor) {
		return;
	}

	_progressSignal = _monitor.signal_progress().connect([=](
			FlatpakPortal::FlatpakUpdateMonitor,
			GLib::Variant info) {
		auto dict = GLib::VariantDict::new_(info);
		switch (dict.lookup_value("status").get_uint32()) {
		case 0: {
			const auto n_ops = dict.lookup_value("n_ops").get_uint32();
			const auto op = dict.lookup_value("op").get_uint32();
			const auto progress = dict.lookup_value("progress").get_uint32();
			threadSafeProgress({
				int64(
					std::round((op + (progress / 100.)) / n_ops * 104857600)),
				104857600,
				true,
			});
		} break;
		case 1:
		case 2: threadSafeReady(); break;
		case 3: {
			LOG(("Update Error: %1").arg(
				dict.lookup_value("error_message").get_string(
					nullptr).c_str()));
			threadSafeFailed();
		} break;
		}
	});
}

void FlatpakLoader::startLoading() {
	if (QFileInfo::exists(kFlatpakUpdated.utf16())) {
		threadSafeReady();
	}

	if (!_monitor) {
		return;
	}

	_monitor.call_update(
		base::Platform::XDP::ParentWindowID(),
		GLib::Variant::new_array(
			GLib::VariantType::new_("{sv}"),
			{}),
		crl::guard(this, [=](GObject::Object, Gio::AsyncResult res) {
			const auto result = _monitor.call_close_finish(res);
			if (!result) {
				Gio::DBusErrorNS_::strip_remote_error(result.error());
				LOG(("Update Error: %1").arg(
					result.error().message_().c_str()));
				threadSafeFailed();
			}
		}));
}

FlatpakLoader::~FlatpakLoader() {
	if (_monitor) {
		_monitor.disconnect(_progressSignal);
	}
}
#endif // !Q_OS_WIN && !Q_OS_MAC

} // namespace

bool UpdaterDisabled() {
	return UpdaterIsDisabled;
}

void SetUpdaterDisabledAtStartup() {
	Expects(UpdaterInstance.lock() == nullptr);

#ifndef TDESKTOP_DISABLE_AUTOUPDATE
	LOG(("Update Info: ignoring legacy startup updater disable."));
#else // TDESKTOP_DISABLE_AUTOUPDATE
	UpdaterIsDisabled = true;
#endif // TDESKTOP_DISABLE_AUTOUPDATE
}

class Updater : public base::has_weak_ptr {
public:
	Updater();

	rpl::producer<> checking() const;
	rpl::producer<> isLatest() const;
	rpl::producer<Progress> progress() const;
	rpl::producer<> failed() const;
	rpl::producer<> ready() const;
	rpl::producer<Updates::MandatoryUpdateState> mandatoryUpdate();

	Updates::MandatoryUpdateState mandatoryUpdateState();
	void dismissMandatoryUpdatePopup();
	void applyMandatoryUpdateNow();
	bool mandatoryUpdateLocked() const;

	void start(bool forceWait);
	void stop();
	void test();

	State state() const;
	int already() const;
	int size() const;
	bool percent() const;

	void setMtproto(base::weak_ptr<Main::Session> session);

	~Updater();

private:
	enum class Action {
		Waiting,
		Checking,
		Loading,
		Unpacking,
		Ready,
	};
	void check();
	void startImplementation(
		not_null<Implementation*> which,
		std::unique_ptr<Checker> checker);
	bool tryLoaders();
	void handleTimeout();
	void checkerDone(
		not_null<Implementation*> which,
		std::shared_ptr<Loader> loader);
	void checkerFail(not_null<Implementation*> which);

	void finalize(QString filepath);
	void unpackDone(bool ready);
	void handleChecking();
	void handleProgress();
	void handleLatest();
	void handleFailed();
	void handleReady();
	void scheduleNext();
	void handleMandatoryRelease(const Updates::StableReleaseAsset &asset);
	void loadMandatoryUpdate(bool showPopup = true);
	void ensureMandatoryUpdateLoaded();
	void refreshMandatoryUpdate();
	void publishMandatoryUpdate(Updates::MandatoryUpdateState state);
	void maybeShowMandatoryUpdatePopup();
	void maybeShowMandatoryUpdateLock();
	bool mandatoryUpdateNeedsLock() const;

	bool _testing = false;
	Action _action = Action::Waiting;
	base::Timer _timer;
	base::Timer _retryTimer;
	base::Timer _mandatoryTimer;
	rpl::event_stream<> _checking;
	rpl::event_stream<> _isLatest;
	rpl::event_stream<Progress> _progress;
	rpl::event_stream<> _failed;
	rpl::event_stream<> _ready;
	rpl::event_stream<Updates::MandatoryUpdateState> _mandatoryUpdate;
	Implementation _httpImplementation;
	Implementation _mtpImplementation;
	Implementation _flatpakImplementation;
	std::shared_ptr<Loader> _activeLoader;
	bool _usingMtprotoLoader = (cAlphaVersion() != 0);
	base::weak_ptr<Main::Session> _session;
	Updates::MandatoryUpdateState _mandatory;
	bool _mandatoryLoaded = false;
	bool _mandatoryPopupShown = false;

	rpl::lifetime _lifetime;

};

Updater::Updater()
: _timer([=] { check(); })
, _retryTimer([=] { handleTimeout(); })
, _mandatoryTimer([=] { refreshMandatoryUpdate(); }) {
	checking() | rpl::on_next([=] {
		handleChecking();
	}, _lifetime);
	progress() | rpl::on_next([=] {
		handleProgress();
	}, _lifetime);
	failed() | rpl::on_next([=] {
		handleFailed();
	}, _lifetime);
	ready() | rpl::on_next([=] {
		handleReady();
	}, _lifetime);
	isLatest() | rpl::on_next([=] {
		handleLatest();
	}, _lifetime);
}

rpl::producer<> Updater::checking() const {
	return _checking.events();
}

rpl::producer<> Updater::isLatest() const {
	return _isLatest.events();
}

auto Updater::progress() const
-> rpl::producer<Progress> {
	return _progress.events();
}

rpl::producer<> Updater::failed() const {
	return _failed.events();
}

rpl::producer<> Updater::ready() const {
	return _ready.events();
}

rpl::producer<Updates::MandatoryUpdateState> Updater::mandatoryUpdate() {
	ensureMandatoryUpdateLoaded();
	auto state = _mandatory;
	return _mandatoryUpdate.events_starting_with(std::move(state));
}

Updates::MandatoryUpdateState Updater::mandatoryUpdateState() {
	ensureMandatoryUpdateLoaded();
	return _mandatory;
}

void Updater::check() {
	start(false);
}

void Updater::handleReady() {
	stop();
	_action = Action::Ready;
	if (mandatoryUpdateNeedsLock()) {
		PrepareMandatoryUpdateApply();
		Restart();
		return;
	}
	if (!Quitting()) {
		cSetLastUpdateCheck(base::unixtime::now());
		Local::writeSettings();
	}
}

void Updater::handleFailed() {
	scheduleNext();
}

void Updater::handleLatest() {
	if (const auto update = FindUpdateFile(); !update.isEmpty()) {
		QFile(update).remove();
	}
	scheduleNext();
}

void Updater::handleChecking() {
	_action = Action::Checking;
	_retryTimer.callOnce(kUpdaterTimeout);
}

void Updater::handleProgress() {
	_retryTimer.callOnce(kUpdaterTimeout);
}

void Updater::scheduleNext() {
	stop();
	if (!Quitting()) {
		cSetLastUpdateCheck(base::unixtime::now());
		Local::writeSettings();
		start(true);
	}
}

void Updater::publishMandatoryUpdate(Updates::MandatoryUpdateState state) {
	if (state == _mandatory) {
		return;
	}
	_mandatory = std::move(state);
	_mandatoryUpdate.fire_copy(_mandatory);
}

void Updater::loadMandatoryUpdate(bool showPopup) {
	_mandatoryLoaded = true;
	const auto now = base::unixtime::now();
	const auto stored = Updates::ReadMandatoryUpdateState(cWorkingDir());
	auto state = stored.value_or(Updates::MandatoryUpdateState());
	if (stored
		&& Updates::MandatoryStatus(*stored, RunningUpdateVersion(), now)
			== Updates::MandatoryUpdateStatus::None) {
		Updates::ClearMandatoryUpdateState(cWorkingDir());
		state = Updates::MandatoryUpdateState();
	}
	publishMandatoryUpdate(std::move(state));
	_mandatoryTimer.cancel();
	if (_mandatory.active && !Quitting()) {
		_mandatoryTimer.callOnce(crl::time(1000));
	}
	maybeShowMandatoryUpdateLock();
	if (showPopup) {
		maybeShowMandatoryUpdatePopup();
	}
}

void Updater::ensureMandatoryUpdateLoaded() {
	if (!_mandatoryLoaded) {
		loadMandatoryUpdate();
	}
}

void Updater::refreshMandatoryUpdate() {
	loadMandatoryUpdate();
	const auto now = base::unixtime::now();
	if (Updates::MandatoryStatus(_mandatory, RunningUpdateVersion(), now)
		== Updates::MandatoryUpdateStatus::Expired) {
		applyMandatoryUpdateNow();
	}
}

void Updater::handleMandatoryRelease(
		const Updates::StableReleaseAsset &asset) {
	const auto now = base::unixtime::now();
	const auto current = Updates::ReadMandatoryUpdateState(cWorkingDir());
	const auto next = Updates::RegisterMandatoryUpdate(
		current,
		Updates::MandatoryTargetFromAsset(asset),
		now);
	if (!next.active) {
		return;
	}
	auto error = QString();
	if (!Updates::WriteMandatoryUpdateState(cWorkingDir(), next, &error)) {
		LOG(("Update Error: Could not persist mandatory update: %1"
			).arg(error));
		return;
	}
	publishMandatoryUpdate(next);
	_mandatoryTimer.callOnce(crl::time(1000));
	maybeShowMandatoryUpdatePopup();
}

void Updater::dismissMandatoryUpdatePopup() {
	ensureMandatoryUpdateLoaded();
	const auto now = base::unixtime::now();
	if (Updates::MandatoryStatus(_mandatory, RunningUpdateVersion(), now)
		== Updates::MandatoryUpdateStatus::None) {
		return;
	}
	const auto next = Updates::DismissMandatoryUpdatePopup(_mandatory);
	auto error = QString();
	if (Updates::WriteMandatoryUpdateState(cWorkingDir(), next, &error)) {
		publishMandatoryUpdate(next);
	}
}

void Updater::applyMandatoryUpdateNow() {
	loadMandatoryUpdate(false);
	const auto now = base::unixtime::now();
	if (Updates::MandatoryStatus(_mandatory, RunningUpdateVersion(), now)
		== Updates::MandatoryUpdateStatus::None) {
		return;
	}
	const auto next = Updates::MarkMandatoryUpdateApplyStarted(_mandatory);
	auto error = QString();
	if (Updates::WriteMandatoryUpdateState(cWorkingDir(), next, &error)) {
		publishMandatoryUpdate(next);
	}
	PrepareMandatoryUpdateApply();
	if (checkReadyUpdate()) {
		_action = Action::Ready;
		Restart();
		return;
	}
	maybeShowMandatoryUpdateLock();
	cSetLastUpdateCheck(0);
	if (_action == Action::Waiting) {
		start(false);
	}
}

void Updater::maybeShowMandatoryUpdatePopup() {
	const auto now = base::unixtime::now();
	if (mandatoryUpdateNeedsLock()) {
		maybeShowMandatoryUpdateLock();
		return;
	}
	if (_mandatoryPopupShown
		|| _mandatory.popupDismissed
		|| Updates::MandatoryStatus(_mandatory, RunningUpdateVersion(), now)
			== Updates::MandatoryUpdateStatus::None
		|| !IsAppLaunched()) {
		return;
	}
	const auto window = App().activePrimaryWindow();
	if (!window) {
		return;
	}
	_mandatoryPopupShown = true;
	const auto remaining = Updates::FormatMandatoryUpdateTime(
		Updates::MandatorySecondsRemaining(_mandatory, now));
	window->show(Ui::MakeConfirmBox({
		.text = u"A new Allowgram update is available. Allowgram will update automatically in %1. Save your work; active calls will end when the countdown finishes."_q.arg(remaining),
		.confirmed = [=] {
			_mandatoryPopupShown = false;
			applyMandatoryUpdateNow();
		},
		.cancelled = [=] {
			_mandatoryPopupShown = false;
			dismissMandatoryUpdatePopup();
		},
		.confirmText = u"Update now"_q,
		.cancelText = u"Close"_q,
		.title = u"Update Allowgram"_q,
	}));
}
bool Updater::mandatoryUpdateNeedsLock() const {
	const auto status = Updates::MandatoryStatus(
		_mandatory,
		RunningUpdateVersion(),
		base::unixtime::now());
	return _mandatory.active
		&& (_mandatory.applyStarted
			|| status == Updates::MandatoryUpdateStatus::Expired);
}

bool Updater::mandatoryUpdateLocked() const {
	return MandatoryUpdateLockShown;
}

void Updater::maybeShowMandatoryUpdateLock() {
	if (!mandatoryUpdateNeedsLock() || !IsAppLaunched() || Quitting()) {
		return;
	} else if (MandatoryUpdateLockShown) {
		return;
	}
	const auto window = App().activePrimaryWindow();
	if (!window) {
		return;
	}

	_mandatoryPopupShown = false;
	window->hideSettingsAndLayer(anim::type::instant);

	auto content = Box([=](not_null<Ui::GenericBox*> box) {
		Ui::ConfirmBox(box, {
			.text = u"Allowgram must install the signed stable update before "_q
				+ u"chats can be used again. If automatic update keeps failing, "_q
				+ u"open the Allowgram releases page and install the latest "_q
				+ u"stable build manually."_q,
			.confirmed = [] {
				UpdateChecker().applyMandatoryUpdateNow();
			},
			.cancelled = [] {
				Quit(QuitReason::Update);
			},
			.confirmText = u"Try update again"_q,
			.cancelText = u"Quit Allowgram"_q,
			.title = u"Allowgram update required"_q,
			.strictCancel = true,
		});
		box->addLeftButton(rpl::single(u"Open releases"_q), [] {
			UrlClickHandler::Open(
				"https://github.com/molotovgit/allowgram/releases");
		});
		box->setCloseByEscape(false);
		box->setCloseByOutsideClick(false);
		QObject::connect(box.get(), &QObject::destroyed, [] {
			MandatoryUpdateLockBox = nullptr;
			MandatoryUpdateLockShown = false;
			if (!Quitting()) {
				crl::on_main([] {
					UpdateChecker().mandatoryUpdateState();
				});
			}
		});
	});
	MandatoryUpdateLockBox = window->show(
		std::move(content),
		Ui::LayerOption::CloseOther,
		anim::type::normal);
	MandatoryUpdateLockShown = true;
}

auto Updater::state() const -> State {
	if (_action == Action::Ready) {
		return State::Ready;
	} else if (_action == Action::Loading) {
		return State::Download;
	}
	return State::None;
}

int Updater::size() const {
	return _activeLoader ? _activeLoader->totalSize() : 0;
}

int Updater::already() const {
	return _activeLoader ? _activeLoader->alreadySize() : 0;
}

bool Updater::percent() const {
	return _activeLoader ? _activeLoader->preferPercent() : 0;
}

void Updater::stop() {
	_httpImplementation = Implementation();
	_mtpImplementation = Implementation();
	_flatpakImplementation = Implementation{
		std::move(_flatpakImplementation.checker)
	};
	_activeLoader = nullptr;
	_action = Action::Waiting;
}

void Updater::start(bool forceWait) {
	if (cExeName().isEmpty()) {
		return;
	}

	_timer.cancel();
	loadMandatoryUpdate();
	if (_action != Action::Waiting) {
		return;
	}

	_retryTimer.cancel();
	const auto now = base::unixtime::now();
	const auto mandatoryKnown = Updates::MandatoryStatus(
		_mandatory,
		RunningUpdateVersion(),
		now) != Updates::MandatoryUpdateStatus::None;
	const auto constDelay = cAlphaVersion() ? 600 : UpdateDelayConstPart;
	const auto randDelay = cAlphaVersion() ? 300 : UpdateDelayRandPart;
	const auto updateInSecs = cLastUpdateCheck()
		+ constDelay
		+ int(rand() % randDelay)
		- now;
	auto sendRequest = mandatoryKnown
		|| (updateInSecs <= 0)
		|| (updateInSecs > constDelay + randDelay);
	if (!sendRequest && !forceWait) {
		if (!FindUpdateFile().isEmpty()) {
			sendRequest = true;
		}
	}
	if (cManyInstance() && !Logs::DebugEnabled() && !mandatoryKnown) {
		// Only main instance is updating when no mandatory update is known.
		return;
	}

	if (KSandbox::isFlatpak()) {
#if !defined Q_OS_WIN && !defined Q_OS_MAC
		if (!_flatpakImplementation.checker) {
			startImplementation(
				&_flatpakImplementation,
				std::make_unique<FlatpakChecker>(_testing));
		}
#endif // !Q_OS_WIN && !Q_OS_MAC
	} else if (sendRequest) {
		startImplementation(
			&_httpImplementation,
			BuildIsCanary
				? nullptr
				: std::make_unique<HttpChecker>(_testing));
		startImplementation(
			&_mtpImplementation,
			BuildIsCanary
				? std::make_unique<MtpChecker>(
					LookupCanaryPrivateSession(_session),
					_testing)
				: nullptr);

		_checking.fire({});
	} else {
		_timer.callOnce((updateInSecs + 5) * crl::time(1000));
	}
}

void Updater::startImplementation(
		not_null<Implementation*> which,
		std::unique_ptr<Checker> checker) {
	if (!checker) {
		class EmptyChecker : public Checker {
		public:
			EmptyChecker() : Checker(false) {
			}

			void start() override {
				crl::on_main(this, [=] { fail(); });
			}

		};
		checker = std::make_unique<EmptyChecker>();
	}

	checker->ready(
	) | rpl::on_next([=](std::shared_ptr<Loader> &&loader) {
		checkerDone(which, std::move(loader));
	}, checker->lifetime());
	checker->failed(
	) | rpl::on_next([=] {
		checkerFail(which);
	}, checker->lifetime());
	checker->mandatoryRelease(
	) | rpl::on_next([=](Updates::StableReleaseAsset asset) {
		handleMandatoryRelease(asset);
	}, checker->lifetime());

	*which = Implementation{ std::move(checker) };

	crl::on_main(which->checker.get(), [=] {
		which->checker->start();
	});
}

void Updater::checkerDone(
		not_null<Implementation*> which,
		std::shared_ptr<Loader> loader) {
	if (which->checker->poll()) which->checker = nullptr;
	which->loader = std::move(loader);

	tryLoaders();
}

void Updater::checkerFail(not_null<Implementation*> which) {
	which->checker = nullptr;
	which->failed = true;

	tryLoaders();
}

void Updater::test() {
	_testing = true;
	cSetLastUpdateCheck(0);
	start(false);
}

void Updater::setMtproto(base::weak_ptr<Main::Session> session) {
	_session = session;
}

void Updater::handleTimeout() {
	if (_action == Action::Checking) {
		const auto reset = [&](Implementation &which) {
			if (base::take(which.checker)) {
				which.failed = true;
			}
		};
		reset(_httpImplementation);
		reset(_mtpImplementation);
		if (!tryLoaders()) {
			cSetLastUpdateCheck(0);
			_timer.callOnce(kUpdaterTimeout);
		}
	} else if (_action == Action::Loading) {
		_failed.fire({});
	}
}

bool Updater::tryLoaders() {
	if (_httpImplementation.checker || _mtpImplementation.checker) {
		// Some checkers didn't finish yet.
		return true;
	}
	_retryTimer.cancel();

	const auto tryOne = [&](Implementation &which) {
		_activeLoader = std::move(which.loader);
		if (const auto loader = _activeLoader.get()) {
			_action = Action::Loading;

			loader->progress(
			) | rpl::start_to_stream(_progress, loader->lifetime());
			loader->ready(
			) | rpl::on_next([=](QString &&filepath) {
				finalize(std::move(filepath));
			}, loader->lifetime());
			loader->failed(
			) | rpl::on_next([=] {
				_failed.fire({});
			}, loader->lifetime());

			_retryTimer.callOnce(kUpdaterTimeout);
			loader->wipeFolder();
			loader->start();
		} else {
			_isLatest.fire({});
		}
	};
	if (KSandbox::isFlatpak()) {
		if (_flatpakImplementation.failed) {
			_failed.fire({});
			return false;
		} else {
			tryOne(_flatpakImplementation);
		}
	} else if (_mtpImplementation.failed && _httpImplementation.failed) {
		_failed.fire({});
		return false;
	} else if (!_mtpImplementation.loader) {
		tryOne(_httpImplementation);
	} else if (!_httpImplementation.loader) {
		tryOne(_mtpImplementation);
	} else {
		tryOne(_usingMtprotoLoader
			? _mtpImplementation
			: _httpImplementation);
		_usingMtprotoLoader = !_usingMtprotoLoader;
	}
	return true;
}

void Updater::finalize(QString filepath) {
	if (_action != Action::Loading) {
		return;
	}
	_retryTimer.cancel();
	_activeLoader = nullptr;
	_action = Action::Unpacking;
	crl::async([=] {
		const auto ready = UnpackUpdate(filepath);
		crl::on_main([=] {
			GetUpdaterInstance()->unpackDone(ready);
		});
	});
}

void Updater::unpackDone(bool ready) {
	if (ready) {
		_ready.fire({});
	} else {
		ClearAll();
		_failed.fire({});
	}
}

Updater::~Updater() {
	stop();
}

UpdateChecker::UpdateChecker()
: _updater(GetUpdaterInstance()) {
	if (IsAppLaunched() && Core::App().domain().started()) {
		if (const auto session = Core::App().activeAccount().maybeSession()) {
			_updater->setMtproto(session);
		}
	}
}

rpl::producer<> UpdateChecker::checking() const {
	return _updater->checking();
}

rpl::producer<> UpdateChecker::isLatest() const {
	return _updater->isLatest();
}

auto UpdateChecker::progress() const
-> rpl::producer<Progress> {
	return _updater->progress();
}

rpl::producer<> UpdateChecker::failed() const {
	return _updater->failed();
}

rpl::producer<> UpdateChecker::ready() const {
	return _updater->ready();
}

rpl::producer<Updates::MandatoryUpdateState> UpdateChecker::mandatoryUpdate() const {
	return _updater->mandatoryUpdate();
}

Updates::MandatoryUpdateState UpdateChecker::mandatoryUpdateState() const {
	return _updater->mandatoryUpdateState();
}

void UpdateChecker::dismissMandatoryUpdatePopup() {
	_updater->dismissMandatoryUpdatePopup();
}

void UpdateChecker::applyMandatoryUpdateNow() {
	_updater->applyMandatoryUpdateNow();
}

bool UpdateChecker::mandatoryUpdateLocked() const {
	return _updater->mandatoryUpdateLocked();
}

void UpdateChecker::start(bool forceWait) {
	_updater->start(forceWait);
}

void UpdateChecker::test() {
	_updater->test();
}

void UpdateChecker::setMtproto(base::weak_ptr<Main::Session> session) {
	_updater->setMtproto(session);
}

void UpdateChecker::stop() {
	_updater->stop();
}

auto UpdateChecker::state() const
-> State {
	return _updater->state();
}

int UpdateChecker::already() const {
	return _updater->already();
}

int UpdateChecker::size() const {
	return _updater->size();
}

bool UpdateChecker::percent() const {
	return _updater->percent();
}

//QString winapiErrorWrap() {
//	WCHAR errMsg[2048];
//	DWORD errorCode = GetLastError();
//	LPTSTR errorText = NULL, errorTextDefault = L"(Unknown error)";
//	FormatMessage(FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_IGNORE_INSERTS, NULL, errorCode, MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT), (LPTSTR)&errorText, 0, 0);
//	if (!errorText) {
//		errorText = errorTextDefault;
//	}
//	StringCbPrintf(errMsg, sizeof(errMsg), L"Error code: %d, error message: %s", errorCode, errorText);
//	if (errorText != errorTextDefault) {
//		LocalFree(errorText);
//	}
//	return QString::fromWCharArray(errMsg);
//}

bool checkReadyUpdate() {
	CheckedReadyUpdateStageHash.clear();
	const auto readyFilePath = cWorkingDir() + u"tupdates/temp/ready"_q;
	const auto readyPath = cWorkingDir() + u"tupdates/temp"_q;
	if (!QFile(readyFilePath).exists() || cExeName().isEmpty()) {
		if (QDir(cWorkingDir() + u"tupdates/ready"_q).exists()
			|| QDir(cWorkingDir() + u"tupdates/temp"_q).exists()) {
			ClearAll();
		}
		return false;
	}

	// check ready version
	auto readyPackedVersion = quint64(0);
	const auto versionPath = readyPath + u"/tdata/version"_q;
	{
		QFile fVersion(versionPath);
		if (!fVersion.open(QIODevice::ReadOnly)) {
			LOG(("Update Error: cant read version file '%1'"
				).arg(versionPath));
			ClearAll();
			return false;
		}
		auto versionNum = VersionInt();
		if (fVersion.read((char*)&versionNum, sizeof(VersionInt))
			!= sizeof(VersionInt)) {
			LOG(("Update Error: cant read version from file '%1'"
				).arg(versionPath));
			ClearAll();
			return false;
		}
		if (versionNum == kVersionFilePackedMarker) {
			quint64 packedVersion = 0;
			if (fVersion.read((char*)&packedVersion, sizeof(quint64))
				!= sizeof(quint64)) {
				LOG(("Update Error: cant read packed version from file '%1'"
					).arg(versionPath));
				ClearAll();
				return false;
			}
			if (packedVersion <= RunningUpdateVersion()) {
				LOG(("Update Error: cant install update version %1 having version %2"
					).arg(packedVersion
					).arg(RunningUpdateVersion()));
				ClearAll();
				return false;
			}
			readyPackedVersion = packedVersion;
		} else {
			LOG(("Update Error: legacy ready update marker rejected: %1"
				).arg(versionNum));
			ClearAll();
			return false;
		}
		fVersion.close();
	}

	auto stageError = QString();
	auto expectedStageHash = QByteArray();
	if (!ExpectedStageManifestHashFromSignedPackage(
			readyPath,
			readyPackedVersion,
			&expectedStageHash,
			&stageError)) {
		LOG(("Update Error: retained signed update rejected: %1").arg(stageError));
		ClearAll();
		return false;
	}
	const auto staged = Updates::VerifyStagedUpdate(
		readyPath,
		RunningUpdateVersion(),
		expectedStageHash,
		&stageError);
	if (!staged || staged->packedVersion != readyPackedVersion) {
		LOG(("Update Error: staged update rejected: %1").arg(stageError));
		ClearAll();
		return false;
	}
	CheckedReadyUpdateStageHash = staged->manifestSha256.toHex();

#ifdef Q_OS_WIN
	const auto curUpdater = cExeDir() + u"AllowgramUpdater.exe"_q;
	const auto updater = QFileInfo(
		cWorkingDir() + u"tupdates/temp/AllowgramUpdater.exe"_q);
#elif defined Q_OS_MAC // Q_OS_WIN
	const auto curUpdater = cExeDir()
		+ cExeName()
		+ u"/Contents/Frameworks/Updater"_q;
	const auto updater = QFileInfo(
		cWorkingDir()
		+ u"tupdates/temp/Telegram.app/Contents/Frameworks/Updater"_q);
#else // Q_OS_MAC
	const auto curUpdater = cExeDir() + u"Updater"_q;
	const auto updater = QFileInfo(cWorkingDir() + u"tupdates/temp/Updater"_q);
#endif // else for Q_OS_WIN || Q_OS_MAC
	if (!updater.exists()) {
		ClearAll();
		return false;
	}
#ifdef Q_OS_WIN
	if (CopyFile(
			updater.absoluteFilePath().toStdWString().c_str(),
			curUpdater.toStdWString().c_str(),
			FALSE) == FALSE) {
		const auto errorCode = GetLastError();
		if (errorCode == ERROR_ACCESS_DENIED) {
			cSetWriteProtected(true);
			return true;
		} else {
			ClearAll();
			return false;
		}
	}
#elif defined Q_OS_MAC // Q_OS_WIN
	QDir().mkpath(QFileInfo(curUpdater).absolutePath());
	DEBUG_LOG(("Update Info: moving %1 to %2..."
		).arg(updater.absoluteFilePath()).arg(curUpdater));
	if (!objc_moveFile(updater.absoluteFilePath(), curUpdater)) {
		ClearAll();
		return false;
	}
#else // Q_OS_MAC
	if (QFile::exists(curUpdater)
		&& unlink(QFile::encodeName(curUpdater).constData())) {
		if (errno == EACCES) {
			DEBUG_LOG(("Update Info: "
				"could not unlink current Updater, access denied."));
			cSetWriteProtected(true);
			return true;
		} else {
			DEBUG_LOG(("Update Error: could not unlink current Updater."));
			ClearAll();
			return false;
		}
	}
	if (!linuxMoveFile(
			QFile::encodeName(updater.absoluteFilePath()).constData(),
			QFile::encodeName(curUpdater).constData())) {
		if (errno == EACCES) {
			DEBUG_LOG(("Update Info: "
				"could not copy new Updater, access denied."));
			cSetWriteProtected(true);
			return true;
		} else {
			DEBUG_LOG(("Update Error: could not copy new Updater."));
			ClearAll();
			return false;
		}
	}
#endif // else for Q_OS_WIN || Q_OS_MAC

#ifdef Q_OS_MAC
	base::Platform::RemoveQuarantine(QFileInfo(curUpdater).absolutePath());
	base::Platform::RemoveQuarantine(updater.absolutePath());
#endif // Q_OS_MAC

	return true;
}

QString ReadyUpdateStageHash() {
	return QString::fromLatin1(CheckedReadyUpdateStageHash);
}

bool MandatoryUpdateKnown() {
	const auto state = UpdateChecker().mandatoryUpdateState();
	return Updates::MandatoryStatus(
		state,
		RunningUpdateVersion(),
		base::unixtime::now()) != Updates::MandatoryUpdateStatus::None;
}

bool MandatoryUpdateBlocksUse() {
	const auto state = UpdateChecker().mandatoryUpdateState();
	const auto status = Updates::MandatoryStatus(
		state,
		RunningUpdateVersion(),
		base::unixtime::now());
	return state.applyStarted
		|| (status == Updates::MandatoryUpdateStatus::Expired);
}

void UpdateApplication() {
	{
		Core::UpdateChecker checker;
		const auto mandatory = checker.mandatoryUpdateState();
		if (Updates::MandatoryStatus(
				mandatory,
				RunningUpdateVersion(),
				base::unixtime::now()) != Updates::MandatoryUpdateStatus::None) {
			checker.applyMandatoryUpdateNow();
			return;
		}
	}
	if (UpdaterDisabled()) {
		UrlClickHandler::Open(
			"https://github.com/molotovgit/allowgram/releases");
	} else {
		cSetAutoUpdate(true);
		const auto window = Core::IsAppLaunched()
			? Core::App().activePrimaryWindow()
			: nullptr;
		if (window) {
			if (const auto controller = window->sessionController()) {
				controller->showSection(
					std::make_shared<Info::Memento>(
						Info::Settings::Tag{ controller->session().user() },
						::Settings::AdvancedId()),
					Window::SectionShow());
			} else {
				window->widget()->showSpecialLayer(
					Box<::Settings::LayerWidget>(window),
					anim::type::normal);
			}
			window->widget()->showFromTray();
		}
		cSetLastUpdateCheck(0);
		Core::UpdateChecker().start();
	}
}

QString countAlphaVersionSignature(uint64 version) { // duplicated in packer.cpp
	if (cAlphaPrivateKey().isEmpty()) {
		LOG(("Error: Trying to count alpha version signature without alpha private key!"));
		return QString();
	}

	QByteArray signedData = (qstr("TelegramBeta_") + QString::number(version, 16).toLower()).toUtf8();

	static const int32 shaSize = 20, keySize = 128;

	uchar sha1Buffer[shaSize];
	hashSha1(signedData.constData(), signedData.size(), sha1Buffer); // count sha1

	uint32 siglen = 0;

	RSA *prKey = [] {
		const auto bio = MakeBIO(
			const_cast<char*>(cAlphaPrivateKey().constData()),
			-1);
		return PEM_read_bio_RSAPrivateKey(bio.get(), 0, 0, 0);
	}();
	if (!prKey) {
		LOG(("Error: Could not read alpha private key!"));
		return QString();
	}
	if (RSA_size(prKey) != keySize) {
		LOG(("Error: Bad alpha private key size: %1").arg(RSA_size(prKey)));
		RSA_free(prKey);
		return QString();
	}
	QByteArray signature;
	signature.resize(keySize);
	if (RSA_sign(NID_sha1, (const uchar*)(sha1Buffer), shaSize, (uchar*)(signature.data()), &siglen, prKey) != 1) { // count signature
		LOG(("Error: Counting alpha version signature failed!"));
		RSA_free(prKey);
		return QString();
	}
	RSA_free(prKey);

	if (siglen != keySize) {
		LOG(("Error: Bad alpha version signature length: %1").arg(siglen));
		return QString();
	}

	signature = signature.toBase64(QByteArray::Base64UrlEncoding | QByteArray::OmitTrailingEquals);
	signature = signature.replace('-', '8').replace('_', 'B');
	return QString::fromUtf8(signature.mid(19, 32));
}

} // namespace Core

/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "core/update_stage.h"

#include <QtCore/QCryptographicHash>
#include <QtCore/QDir>
#include <QtCore/QDirIterator>
#include <QtCore/QFile>
#include <QtCore/QFileInfo>
#include <QtCore/QSet>

#ifdef Q_OS_WIN
#include <windows.h>
#endif // Q_OS_WIN

#include <algorithm>
#include <array>
#include <cstring>
#include <utility>

namespace Core::Updates {
namespace {

constexpr auto kStageMagic = "AGST";
constexpr auto kStageFormat = quint32(1);
constexpr auto kStageHashSize = 32;
constexpr auto kMaxStageManifestSize = 32 * 1024;
constexpr auto kMaxStageFilesCount = quint32(32);
constexpr auto kMaxStagePathSize = quint32(240);

void SetError(QString *error, const QString &text) {
	if (error) {
		*error = text;
	}
}

void AppendLeU32(QByteArray &to, quint32 value) {
	const char bytes[4] = {
		char(value & 0xFF),
		char((value >> 8) & 0xFF),
		char((value >> 16) & 0xFF),
		char((value >> 24) & 0xFF),
	};
	to.append(bytes, 4);
}

void AppendLeU64(QByteArray &to, quint64 value) {
	AppendLeU32(to, quint32(value & 0xFFFFFFFFULL));
	AppendLeU32(to, quint32(value >> 32));
}

struct Reader {
	const uchar *data = nullptr;
	size_t size = 0;
	size_t offset = 0;

	[[nodiscard]] bool read(void *to, size_t count) {
		if (count > size - offset) {
			return false;
		}
		memcpy(to, data + offset, count);
		offset += count;
		return true;
	}

	[[nodiscard]] std::optional<quint32> readU32() {
		auto bytes = std::array<uchar, 4>();
		if (!read(bytes.data(), bytes.size())) {
			return std::nullopt;
		}
		return quint32(bytes[0])
			| (quint32(bytes[1]) << 8)
			| (quint32(bytes[2]) << 16)
			| (quint32(bytes[3]) << 24);
	}

	[[nodiscard]] std::optional<quint64> readU64() {
		const auto low = readU32();
		const auto high = readU32();
		if (!low || !high) {
			return std::nullopt;
		}
		return quint64(*low) | (quint64(*high) << 32);
	}

	[[nodiscard]] std::optional<QByteArray> readBytes(quint32 count) {
		if (size_t(count) > size - offset) {
			return std::nullopt;
		}
		auto result = QByteArray(
			reinterpret_cast<const char*>(data + offset),
			qsizetype(count));
		offset += count;
		return result;
	}
};

[[nodiscard]] bool HasFile(
		const std::vector<StagedUpdateFile> &files,
		const QString &path) {
	const auto folded = path.toCaseFolded();
	for (const auto &file : files) {
		if (file.path.toCaseFolded() == folded) {
			return true;
		}
	}
	return false;
}

#ifdef Q_OS_WIN
[[nodiscard]] bool HasReparsePoint(const QString &path) {
	const auto native = QDir::toNativeSeparators(path).toStdWString();
	const auto attributes = GetFileAttributesW(native.c_str());
	return attributes != INVALID_FILE_ATTRIBUTES
		&& (attributes & FILE_ATTRIBUTE_REPARSE_POINT);
}

[[nodiscard]] bool HasReparsePointParent(
		const QString &root,
		const QString &target) {
	const auto relative = QDir(root).relativeFilePath(
		QFileInfo(target).absolutePath());
	if (relative.startsWith("..") || QDir::isAbsolutePath(relative)) {
		return true;
	}
	auto current = QDir(root).absolutePath();
	for (const auto &part : relative.split('/', Qt::SkipEmptyParts)) {
		current += '/' + part;
		if (HasReparsePoint(current)) {
			return true;
		}
	}
	return false;
}
#endif // Q_OS_WIN

[[nodiscard]] std::optional<QByteArray> HashFile(
		const QString &path,
		quint64 expectedSize,
		QString *error) {
	auto file = QFile(path);
	if (!file.open(QIODevice::ReadOnly)) {
		SetError(error, QStringLiteral("Could not open staged file."));
		return std::nullopt;
	}
	if (file.size() < 0 || quint64(file.size()) != expectedSize) {
		SetError(error, QStringLiteral("Staged file size changed."));
		return std::nullopt;
	}
	auto hash = QCryptographicHash(QCryptographicHash::Sha256);
	while (!file.atEnd()) {
		const auto chunk = file.read(1024 * 1024);
		if (chunk.isEmpty() && file.error() != QFileDevice::NoError) {
			SetError(error, QStringLiteral("Could not read staged file."));
			return std::nullopt;
		}
		hash.addData(chunk);
	}
	return hash.result();
}

[[nodiscard]] bool ValidateStageTree(
		const QString &tempDirPath,
		const StagedUpdateManifest &manifest,
		QString *error) {
	const auto root = QDir(tempDirPath).absolutePath();
	if (!QDir(root).exists()) {
		SetError(error, QStringLiteral("Staged update directory is missing."));
		return false;
	}
#ifdef Q_OS_WIN
	if (HasReparsePoint(root)) {
		SetError(error, QStringLiteral("Staged update root is a reparse point."));
		return false;
	}
#endif // Q_OS_WIN
	for (const auto &entry : manifest.files) {
		const auto path = QDir(root).filePath(entry.path);
		const auto info = QFileInfo(path);
		if (!info.exists() || !info.isFile() || info.isSymLink()) {
			SetError(error, QStringLiteral("Expected staged file is missing."));
			return false;
		}
#ifdef Q_OS_WIN
		if (HasReparsePointParent(root, path) || HasReparsePoint(path)) {
			SetError(error, QStringLiteral("Staged file crosses a reparse point."));
			return false;
		}
#endif // Q_OS_WIN
		const auto hash = HashFile(path, entry.size, error);
		if (!hash || *hash != entry.sha256) {
			SetError(error, QStringLiteral("Staged file hash changed."));
			return false;
		}
	}
	const auto allowedSpecial = QSet<QString>{
		QStringLiteral("ready"),
		QStringLiteral("tdata/version"),
		QStringLiteral("tdata/stage-manifest.bin"),
		QStringLiteral("tdata/package.tdup"),
	};
	auto iterator = QDirIterator(
		root,
		QDir::AllEntries | QDir::NoDotAndDotDot,
		QDirIterator::Subdirectories);
	while (iterator.hasNext()) {
		iterator.next();
		const auto info = iterator.fileInfo();
		const auto relative = QDir(root).relativeFilePath(
			info.absoluteFilePath()).replace('\\', '/');
		if (info.isSymLink()) {
			SetError(error, QStringLiteral("Staged tree contains a symlink."));
			return false;
		}
#ifdef Q_OS_WIN
		if (HasReparsePoint(info.absoluteFilePath())) {
			SetError(error, QStringLiteral("Staged tree contains a reparse point."));
			return false;
		}
#endif // Q_OS_WIN
		if (info.isDir()) {
			if (relative != QStringLiteral("tdata")) {
				SetError(error, QStringLiteral("Staged tree contains an unexpected directory."));
				return false;
			}
		} else if (!allowedSpecial.contains(relative)
			&& !HasFile(manifest.files, relative)) {
			SetError(error, QStringLiteral("Staged tree contains an unexpected file."));
			return false;
		}
	}
	return true;
}

} // namespace

QByteArray Sha256Bytes(const QByteArray &data) {
	return QCryptographicHash::hash(data, QCryptographicHash::Sha256);
}

QByteArray Sha256Hex(const QByteArray &data) {
	return Sha256Bytes(data).toHex();
}

std::optional<QString> NormalizeUpdatePayloadPath(QString relativeName) {
	relativeName.replace('\\', '/');
	if (relativeName.isEmpty()
		|| relativeName.startsWith('/')
		|| relativeName.contains(':')
		|| QDir::isAbsolutePath(relativeName)) {
		return std::nullopt;
	}
	const auto parts = relativeName.split('/');
	if (parts.isEmpty()) {
		return std::nullopt;
	}
	for (const auto &part : parts) {
		if (part.isEmpty() || part == "." || part == "..") {
			return std::nullopt;
		}
	}
	const auto cleaned = QDir::cleanPath(relativeName);
	return (cleaned == relativeName) ? std::make_optional(cleaned) : std::nullopt;
}

bool UpdatePayloadFileAllowed(const QString &relativeName) {
#ifdef Q_OS_WIN
	static const auto Allowed = QSet<QString>{
		QStringLiteral("Allowgram.exe"),
		QStringLiteral("AllowgramUpdater.exe"),
		QStringLiteral("build-info.json"),
		QStringLiteral("LEGAL"),
		QStringLiteral("LICENSE"),
		QStringLiteral("README.txt"),
	};
	return Allowed.contains(relativeName);
#else // Q_OS_WIN
	return true;
#endif // Q_OS_WIN
}

QByteArray SerializeStageManifest(const StagedUpdateManifest &manifest) {
	if (!manifest.packedVersion
		|| manifest.displayVersion.isEmpty()
		|| manifest.packageSha256.size() != kStageHashSize
		|| manifest.files.empty()
		|| manifest.files.size() > kMaxStageFilesCount) {
		return QByteArray();
	}
	auto result = QByteArray(kStageMagic, 4);
	AppendLeU32(result, kStageFormat);
	AppendLeU64(result, manifest.packedVersion);
	const auto display = manifest.displayVersion.toUtf8();
	if (display.isEmpty() || display.size() > int(kMaxStagePathSize)) {
		return QByteArray();
	}
	AppendLeU32(result, quint32(display.size()));
	result.append(display);
	result.append(manifest.packageSha256);
	AppendLeU32(result, quint32(manifest.files.size()));
	auto seen = QSet<QString>();
	for (const auto &file : manifest.files) {
		const auto normalized = NormalizeUpdatePayloadPath(file.path);
		if (!normalized
			|| *normalized != file.path
			|| !UpdatePayloadFileAllowed(file.path)
			|| file.sha256.size() != kStageHashSize) {
			return QByteArray();
		}
		const auto path = file.path.toUtf8();
		if (path.isEmpty() || path.size() > int(kMaxStagePathSize)) {
			return QByteArray();
		}
		const auto collisionKey = file.path.toCaseFolded();
		if (seen.contains(collisionKey)) {
			return QByteArray();
		}
		seen.insert(collisionKey);
		AppendLeU32(result, quint32(path.size()));
		result.append(path);
		AppendLeU64(result, file.size);
		result.append(file.sha256);
	}
	return result.size() <= kMaxStageManifestSize ? result : QByteArray();
}

std::optional<StagedUpdateManifest> ParseStageManifest(
		const QByteArray &data,
		QString *error) {
	if (data.size() < 4 || data.size() > kMaxStageManifestSize) {
		SetError(error, QStringLiteral("Bad staged manifest size."));
		return std::nullopt;
	}
	auto reader = Reader{
		reinterpret_cast<const uchar*>(data.constData()),
		size_t(data.size()),
	};
	char magic[4] = {};
	if (!reader.read(magic, sizeof(magic)) || memcmp(magic, kStageMagic, 4)) {
		SetError(error, QStringLiteral("Bad staged manifest magic."));
		return std::nullopt;
	}
	const auto format = reader.readU32();
	const auto packedVersion = reader.readU64();
	const auto displaySize = reader.readU32();
	if (!format || *format != kStageFormat || !packedVersion || !*packedVersion
		|| !displaySize || !*displaySize || *displaySize > kMaxStagePathSize) {
		SetError(error, QStringLiteral("Bad staged manifest header."));
		return std::nullopt;
	}
	const auto displayBytes = reader.readBytes(*displaySize);
	const auto packageHash = reader.readBytes(kStageHashSize);
	const auto filesCount = reader.readU32();
	if (!displayBytes || !packageHash || !filesCount
		|| !*filesCount || *filesCount > kMaxStageFilesCount) {
		SetError(error, QStringLiteral("Bad staged manifest metadata."));
		return std::nullopt;
	}
	auto display = QString::fromUtf8(*displayBytes);
	if (display.toUtf8() != *displayBytes) {
		SetError(error, QStringLiteral("Bad staged manifest display version."));
		return std::nullopt;
	}
	auto result = StagedUpdateManifest();
	result.packedVersion = *packedVersion;
	result.displayVersion = std::move(display);
	result.packageSha256 = *packageHash;
	result.files.reserve(size_t(*filesCount));
	auto seen = QSet<QString>();
	for (auto i = quint32(0); i != *filesCount; ++i) {
		const auto pathSize = reader.readU32();
		if (!pathSize || !*pathSize || *pathSize > kMaxStagePathSize) {
			SetError(error, QStringLiteral("Bad staged manifest path size."));
			return std::nullopt;
		}
		const auto pathBytes = reader.readBytes(*pathSize);
		const auto size = reader.readU64();
		const auto hash = reader.readBytes(kStageHashSize);
		if (!pathBytes || !size || !hash) {
			SetError(error, QStringLiteral("Bad staged manifest entry."));
			return std::nullopt;
		}
		const auto path = QString::fromUtf8(*pathBytes);
		const auto normalized = NormalizeUpdatePayloadPath(path);
		if (path.toUtf8() != *pathBytes
			|| !normalized
			|| *normalized != path
			|| !UpdatePayloadFileAllowed(path)) {
			SetError(error, QStringLiteral("Bad staged manifest path."));
			return std::nullopt;
		}
		const auto collisionKey = path.toCaseFolded();
		if (seen.contains(collisionKey)) {
			SetError(error, QStringLiteral("Duplicate staged manifest path."));
			return std::nullopt;
		}
		seen.insert(collisionKey);
		result.files.push_back({ path, *size, *hash });
	}
	if (reader.offset != reader.size) {
		SetError(error, QStringLiteral("Trailing staged manifest bytes."));
		return std::nullopt;
	}
#ifdef Q_OS_WIN
	for (const auto &required : {
			QStringLiteral("Allowgram.exe"),
			QStringLiteral("AllowgramUpdater.exe"),
			QStringLiteral("build-info.json") }) {
		if (!HasFile(result.files, required)) {
			SetError(error, QStringLiteral("Required staged file is missing."));
			return std::nullopt;
		}
	}
#endif // Q_OS_WIN
	return result;
}

QString StageManifestPath(const QString &tempDirPath) {
	return QDir(tempDirPath).filePath(QStringLiteral("tdata/stage-manifest.bin"));
}

QString StagePackagePath(const QString &tempDirPath) {
	return QDir(tempDirPath).filePath(QStringLiteral("tdata/package.tdup"));
}

bool WriteStageManifest(
		const QString &tempDirPath,
		const StagedUpdateManifest &manifest,
		QString *error) {
	const auto bytes = SerializeStageManifest(manifest);
	if (bytes.isEmpty()) {
		SetError(error, QStringLiteral("Could not serialize staged manifest."));
		return false;
	}
	const auto path = StageManifestPath(tempDirPath);
	if (!QDir().mkpath(QFileInfo(path).absolutePath())) {
		SetError(error, QStringLiteral("Could not create staged manifest path."));
		return false;
	}
	auto file = QFile(path);
	if (!file.open(QIODevice::WriteOnly) || file.write(bytes) != bytes.size()) {
		SetError(error, QStringLiteral("Could not write staged manifest."));
		return false;
	}
	return true;
}

std::optional<StagedUpdateReady> VerifyStagedUpdate(
		const QString &tempDirPath,
		quint64 runningVersion,
		const QByteArray &expectedManifestSha256,
		QString *error) {
	const auto path = StageManifestPath(tempDirPath);
	auto file = QFile(path);
	if (!file.open(QIODevice::ReadOnly) || file.size() > kMaxStageManifestSize) {
		SetError(error, QStringLiteral("Could not read staged manifest."));
		return std::nullopt;
	}
	const auto bytes = file.readAll();
	const auto hash = Sha256Bytes(bytes);
	if (!expectedManifestSha256.isEmpty()
		&& expectedManifestSha256 != hash) {
		SetError(error, QStringLiteral("Staged manifest hash changed."));
		return std::nullopt;
	}
	const auto parsed = ParseStageManifest(bytes, error);
	if (!parsed) {
		return std::nullopt;
	}
	if (parsed->packedVersion <= runningVersion) {
		SetError(error, QStringLiteral("Staged update version is not newer."));
		return std::nullopt;
	}
	if (!ValidateStageTree(tempDirPath, *parsed, error)) {
		return std::nullopt;
	}
	return StagedUpdateReady{ parsed->packedVersion, hash };
}

} // namespace Core::Updates
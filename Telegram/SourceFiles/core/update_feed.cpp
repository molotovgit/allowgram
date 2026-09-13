/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "core/update_feed.h"

#include <QtCore/QJsonArray>
#include <QtCore/QJsonDocument>
#include <QtCore/QJsonObject>
#include <QtCore/QRegularExpression>

#include <cmath>
#include <utility>
#include <vector>

namespace Core::Updates {
namespace {

constexpr auto kMaxFeedSize = 64 * 1024;
constexpr auto kMaxSignedFeedSize = 32 * 1024;
constexpr auto kMaxFeedSignatures = 8;
constexpr auto kMaxKeyIdSize = 64;
constexpr auto kMaxSignatureSize = 512;
constexpr auto kMaxSequence = quint64(65535);
constexpr auto kAllowgramProduct = "Allowgram";
constexpr auto kStableChannel = "stable";
constexpr auto kFeedAsset = "allowgram-update-feed.json";
constexpr auto kFeedSigningDomain = "Allowgram stable release feed v1\n";

void SetError(QString *error, const QString &message) {
	if (error) {
		*error = message;
	}
}

[[nodiscard]] std::optional<quint64> ReadInt(
		const QJsonValue &value,
		quint64 max) {
	if (!value.isDouble()) {
		return std::nullopt;
	}
	const auto number = value.toDouble();
	if (!std::isfinite(number)
		|| number < 0.
		|| number > double(max)
		|| std::floor(number) != number) {
		return std::nullopt;
	}
	return quint64(number);
}

[[nodiscard]] std::optional<QByteArray> DecodeBase64Url(
		const QJsonValue &value,
		int maxSize) {
	if (!value.isString()) {
		return std::nullopt;
	}
	const auto decoded = QByteArray::fromBase64Encoding(
		value.toString().toLatin1(),
		QByteArray::Base64UrlEncoding
			| QByteArray::AbortOnBase64DecodingErrors);
	if (!decoded || decoded.decoded.isEmpty()
		|| decoded.decoded.size() > maxSize) {
		return std::nullopt;
	}
	return decoded.decoded;
}

[[nodiscard]] std::optional<std::vector<EnvelopeSignature>> ParseSignatures(
		const QJsonValue &value) {
	if (!value.isArray()) {
		return std::nullopt;
	}
	const auto list = value.toArray();
	if (list.isEmpty() || list.size() > kMaxFeedSignatures) {
		return std::nullopt;
	}
	auto result = std::vector<EnvelopeSignature>();
	result.reserve(size_t(list.size()));
	for (const auto &entry : list) {
		if (!entry.isObject()) {
			return std::nullopt;
		}
		const auto object = entry.toObject();
		const auto keyId = object.value(QStringLiteral("key_id"));
		if (!keyId.isString()) {
			return std::nullopt;
		}
		auto id = keyId.toString().toLatin1();
		if (id.isEmpty() || id.size() > kMaxKeyIdSize) {
			return std::nullopt;
		}
		const auto signature = DecodeBase64Url(
			object.value(QStringLiteral("signature")),
			kMaxSignatureSize);
		if (!signature) {
			return std::nullopt;
		}
		result.push_back({ std::move(id), *signature });
	}
	return result;
}

struct DisplayVersion {
	quint32 base = 0;
	quint32 sequence = 0;
};

[[nodiscard]] std::optional<DisplayVersion> ParseDisplayVersion(
		const QString &display) {
	static const auto Pattern = QRegularExpression(
		QStringLiteral(R"(^(\d{1,3})\.(\d{1,3})\.(\d{1,3})\.(\d{1,5})$)"));
	const auto match = Pattern.match(display);
	if (!match.hasMatch()) {
		return std::nullopt;
	}
	auto ok = false;
	const auto major = match.captured(1).toUInt(&ok);
	if (!ok) {
		return std::nullopt;
	}
	const auto minor = match.captured(2).toUInt(&ok);
	if (!ok || minor > 999) {
		return std::nullopt;
	}
	const auto patch = match.captured(3).toUInt(&ok);
	if (!ok || patch > 999) {
		return std::nullopt;
	}
	const auto sequence = match.captured(4).toUInt(&ok);
	if (!ok || !sequence || sequence > kMaxSequence) {
		return std::nullopt;
	}
	const auto base = quint64(major) * 1000000ULL
		+ quint64(minor) * 1000ULL
		+ quint64(patch);
	if (!base || base > 999999999ULL) {
		return std::nullopt;
	}
	return DisplayVersion{ quint32(base), sequence };
}

[[nodiscard]] bool IsHexSha256(const QString &value) {
	static const auto Pattern = QRegularExpression(
		QStringLiteral("^[0-9a-fA-F]{64}$"));
	return Pattern.match(value).hasMatch();
}

[[nodiscard]] bool ValidReleaseTag(
		const QString &tag,
		const QString &display) {
	return tag == (QStringLiteral("v") + display);
}

} // namespace

QByteArray StableReleaseFeedSigningInput(const QByteArray &signedBytes) {
	return QByteArray(kFeedSigningDomain) + signedBytes;
}

QString StableReleaseFeedUrl() {
	return QStringLiteral(
		"https://github.com/molotovgit/allowgram/releases/latest/download/%1"
	).arg(QString::fromLatin1(kFeedAsset));
}

QString StableReleaseDownloadUrl(
		const QString &tag,
		const QString &fileName) {
	return QStringLiteral(
		"https://github.com/molotovgit/allowgram/releases/download/%1/%2"
	).arg(tag, fileName);
}

QString StableReleaseFileName(Target target, const QString &displayVersion) {
	return QStringLiteral("allowgram-update-stable-%1-%2-%3.tdup")
		.arg(QString::fromLatin1(OsName(target.os)))
		.arg(QString::fromLatin1(ArchName(target.arch)))
		.arg(displayVersion);
}

std::optional<StableReleaseFeed> ParseStableReleaseFeed(
		const QByteArray &response,
		const QByteArray &platformKey,
		quint64 runningVersion,
		const std::optional<Manifest> &trustedManifest,
		qint64 now,
		QString *error) {
	if (response.isEmpty() || response.size() > kMaxFeedSize) {
		SetError(error, QStringLiteral("Bad feed size."));
		return std::nullopt;
	}
	auto parseError = QJsonParseError();
	const auto document = QJsonDocument::fromJson(response, &parseError);
	if (parseError.error != QJsonParseError::NoError || !document.isObject()) {
		SetError(error, QStringLiteral("Could not parse release feed."));
		return std::nullopt;
	}
	const auto object = document.object();
	const auto format = ReadInt(object.value(QStringLiteral("format")), 1);
	if (!format || *format != 1) {
		SetError(error, QStringLiteral("Unknown release feed format."));
		return std::nullopt;
	} else if (!trustedManifest) {
		SetError(error, QStringLiteral("No trusted release manifest."));
		return std::nullopt;
	} else if (trustedManifest->expires > 0 && trustedManifest->expires <= now) {
		SetError(error, QStringLiteral("Trusted release manifest expired."));
		return std::nullopt;
	}

	const auto signedBytes = DecodeBase64Url(
		object.value(QStringLiteral("signed")),
		kMaxSignedFeedSize);
	const auto signatures = ParseSignatures(
		object.value(QStringLiteral("signatures")));
	if (!signedBytes || !signatures) {
		SetError(error, QStringLiteral("Bad release feed signature block."));
		return std::nullopt;
	}
	auto authError = QString();
	if (!VerifyChannelAuthorization(
			*trustedManifest,
			Channel::Stable,
			StableReleaseFeedSigningInput(*signedBytes),
			*signatures,
			now,
			&authError)) {
		SetError(
			error,
			QStringLiteral("Release feed is not authenticated: %1"
				).arg(authError));
		return std::nullopt;
	}

	const auto signedDocument = QJsonDocument::fromJson(*signedBytes, &parseError);
	if (parseError.error != QJsonParseError::NoError
		|| !signedDocument.isObject()) {
		SetError(error, QStringLiteral("Could not parse signed release feed."));
		return std::nullopt;
	}
	const auto signedObject = signedDocument.object();
	const auto signedFormat = ReadInt(
		signedObject.value(QStringLiteral("format")),
		1);
	if (!signedFormat || *signedFormat != 1) {
		SetError(error, QStringLiteral("Unknown signed release feed format."));
		return std::nullopt;
	} else if (signedObject.value(QStringLiteral("product")).toString()
		!= kAllowgramProduct) {
		SetError(error, QStringLiteral("Release feed is not for Allowgram."));
		return std::nullopt;
	} else if (signedObject.value(QStringLiteral("channel")).toString()
		!= kStableChannel) {
		SetError(error, QStringLiteral("Release feed is not stable."));
		return std::nullopt;
	}

	const auto release = signedObject.value(QStringLiteral("release"));
	if (!release.isObject()) {
		SetError(error, QStringLiteral("Release feed release is missing."));
		return std::nullopt;
	}
	const auto releaseObject = release.toObject();
	const auto tag = releaseObject.value(QStringLiteral("tag")).toString();
	if (releaseObject.value(QStringLiteral("draft")).toBool(true)
		|| releaseObject.value(QStringLiteral("prerelease")).toBool(true)) {
		SetError(error, QStringLiteral("Release feed is not a stable release."));
		return std::nullopt;
	}

	const auto version = signedObject.value(QStringLiteral("version"));
	if (!version.isObject()) {
		SetError(error, QStringLiteral("Release feed version is missing."));
		return std::nullopt;
	}
	const auto versionObject = version.toObject();
	const auto display = versionObject.value(
		QStringLiteral("display")).toString();
	const auto parsedDisplay = ParseDisplayVersion(display);
	const auto base = ReadInt(
		versionObject.value(QStringLiteral("base")),
		999999999ULL);
	const auto sequence = ReadInt(
		versionObject.value(QStringLiteral("sequence")),
		kMaxSequence);
	if (!parsedDisplay
		|| !base
		|| !sequence
		|| !*base
		|| !*sequence
		|| parsedDisplay->base != *base
		|| parsedDisplay->sequence != *sequence
		|| !ValidReleaseTag(tag, display)) {
		SetError(
			error,
			QStringLiteral("Release feed version is inconsistent."));
		return std::nullopt;
	}

	const auto target = TargetFromPlatformKey(platformKey);
	if (!target) {
		SetError(error, QStringLiteral("Unknown release feed platform."));
		return std::nullopt;
	}
	const auto files = signedObject.value(QStringLiteral("files"));
	if (!files.isObject()) {
		SetError(error, QStringLiteral("Release feed files are missing."));
		return std::nullopt;
	}
	const auto entry = files.toObject().value(QString::fromLatin1(platformKey));
	if (!entry.isObject()) {
		SetError(
			error,
			QStringLiteral("Release feed has no file for this platform."));
		return std::nullopt;
	}
	const auto fileObject = entry.toObject();
	const auto os = QString::fromLatin1(OsName(target->os));
	const auto arch = QString::fromLatin1(ArchName(target->arch));
	const auto name = fileObject.value(QStringLiteral("file")).toString();
	const auto size = ReadInt(
		fileObject.value(QStringLiteral("size")),
		kMaxPayloadSize);
	const auto sha256 = fileObject.value(QStringLiteral("sha256")).toString();
	if (fileObject.value(QStringLiteral("os")).toString() != os
		|| fileObject.value(QStringLiteral("arch")).toString() != arch
		|| name != StableReleaseFileName(*target, display)
		|| !size
		|| !*size
		|| !IsHexSha256(sha256)) {
		SetError(
			error,
			QStringLiteral("Release feed file metadata is inconsistent."));
		return std::nullopt;
	}

	auto result = StableReleaseFeed();
	result.asset.fileName = name;
	result.asset.tag = tag;
	result.asset.url = StableReleaseDownloadUrl(tag, name);
	result.asset.size = *size;
	result.asset.sha256 = sha256.toLatin1().toLower();
	result.asset.baseVersion = *base;
	result.asset.sequence = *sequence;
	result.asset.packedVersion = MakeUpdateVersion(*base, *sequence);
	result.asset.displayVersion = display;
	result.updateAvailable = (result.asset.packedVersion > runningVersion);
	return result;
}

} // namespace Core::Updates

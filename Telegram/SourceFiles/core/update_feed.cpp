/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "core/update_feed.h"

#include <QtCore/QJsonDocument>
#include <QtCore/QJsonObject>
#include <QtCore/QRegularExpression>

#include <cmath>

namespace Core::Updates {
namespace {

constexpr auto kMaxFeedSize = 64 * 1024;
constexpr auto kMaxSequence = quint64(1000000);
constexpr auto kAllowgramProduct = "Allowgram";
constexpr auto kStableChannel = "stable";
constexpr auto kFeedAsset = "allowgram-update-feed.json";

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

struct DisplayVersion {
	quint32 base = 0;
	quint32 sequence = 0;
};

[[nodiscard]] std::optional<DisplayVersion> ParseDisplayVersion(
		const QString &display) {
	static const auto Pattern = QRegularExpression(
		QStringLiteral(R"(^(\d{1,3})\.(\d{1,3})\.(\d{1,3})\.(\d{1,6})$)"));
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

} // namespace

QString StableReleaseFeedUrl() {
	return StableReleaseDownloadUrl(QString::fromLatin1(kFeedAsset));
}

QString StableReleaseDownloadUrl(const QString &fileName) {
	return QStringLiteral(
		"https://github.com/molotovgit/allowgram/releases/latest/download/%1")
		.arg(fileName);
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
	} else if (object.value(QStringLiteral("product")).toString()
		!= kAllowgramProduct) {
		SetError(error, QStringLiteral("Release feed is not for Allowgram."));
		return std::nullopt;
	} else if (object.value(QStringLiteral("channel")).toString()
		!= kStableChannel) {
		SetError(error, QStringLiteral("Release feed is not stable."));
		return std::nullopt;
	}

	const auto version = object.value(QStringLiteral("version"));
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
		|| parsedDisplay->sequence != *sequence) {
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
	const auto files = object.value(QStringLiteral("files"));
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
	const auto size = ReadInt(fileObject.value(QStringLiteral("size")), kMaxPayloadSize);
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
	result.asset.url = StableReleaseDownloadUrl(name);
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
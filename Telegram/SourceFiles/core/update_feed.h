/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "core/update_verify.h"

#include <QtCore/QByteArray>
#include <QtCore/QString>

#include <optional>

namespace Core::Updates {

struct StableReleaseAsset {
	QString fileName;
	QString tag;
	QString url;
	quint64 size = 0;
	QByteArray sha256;
	quint32 baseVersion = 0;
	quint32 sequence = 0;
	quint64 packedVersion = 0;
	QString displayVersion;
};

struct StableReleaseFeed {
	bool updateAvailable = false;
	StableReleaseAsset asset;
};

[[nodiscard]] QByteArray StableReleaseFeedSigningInput(
	const QByteArray &signedBytes);

[[nodiscard]] QString StableReleaseFeedUrl();
[[nodiscard]] QString StableReleaseDownloadUrl(
	const QString &tag,
	const QString &fileName);
[[nodiscard]] QString StableReleaseFileName(
	Target target,
	const QString &displayVersion);

[[nodiscard]] std::optional<StableReleaseFeed> ParseStableReleaseFeed(
	const QByteArray &response,
	const QByteArray &platformKey,
	quint64 runningVersion,
	const std::optional<Manifest> &trustedManifest,
	qint64 now,
	QString *error = nullptr);

} // namespace Core::Updates

/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include <QtCore/QByteArray>
#include <QtCore/QString>

#include <optional>
#include <vector>

namespace Core::Updates {

struct StagedUpdateFile {
	QString path;
	quint64 size = 0;
	QByteArray sha256;

	friend bool operator==(
		const StagedUpdateFile &a,
		const StagedUpdateFile &b) = default;
};

struct StagedUpdateManifest {
	quint64 packedVersion = 0;
	QString displayVersion;
	QByteArray packageSha256;
	std::vector<StagedUpdateFile> files;

	friend bool operator==(
		const StagedUpdateManifest &a,
		const StagedUpdateManifest &b) = default;
};

struct StagedUpdateReady {
	quint64 packedVersion = 0;
	QByteArray manifestSha256;
};

[[nodiscard]] QByteArray Sha256Bytes(const QByteArray &data);
[[nodiscard]] QByteArray Sha256Hex(const QByteArray &data);

[[nodiscard]] std::optional<QString> NormalizeUpdatePayloadPath(
	QString relativeName);
[[nodiscard]] bool UpdatePayloadFileAllowed(const QString &relativeName);

[[nodiscard]] QByteArray SerializeStageManifest(
	const StagedUpdateManifest &manifest);
[[nodiscard]] std::optional<StagedUpdateManifest> ParseStageManifest(
	const QByteArray &data,
	QString *error = nullptr);

[[nodiscard]] QString StageManifestPath(const QString &tempDirPath);
[[nodiscard]] QString StagePackagePath(const QString &tempDirPath);

[[nodiscard]] bool WriteStageManifest(
	const QString &tempDirPath,
	const StagedUpdateManifest &manifest,
	QString *error = nullptr);

[[nodiscard]] std::optional<StagedUpdateReady> VerifyStagedUpdate(
	const QString &tempDirPath,
	quint64 runningVersion,
	const QByteArray &expectedManifestSha256 = QByteArray(),
	QString *error = nullptr);

} // namespace Core::Updates
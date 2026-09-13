/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "core/update_mandatory.h"

#include <QtCore/QDir>
#include <QtCore/QFile>
#include <QtCore/QFileInfo>
#include <QtCore/QJsonDocument>
#include <QtCore/QJsonObject>
#include <QtCore/QRegularExpression>
#include <QtCore/QSaveFile>

#include <algorithm>
#include <cmath>
#include <limits>
#include <utility>

namespace Core::Updates {
namespace {

constexpr auto kStateFileName = "mandatory-state.json";
constexpr auto kMaxStateSize = 16 * 1024;
constexpr auto kMaxSequence = quint64(65535);

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

[[nodiscard]] bool IsHexSha256(const QByteArray &value) {
	static const auto Pattern = QRegularExpression(
		QStringLiteral("^[0-9a-fA-F]{64}$"));
	return Pattern.match(QString::fromLatin1(value)).hasMatch();
}

[[nodiscard]] bool IsValidTarget(const MandatoryUpdateTarget &target) {
	if (target.tag.isEmpty()
		|| target.fileName.isEmpty()
		|| !target.size
		|| target.size > kMaxPayloadSize
		|| !target.packedVersion
		|| target.displayVersion.isEmpty()
		|| !IsHexSha256(target.sha256)) {
		return false;
	}
	const auto sequence = target.packedVersion & 0xFFFFFFFFULL;
	return sequence > 0 && sequence <= kMaxSequence;
}

[[nodiscard]] QJsonObject SerializeTarget(
		const MandatoryUpdateTarget &target) {
	return {
		{ "tag", target.tag },
		{ "file", target.fileName },
		{ "sha256", QString::fromLatin1(target.sha256) },
		{ "size", double(target.size) },
		{ "packed_version", QString::number(target.packedVersion) },
		{ "display", target.displayVersion },
	};
}

[[nodiscard]] std::optional<MandatoryUpdateTarget> ParseTarget(
		const QJsonValue &value) {
	if (!value.isObject()) {
		return std::nullopt;
	}
	const auto object = value.toObject();
	const auto packed = object.value(
		QStringLiteral("packed_version")).toString().toULongLong();
	auto result = MandatoryUpdateTarget{
		.tag = object.value(QStringLiteral("tag")).toString(),
		.fileName = object.value(QStringLiteral("file")).toString(),
		.sha256 = object.value(QStringLiteral("sha256")).toString(
			).toLatin1().toLower(),
		.size = ReadInt(
			object.value(QStringLiteral("size")),
			kMaxPayloadSize).value_or(0),
		.packedVersion = packed,
		.displayVersion = object.value(QStringLiteral("display")).toString(),
	};
	return IsValidTarget(result)
		? std::make_optional(std::move(result))
		: std::nullopt;
}

} // namespace

MandatoryUpdateTarget MandatoryTargetFromAsset(const StableReleaseAsset &asset) {
	return {
		.tag = asset.tag,
		.fileName = asset.fileName,
		.sha256 = asset.sha256,
		.size = asset.size,
		.packedVersion = asset.packedVersion,
		.displayVersion = asset.displayVersion,
	};
}

MandatoryUpdateState RegisterMandatoryUpdate(
		std::optional<MandatoryUpdateState> current,
		const MandatoryUpdateTarget &target,
		qint64 now) {
	if (!IsValidTarget(target)) {
		return current.value_or(MandatoryUpdateState());
	}
	if (!current || !current->active || !IsValidTarget(current->target)) {
		return {
			.active = true,
			.firstSeen = now,
			.deadline = now + kMandatoryUpdateGraceSeconds,
			.target = target,
		};
	}
	auto result = *current;
	if (target.packedVersion > result.target.packedVersion) {
		result.target = target;
	}
	return result;
}
MandatoryUpdateState DismissMandatoryUpdatePopup(MandatoryUpdateState state) {
	state.popupDismissed = true;
	return state;
}

MandatoryUpdateState MarkMandatoryUpdateApplyStarted(MandatoryUpdateState state) {
	state.applyStarted = true;
	return state;
}

MandatoryUpdateStatus MandatoryStatus(
		const MandatoryUpdateState &state,
		quint64 runningVersion,
		qint64 now) {
	if (!state.active || state.target.packedVersion <= runningVersion) {
		return MandatoryUpdateStatus::None;
	}
	return (now >= state.deadline)
		? MandatoryUpdateStatus::Expired
		: MandatoryUpdateStatus::Grace;
}

int MandatorySecondsRemaining(
		const MandatoryUpdateState &state,
		qint64 now) {
	return int(std::clamp(state.deadline - now, qint64(0), qint64(65535)));
}

QString MandatoryUpdateStatePath(const QString &workingDir) {
	return QDir(QDir::cleanPath(workingDir)).filePath(
		QStringLiteral("tupdates/%1").arg(QString::fromLatin1(kStateFileName)));
}

std::optional<MandatoryUpdateState> ReadMandatoryUpdateState(
		const QString &workingDir,
		QString *error) {
	QFile file(MandatoryUpdateStatePath(workingDir));
	if (!file.open(QIODevice::ReadOnly)) {
		return std::nullopt;
	}
	const auto content = file.readAll();
	if (content.isEmpty() || content.size() > kMaxStateSize) {
		SetError(error, QStringLiteral("Bad mandatory update state size."));
		return std::nullopt;
	}
	auto parseError = QJsonParseError();
	const auto document = QJsonDocument::fromJson(content, &parseError);
	if (parseError.error != QJsonParseError::NoError || !document.isObject()) {
		SetError(error, QStringLiteral("Bad mandatory update state JSON."));
		return std::nullopt;
	}
	const auto object = document.object();
	const auto format = ReadInt(object.value(QStringLiteral("format")), 1);
	const auto firstSeen = ReadInt(
		object.value(QStringLiteral("first_seen")),
		std::numeric_limits<quint64>::max());
	const auto deadline = ReadInt(
		object.value(QStringLiteral("deadline")),
		std::numeric_limits<quint64>::max());
	const auto target = ParseTarget(object.value(QStringLiteral("target")));
	if (!format || *format != 1 || !firstSeen || !deadline || !target) {
		SetError(error, QStringLiteral("Bad mandatory update state fields."));
		return std::nullopt;
	}
	return MandatoryUpdateState{
		.active = object.value(QStringLiteral("active")).toBool(true),
		.popupDismissed = object.value(
			QStringLiteral("popup_dismissed")).toBool(false),
		.applyStarted = object.value(
			QStringLiteral("apply_started")).toBool(false),
		.firstSeen = qint64(*firstSeen),
		.deadline = qint64(*deadline),
		.target = *target,
	};
}

bool WriteMandatoryUpdateState(
		const QString &workingDir,
		const MandatoryUpdateState &state,
		QString *error) {
	if (!state.active || !IsValidTarget(state.target)) {
		SetError(error, QStringLiteral("Bad mandatory update state."));
		return false;
	}
	const auto path = MandatoryUpdateStatePath(workingDir);
	QDir().mkpath(QFileInfo(path).absolutePath());
	QSaveFile file(path);
	const auto object = QJsonObject{
		{ "format", 1 },
		{ "active", state.active },
		{ "popup_dismissed", state.popupDismissed },
		{ "apply_started", state.applyStarted },
		{ "first_seen", double(state.firstSeen) },
		{ "deadline", double(state.deadline) },
		{ "target", SerializeTarget(state.target) },
	};
	const auto bytes = QJsonDocument(object).toJson(QJsonDocument::Compact);
	if (!file.open(QIODevice::WriteOnly)
		|| file.write(bytes) != bytes.size()
		|| !file.commit()) {
		SetError(error, QStringLiteral("Could not write mandatory update state."));
		return false;
	}
	return true;
}

void ClearMandatoryUpdateState(const QString &workingDir) {
	QFile::remove(MandatoryUpdateStatePath(workingDir));
}

} // namespace Core::Updates

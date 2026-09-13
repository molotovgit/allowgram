/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "core/update_feed.h"

#include <QtCore/QByteArray>
#include <QtCore/QString>

#include <optional>

namespace Core::Updates {

inline constexpr auto kMandatoryUpdateGraceSeconds = qint64(300);

struct MandatoryUpdateTarget {
	QString tag;
	QString fileName;
	QByteArray sha256;
	quint64 size = 0;
	quint64 packedVersion = 0;
	QString displayVersion;

	friend inline bool operator==(
		const MandatoryUpdateTarget &a,
		const MandatoryUpdateTarget &b) = default;
};

struct MandatoryUpdateState {
	bool active = false;
	bool popupDismissed = false;
	bool applyStarted = false;
	qint64 firstSeen = 0;
	qint64 deadline = 0;
	MandatoryUpdateTarget target;

	friend inline bool operator==(
		const MandatoryUpdateState &a,
		const MandatoryUpdateState &b) = default;
};

enum class MandatoryUpdateStatus {
	None,
	Grace,
	Expired,
};

[[nodiscard]] MandatoryUpdateTarget MandatoryTargetFromAsset(
	const StableReleaseAsset &asset);

[[nodiscard]] MandatoryUpdateState RegisterMandatoryUpdate(
	std::optional<MandatoryUpdateState> current,
	const MandatoryUpdateTarget &target,
	qint64 now);

[[nodiscard]] MandatoryUpdateState DismissMandatoryUpdatePopup(
	MandatoryUpdateState state);

[[nodiscard]] MandatoryUpdateState MarkMandatoryUpdateApplyStarted(
	MandatoryUpdateState state);

[[nodiscard]] MandatoryUpdateStatus MandatoryStatus(
	const MandatoryUpdateState &state,
	quint64 runningVersion,
	qint64 now);

[[nodiscard]] int MandatorySecondsRemaining(
	const MandatoryUpdateState &state,
	qint64 now);

[[nodiscard]] QString MandatoryUpdateStatePath(const QString &workingDir);

[[nodiscard]] std::optional<MandatoryUpdateState> ReadMandatoryUpdateState(
	const QString &workingDir,
	QString *error = nullptr);

bool WriteMandatoryUpdateState(
	const QString &workingDir,
	const MandatoryUpdateState &state,
	QString *error = nullptr);

void ClearMandatoryUpdateState(const QString &workingDir);

} // namespace Core::Updates

/*
This file is part of Telegram Desktop.
For license and copyright information see:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "data/data_peer_id.h"

#include <span>

namespace MTP {

[[nodiscard]] bool AllowlistWebViewAllowed(
	UserId bot,
	bool sameAccount,
	std::span<const PeerId> peers,
	const Fn<bool(PeerId)> &allows,
	const Fn<bool(UserId)> &knownBot);

[[nodiscard]] bool AllowlistBotAppMatches(
	UserId bot,
	UserId appBot,
	const QString &requestedName,
	const QString &resolvedName,
	bool sameAccount);

[[nodiscard]] bool AllowlistWebViewLocalUriAllowed(const QString &uri);

} // namespace MTP

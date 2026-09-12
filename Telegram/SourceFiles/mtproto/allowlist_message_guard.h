/*
This file is part of Telegram Desktop.
For license and copyright information see:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "scheme.h"
#include "data/data_peer_id.h"

namespace MTP {

template <typename Predicate>
[[nodiscard]] bool AllowlistMessageAllowed(
		const MTPMessage &message,
		Predicate &&allows) {
	return message.match([](const MTPDmessageEmpty &) {
		return false;
	}, [&](const MTPDmessage &data) {
		return allows(peerFromMTP(data.vpeer_id()));
	}, [&](const MTPDmessageService &data) {
		return allows(peerFromMTP(data.vpeer_id()));
	});
}

} // namespace MTP

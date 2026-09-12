/*
This file is part of Telegram Desktop.
For license and copyright information see:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "scheme.h"
#include "data/data_peer_id.h"
#include "mtproto/details/mtproto_serialized_request.h"

namespace MTP {

[[nodiscard]] bool AllowlistRequestAllowed(
	const details::SerializedRequest &request,
	UserId selfId,
	const Fn<bool(PeerId)> &allows,
	const Fn<bool(UserId)> &knownBot = nullptr);

} // namespace MTP

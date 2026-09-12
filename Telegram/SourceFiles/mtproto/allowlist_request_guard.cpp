/*
This file is part of Telegram Desktop.
For license and copyright information see:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "mtproto/allowlist_request_guard.h"

namespace MTP {
namespace {

[[nodiscard]] PeerId Destination(const MTPInputPeer &peer, UserId selfId) {
	return peer.match([&](const MTPDinputPeerSelf &) -> PeerId {
		return selfId;
	}, [](const MTPDinputPeerUser &data) -> PeerId {
		return UserId(data.vuser_id());
	}, [](const MTPDinputPeerChat &data) -> PeerId {
		return ChatId(data.vchat_id());
	}, [](const MTPDinputPeerChannel &data) -> PeerId {
		return ChannelId(data.vchannel_id());
	}, [](const MTPDinputPeerUserFromMessage &data) -> PeerId {
		return UserId(data.vuser_id());
	}, [](const MTPDinputPeerChannelFromMessage &data) -> PeerId {
		return ChannelId(data.vchannel_id());
	}, [](const auto &) -> PeerId {
		return PeerId();
	});
}

[[nodiscard]] PeerId Destination(const MTPInputUser &user, UserId selfId) {
	return user.match([&](const MTPDinputUserSelf &) -> PeerId {
		return selfId;
	}, [](const MTPDinputUser &data) -> PeerId {
		return UserId(data.vuser_id());
	}, [](const MTPDinputUserFromMessage &data) -> PeerId {
		return UserId(data.vuser_id());
	}, [](const auto &) -> PeerId {
		return PeerId();
	});
}

[[nodiscard]] PeerId Destination(const MTPInputChannel &channel) {
	return channel.match([](const MTPDinputChannel &data) -> PeerId {
		return ChannelId(data.vchannel_id());
	}, [](const MTPDinputChannelFromMessage &data) -> PeerId {
		return ChannelId(data.vchannel_id());
	}, [](const auto &) -> PeerId {
		return PeerId();
	});
}

[[nodiscard]] bool ReplyAllowed(
		const MTPInputReplyTo &reply,
		UserId selfId,
		const Fn<bool(PeerId)> &allows) {
	return reply.match([&](const MTPDinputReplyToMessage &data) {
		const auto monoforumPeer = data.vmonoforum_peer_id();
		return !monoforumPeer
			|| allows(Destination(*monoforumPeer, selfId));
	}, [&](const MTPDinputReplyToMonoForum &data) {
		return allows(Destination(data.vmonoforum_peer_id(), selfId));
	}, [](const MTPDinputReplyToStory &) {
		return true;
	}, [](const MTPDinputReplyToEphemeralMessage &) {
		return true;
	}, [](const auto &) {
		return false;
	});
}

template <typename Request>
[[nodiscard]] bool ValidateRequest(
		const mtpPrime *from,
		const mtpPrime *end) {
	auto request = Request();
	return request.read(from, end) && (from == end);
}

struct PeerRequestLayout {
	bool flags = false;
	uint32 replyFlag = 0;
	bool requiredReply = false;
	uint32 forbiddenFlags = 0;
	bool replyBeforePeer = false;
};

[[nodiscard]] bool MediaAllowed(
		const MTPInputMedia &media,
		UserId selfId,
		const Fn<bool(PeerId)> &allows) {
	return media.match([&](const MTPDinputMediaStory &data) {
		return allows(Destination(data.vpeer(), selfId));
	}, [&](const MTPDinputMediaPaidMedia &data) {
		for (const auto &nested : data.vextended_media().v) {
			if (!MediaAllowed(nested, selfId, allows)) {
				return false;
			}
		}
		return true;
	}, [](const auto &) {
		return true;
	});
}

template <typename Request>
[[nodiscard]] bool ReadPeerAllowed(
		const mtpPrime *from,
		const mtpPrime *end,
		UserId selfId,
		const Fn<bool(PeerId)> &allows,
		PeerRequestLayout layout) {
	if (!ValidateRequest<Request>(from, end)) {
		return false;
	}
	++from;
	auto flags = MTPint();
	if (layout.flags && !flags.read(from, end)) {
		return false;
	}
	if (flags.v & layout.forbiddenFlags) {
		return false;
	}
	const auto hasReply = layout.requiredReply || (flags.v & layout.replyFlag);
	const auto readReply = [&] {
		auto reply = MTPInputReplyTo();
		return reply.read(from, end) && ReplyAllowed(reply, selfId, allows);
	};
	if (hasReply && layout.replyBeforePeer && !readReply()) {
		return false;
	}
	auto peer = MTPInputPeer();
	if (!peer.read(from, end) || !allows(Destination(peer, selfId))) {
		return false;
	}
	if (hasReply && !layout.replyBeforePeer && !readReply()) {
		return false;
	}
	const auto readMedia = [&] {
		auto media = MTPInputMedia();
		return media.read(from, end) && MediaAllowed(media, selfId, allows);
	};
	if constexpr (std::is_same_v<Request, MTPmessages_SendMedia>
		|| std::is_same_v<Request, MTPmessages_UploadMedia>) {
		return readMedia();
	} else if constexpr (std::is_same_v<Request, MTPmessages_EditMessage>) {
		auto id = MTPint();
		auto message = MTPstring();
		return id.read(from, end)
			&& (!(flags.v & (1U << 11)) || message.read(from, end))
			&& (!(flags.v & (1U << 14)) || readMedia());
	} else if constexpr (std::is_same_v<Request, MTPmessages_SendMultiMedia>) {
		auto media = MTPVector<MTPInputSingleMedia>();
		if (!media.read(from, end)) {
			return false;
		}
		for (const auto &single : media.v) {
			if (!MediaAllowed(single.data().vmedia(), selfId, allows)) {
				return false;
			}
		}
	}
	return true;
}

[[nodiscard]] bool ReadForwardAllowed(
		const mtpPrime *from,
		const mtpPrime *end,
		UserId selfId,
		const Fn<bool(PeerId)> &allows) {
	if (!ValidateRequest<MTPmessages_ForwardMessages>(from, end)) {
		return false;
	}
	++from;
	auto flags = MTPint();
	auto source = MTPInputPeer();
	auto ids = MTPVector<MTPint>();
	auto randomIds = MTPVector<MTPlong>();
	auto destination = MTPInputPeer();
	if (!flags.read(from, end)
		|| !source.read(from, end)
		|| !allows(Destination(source, selfId))
		|| !ids.read(from, end)
		|| !randomIds.read(from, end)
		|| !destination.read(from, end)
		|| !allows(Destination(destination, selfId))) {
		return false;
	}
	if (flags.v & (1 << 9)) {
		auto topMessageId = MTPint();
		if (!topMessageId.read(from, end)) {
			return false;
		}
	}
	if (flags.v & (1 << 22)) {
		auto reply = MTPInputReplyTo();
		if (!reply.read(from, end) || !ReplyAllowed(reply, selfId, allows)) {
			return false;
		}
	}
	return true;
}

template <typename Request>
[[nodiscard]] bool ReadBotAllowed(
		const mtpPrime *from,
		const mtpPrime *end,
		UserId selfId,
		const Fn<bool(PeerId)> &allows,
		bool hasFlags,
		bool hasPeer) {
	if (!ValidateRequest<Request>(from, end)) {
		return false;
	}
	++from;
	auto flags = MTPint();
	if (hasFlags && !flags.read(from, end)) {
		return false;
	}
	auto bot = MTPInputUser();
	if (!bot.read(from, end) || !allows(Destination(bot, selfId))) {
		return false;
	}
	if (hasPeer) {
		auto peer = MTPInputPeer();
		if (!peer.read(from, end) || !allows(Destination(peer, selfId))) {
			return false;
		}
	}
	return true;
}

[[nodiscard]] bool BodyAllowed(
		const mtpPrime *from,
		const mtpPrime *end,
		UserId selfId,
		const Fn<bool(PeerId)> &allows,
		int depth) {
	if (from == end || depth > 8) {
		return false;
	}
	const auto type = mtpTypeId(*from);
	switch (type) {
#include "mtproto/allowlist_safe_requests.inc"
	case mtpc_req_pq:
	case mtpc_req_pq_multi:
	case mtpc_req_DH_params:
	case mtpc_set_client_DH_params:
	case mtpc_destroy_auth_key:
	case mtpc_rpc_drop_answer:
	case mtpc_get_future_salts:
	case mtpc_ping:
	case mtpc_ping_delay_disconnect:
	case mtpc_destroy_session:
	case mtpc_http_wait:
	case mtpc_msgs_ack:
	case mtpc_msgs_state_req:
	case mtpc_msg_resend_req:
		return true;
	case mtpc_invokeWithoutUpdates:
		return BodyAllowed(from + 1, end, selfId, allows, depth + 1);
	case mtpc_account_initTakeoutSession:
	case mtpc_invokeWithTakeout:
		return false;
	case mtpc_invokeAfterMsg:
		return (end - from > 3)
			&& BodyAllowed(from + 3, end, selfId, allows, depth + 1);
	case mtpc_invokeWithLayer:
		return (end - from > 2)
			&& BodyAllowed(from + 2, end, selfId, allows, depth + 1);
	case mtpc_messages_forwardMessages:
		return ReadForwardAllowed(from, end, selfId, allows);
	case mtpc_channels_joinChannel: {
		if (!ValidateRequest<MTPchannels_JoinChannel>(from, end)) {
			return false;
		}
		++from;
		auto channel = MTPInputChannel();
		return channel.read(from, end) && allows(Destination(channel));
	}
	case mtpc_messages_startBot:
		return ReadBotAllowed<MTPmessages_StartBot>(
			from,
			end,
			selfId,
			allows,
			false,
			true);
	case mtpc_messages_getInlineBotResults:
		return ReadBotAllowed<MTPmessages_GetInlineBotResults>(
			from,
			end,
			selfId,
			allows,
			true,
			true);
	case mtpc_messages_sendWebViewData:
		return ReadBotAllowed<MTPmessages_SendWebViewData>(
			from,
			end,
			selfId,
			allows,
			false,
			false);
#include "mtproto/allowlist_peer_requests.inc"
	default:
		return false;
	}
}

} // namespace

bool AllowlistRequestAllowed(
		const details::SerializedRequest &request,
		UserId selfId,
		const Fn<bool(PeerId)> &allows) {
	constexpr auto offset = details::SerializedRequest::kMessageBodyPosition;
	if (!request || request->size() <= offset) {
		return false;
	}
	const auto from = request->constData() + offset;
	const auto size = (*request)[
		details::SerializedRequest::kMessageLengthPosition];
	if (size <= 0
		|| (size % sizeof(mtpPrime))
		|| size / sizeof(mtpPrime) != request->size() - offset) {
		return false;
	}
	return BodyAllowed(from, from + size / sizeof(mtpPrime), selfId, allows, 0);
}

} // namespace MTP

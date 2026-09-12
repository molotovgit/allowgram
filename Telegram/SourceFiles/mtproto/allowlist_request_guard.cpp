/*
This file is part of Telegram Desktop.
For license and copyright information see:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "mtproto/allowlist_request_guard.h"
#include "mtproto/allowlist_webview_guard.h"
#include "main/allowlist_policy.h"

#include <QtCore/QRegularExpression>
#include <QtCore/QUrl>
#include <QtCore/QUrlQuery>
#include <algorithm>

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

[[nodiscard]] bool TextAllowed(const MTPstring &text) {
	const auto decoded = QString::fromUtf8(text.v);
	return decoded.toUtf8() == text.v
		&& !Main::Allowlist::ContainsEmoji(decoded.toStdU32String());
}

[[nodiscard]] bool EntitiesAllowed(const MTPVector<MTPMessageEntity> &entities) {
	return std::ranges::none_of(entities.v, [](const MTPMessageEntity &entity) {
		return entity.match([](const MTPDmessageEntityCustomEmoji &) {
			return true;
		}, [](const MTPDmessageEntityPre &data) {
			return !TextAllowed(data.vlanguage());
		}, [](const MTPDmessageEntityTextUrl &data) {
			return !TextAllowed(data.vurl());
		}, [](const auto &) {
			return false;
		});
	});
}

[[nodiscard]] bool ReplyAllowed(
		const MTPInputReplyTo &reply,
		UserId selfId,
		const Fn<bool(PeerId)> &allows) {
	return reply.match([&](const MTPDinputReplyToMessage &data) {
		const auto monoforumPeer = data.vmonoforum_peer_id();
		return (!monoforumPeer || allows(Destination(*monoforumPeer, selfId)))
			&& (!data.vreply_to_peer_id()
				|| allows(Destination(*data.vreply_to_peer_id(), selfId)))
			&& (!data.vquote_text() || TextAllowed(*data.vquote_text()))
			&& (!data.vquote_entities() || EntitiesAllowed(*data.vquote_entities()));
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
		const Fn<bool(PeerId)> &allows,
		AllowlistContentContext *content) {
	return media.match([](const MTPDinputMediaEmpty &) {
		return true;
	}, [&](const MTPDinputMediaUploadedPhoto &data) {
		return content && content->uploadAllowed(data.vfile())
			&& !data.vstickers() && !data.vvideo();
	}, [&](const MTPDinputMediaPhoto &data) {
		return !data.vvideo();
	}, [&](const MTPDinputMediaUploadedDocument &data) {
		return content && content->uploadAllowed(data.vfile())
			&& !data.vstickers() && !data.is_nosound_video()
			&& AllowlistDocumentContentAllowed(qs(data.vmime_type()), data.vattributes().v);
	}, [&](const MTPDinputMediaDocument &data) {
		return content && content->documentAllowed(data.vid())
			&& (!data.vquery() || TextAllowed(*data.vquery()));
	}, [](const MTPDinputMediaGeoPoint &) {
		return true;
	}, [](const MTPDinputMediaGeoLive &) {
		return true;
	}, [](const MTPDinputMediaContact &data) {
		return TextAllowed(data.vphone_number()) && TextAllowed(data.vfirst_name())
			&& TextAllowed(data.vlast_name()) && TextAllowed(data.vvcard());
	}, [](const MTPDinputMediaVenue &data) {
		return TextAllowed(data.vtitle()) && TextAllowed(data.vaddress());
	}, [](const MTPDinputMediaWebPage &) {
		return false;
	}, [&](const MTPDinputMediaPaidMedia &data) {
		for (const auto &nested : data.vextended_media().v) {
			if (!MediaAllowed(nested, selfId, allows, content)) {
				return false;
			}
		}
		return true;
	}, [](const auto &) {
		return false;
	});
}

template <typename Request>
[[nodiscard]] bool ReadPeerAllowed(
		const mtpPrime *from,
		const mtpPrime *end,
		UserId selfId,
		const Fn<bool(PeerId)> &allows,
		AllowlistContentContext *content,
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
		return media.read(from, end) && MediaAllowed(media, selfId, allows, content);
	};
	const auto readEntities = [&] {
		auto entities = MTPVector<MTPMessageEntity>();
		return !(flags.v & (1U << 2))
			&& (!(flags.v & (1U << 3))
				|| (entities.read(from, end) && EntitiesAllowed(entities)));
	};
	if constexpr (std::is_same_v<Request, MTPmessages_SetTyping>) {
		auto action = MTPSendMessageAction();
		if (!action.read(from, end)) {
			return false;
		}
		switch (action.type()) {
		case mtpc_sendMessageTypingAction:
		case mtpc_sendMessageCancelAction:
		case mtpc_sendMessageRecordVideoAction:
		case mtpc_sendMessageUploadVideoAction:
		case mtpc_sendMessageRecordAudioAction:
		case mtpc_sendMessageUploadAudioAction:
		case mtpc_sendMessageUploadPhotoAction:
		case mtpc_sendMessageUploadDocumentAction:
		case mtpc_sendMessageGeoLocationAction:
		case mtpc_sendMessageChooseContactAction:
		case mtpc_sendMessageRecordRoundAction:
		case mtpc_sendMessageUploadRoundAction:
		case mtpc_sendMessageStopDraftAction:
			return true;
		case mtpc_sendMessageTextDraftAction: {
			const auto &text = action.c_sendMessageTextDraftAction().vtext().data();
			return TextAllowed(text.vtext()) && EntitiesAllowed(text.ventities());
		} break;
		default:
			return false;
		}
	} else if constexpr (std::is_same_v<Request, MTPmessages_SendInlineBotResult>
		|| std::is_same_v<Request, MTPmessages_SendQuickReplyMessages>
		|| std::is_same_v<Request, MTPmessages_SendScheduledMessages>
		|| std::is_same_v<Request, MTPmessages_SendPaidReaction>) {
		return false;
	} else if constexpr (std::is_same_v<Request, MTPmessages_SendReaction>) {
		auto id = MTPint();
		auto reactions = MTPVector<MTPReaction>();
		return id.read(from, end) && (!(flags.v & 1U)
			|| (reactions.read(from, end) && reactions.v.isEmpty()));
	} else if constexpr (std::is_same_v<Request, MTPstories_SendReaction>) {
		auto id = MTPint();
		auto reaction = MTPReaction();
		return id.read(from, end) && reaction.read(from, end)
			&& reaction.type() == mtpc_reactionEmpty;
	} else if constexpr (std::is_same_v<Request, MTPmessages_UploadMedia>) {
		return readMedia();
	} else if constexpr (std::is_same_v<Request, MTPmessages_SendMessage>
		|| std::is_same_v<Request, MTPmessages_SendMedia>) {
		if constexpr (std::is_same_v<Request, MTPmessages_SendMessage>) {
			if (!(flags.v & (1U << 1))) {
				return false;
			}
		}
		if (flags.v & ((1U << 18) | (1U << 23))) {
			return false;
		}
		if constexpr (std::is_same_v<Request, MTPmessages_SendMedia>) {
			if (!readMedia()) {
				return false;
			}
		}
		auto message = MTPstring();
		auto randomId = MTPlong();
		return message.read(from, end) && TextAllowed(message)
			&& randomId.read(from, end) && readEntities();
	} else if constexpr (std::is_same_v<Request, MTPmessages_EditMessage>) {
		auto id = MTPint();
		auto message = MTPstring();
		return !(flags.v & (1U << 23))
			&& (!(flags.v & (1U << 11)) || (flags.v & (1U << 1)))
			&& id.read(from, end)
			&& (!(flags.v & (1U << 15))
				|| (content && content->messageAllowed(Destination(peer, selfId), id.v, true)))
			&& (!(flags.v & (1U << 11))
				|| (message.read(from, end) && TextAllowed(message)))
			&& (!(flags.v & (1U << 14)) || readMedia())
			&& readEntities();
	} else if constexpr (std::is_same_v<Request, MTPmessages_SendMultiMedia>) {
		if (flags.v & (1U << 18)) {
			return false;
		}
		auto media = MTPVector<MTPInputSingleMedia>();
		if (!media.read(from, end)) {
			return false;
		}
		for (const auto &single : media.v) {
			if (!MediaAllowed(single.data().vmedia(), selfId, allows, content)
				|| !TextAllowed(single.data().vmessage())
				|| (single.data().ventities()
					&& !EntitiesAllowed(*single.data().ventities()))) {
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
		const Fn<bool(PeerId)> &allows,
		AllowlistContentContext *content) {
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
		|| (flags.v & (1U << 18))
		|| !source.read(from, end)
		|| !allows(Destination(source, selfId))
		|| !ids.read(from, end)
		|| !randomIds.read(from, end)
		|| !destination.read(from, end)
		|| !allows(Destination(destination, selfId))) {
		return false;
	}
	if (!content || ids.v.isEmpty() || ids.v.size() != randomIds.v.size()) {
		return false;
	}
	for (const auto &id : ids.v) {
		if (!content->messageAllowed(Destination(source, selfId), id.v)) {
			return false;
		}
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
[[nodiscard]] bool ReadSavedParentAllowed(
		const mtpPrime *from,
		const mtpPrime *end,
		UserId selfId,
		const Fn<bool(PeerId)> &allows,
		uint32 parentFlag) {
	if (!ValidateRequest<Request>(from, end)) {
		return false;
	}
	++from;
	if (parentFlag) {
		auto flags = MTPint();
		if (!flags.read(from, end) || !(flags.v & parentFlag)) {
			return false;
		}
	}
	auto parent = MTPInputPeer();
	if (!parent.read(from, end)) {
		return false;
	}
	const auto peer = Destination(parent, selfId);
	return peerIsChannel(peer) && allows(peer);
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
	if constexpr (std::is_same_v<Request, MTPmessages_StartBot>) {
		auto randomId = MTPlong();
		auto parameter = MTPstring();
		return randomId.read(from, end) && parameter.read(from, end)
			&& TextAllowed(parameter);
	}
	return true;
}

[[nodiscard]] bool WebViewPeerAllowed(
		const MTPInputPeer &peer,
		UserId selfId,
		const Fn<bool(PeerId)> &allows,
		int depth = 0) {
	if (depth > 8 || !allows(Destination(peer, selfId))) {
		return false;
	}
	return peer.match([&](const MTPDinputPeerUserFromMessage &data) {
		return WebViewPeerAllowed(data.vpeer(), selfId, allows, depth + 1);
	}, [&](const MTPDinputPeerChannelFromMessage &data) {
		return WebViewPeerAllowed(data.vpeer(), selfId, allows, depth + 1);
	}, [](const auto &) {
		return true;
	});
}

[[nodiscard]] bool WebViewBotAllowed(
		const MTPInputUser &bot,
		UserId selfId,
		const Fn<bool(PeerId)> &allows,
		const Fn<bool(UserId)> &knownBot) {
	const auto id = Destination(bot, selfId);
	if (!peerIsUser(id)
		|| !AllowlistWebViewAllowed(
			peerToUser(id), true, {}, allows, knownBot)) {
		return false;
	}
	return bot.match([&](const MTPDinputUserFromMessage &data) {
		return WebViewPeerAllowed(data.vpeer(), selfId, allows);
	}, [](const auto &) {
		return true;
	});
}

[[nodiscard]] bool WebViewReplyAllowed(
		const MTPInputReplyTo &reply,
		UserId selfId,
		const Fn<bool(PeerId)> &allows) {
	const auto peerAllowed = [&](const MTPInputPeer &peer) {
		return WebViewPeerAllowed(peer, selfId, allows);
	};
	return reply.match([&](const MTPDinputReplyToMessage &data) {
		return (!data.vreply_to_peer_id() || peerAllowed(*data.vreply_to_peer_id()))
			&& (!data.vmonoforum_peer_id() || peerAllowed(*data.vmonoforum_peer_id()));
	}, [&](const MTPDinputReplyToStory &data) {
		return peerAllowed(data.vpeer());
	}, [&](const MTPDinputReplyToMonoForum &data) {
		return peerAllowed(data.vmonoforum_peer_id());
	}, [](const auto &) {
		return false;
	});
}

template <typename Request>
[[nodiscard]] bool ReadWebViewAllowed(
		const mtpPrime *from,
		const mtpPrime *end,
		UserId selfId,
		const Fn<bool(PeerId)> &allows,
		const Fn<bool(UserId)> &knownBot) {
	if (!ValidateRequest<Request>(from, end)) {
		return false;
	}
	++from;
	constexpr auto noFlags = std::is_same_v<Request, MTPmessages_GetBotApp>
		|| std::is_same_v<Request, MTPmessages_SendWebViewData>
		|| std::is_same_v<Request, MTPmessages_GetAttachMenuBot>
		|| std::is_same_v<Request, MTPbots_CanSendMessage>
		|| std::is_same_v<Request, MTPbots_AllowSendMessage>;
	constexpr auto hasPeer = std::is_same_v<Request, MTPmessages_RequestWebView>
		|| std::is_same_v<Request, MTPmessages_ProlongWebView>
		|| std::is_same_v<Request, MTPmessages_RequestMainWebView>
		|| std::is_same_v<Request, MTPmessages_RequestAppWebView>;
	auto flags = MTPint();
	if (!noFlags && !flags.read(from, end)) {
		return false;
	}
	if constexpr (hasPeer) {
		auto peer = MTPInputPeer();
		if (!peer.read(from, end) || !WebViewPeerAllowed(peer, selfId, allows)) {
			return false;
		}
	}
	if constexpr (std::is_same_v<Request, MTPmessages_GetBotApp>
		|| std::is_same_v<Request, MTPmessages_RequestAppWebView>) {
		auto app = MTPInputBotApp();
		return app.read(from, end) && app.match(
			[&](const MTPDinputBotAppShortName &data) {
				return !data.vshort_name().v.isEmpty()
					&& WebViewBotAllowed(data.vbot_id(), selfId, allows, knownBot);
			}, [](const auto &) {
				return false;
			});
	} else {
		auto bot = MTPInputUser();
		if (!bot.read(from, end)
			|| !WebViewBotAllowed(bot, selfId, allows, knownBot)) {
			return false;
		}
	}
	if constexpr (std::is_same_v<Request, MTPmessages_RequestWebView>) {
		auto text = MTPstring();
		auto theme = MTPDataJSON();
		if (((flags.v & (1U << 1)) && !text.read(from, end))
			|| ((flags.v & (1U << 3)) && !text.read(from, end))
			|| ((flags.v & (1U << 2)) && !theme.read(from, end))
			|| !text.read(from, end)) {
			return false;
		}
	} else if constexpr (std::is_same_v<Request, MTPmessages_ProlongWebView>) {
		auto queryId = MTPlong();
		if (!queryId.read(from, end) || !queryId.v) {
			return false;
		}
	}
	if constexpr (std::is_same_v<Request, MTPmessages_RequestWebView>
		|| std::is_same_v<Request, MTPmessages_ProlongWebView>) {
		if (flags.v & 1U) {
			auto reply = MTPInputReplyTo();
			if (!reply.read(from, end) || !WebViewReplyAllowed(reply, selfId, allows)) {
				return false;
			}
		}
		if (flags.v & (1U << 13)) {
			auto sendAs = MTPInputPeer();
			if (!sendAs.read(from, end) || !WebViewPeerAllowed(sendAs, selfId, allows)) {
				return false;
			}
		}
	}
	return true;
}

[[nodiscard]] bool BodyAllowed(
		const mtpPrime *from,
		const mtpPrime *end,
		UserId selfId,
		const Fn<bool(PeerId)> &allows,
		const Fn<bool(UserId)> &knownBot,
		AllowlistContentContext *content,
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
		return BodyAllowed(from + 1, end, selfId, allows, knownBot, content, depth + 1);
	case mtpc_account_initTakeoutSession:
	case mtpc_invokeWithTakeout:
		return false;
	case mtpc_invokeAfterMsg:
		return (end - from > 3)
			&& BodyAllowed(from + 3, end, selfId, allows, knownBot, content, depth + 1);
	case mtpc_invokeWithLayer:
		return (end - from > 2)
			&& BodyAllowed(from + 2, end, selfId, allows, knownBot, content, depth + 1);
	case mtpc_messages_forwardMessages:
		return ReadForwardAllowed(from, end, selfId, allows, content);
	case mtpc_account_updateEmojiStatus: {
		if (!ValidateRequest<MTPaccount_UpdateEmojiStatus>(from, end)) {
			return false;
		}
		++from;
		auto status = MTPEmojiStatus();
		return status.read(from, end) && status.type() == mtpc_emojiStatusEmpty;
	}
	case mtpc_upload_saveFilePart:
	case mtpc_upload_saveBigFilePart: {
		const auto big = type == mtpc_upload_saveBigFilePart;
		if (!content || !(big
			? ValidateRequest<MTPupload_SaveBigFilePart>(from, end)
			: ValidateRequest<MTPupload_SaveFilePart>(from, end))) {
			return false;
		}
		++from;
		auto id = MTPlong();
		auto part = MTPint();
		auto total = MTPint();
		auto bytes = MTPbytes();
		return id.read(from, end) && part.read(from, end)
			&& (!big || total.read(from, end)) && bytes.read(from, end)
			&& content->recordUploadPart(id.v, part.v, bytes.v);
	}
	case mtpc_messages_getSavedDialogs:
		return ReadSavedParentAllowed<MTPmessages_GetSavedDialogs>(
			from, end, selfId, allows, 1U << 1);
	case mtpc_messages_getSavedDialogsByID:
		return ReadSavedParentAllowed<MTPmessages_GetSavedDialogsByID>(
			from, end, selfId, allows, 1U << 1);
	case mtpc_messages_getSavedHistory:
		return ReadSavedParentAllowed<MTPmessages_GetSavedHistory>(
			from, end, selfId, allows, 1U);
	case mtpc_messages_deleteSavedHistory:
		return ReadSavedParentAllowed<MTPmessages_DeleteSavedHistory>(
			from, end, selfId, allows, 1U);
	case mtpc_messages_readSavedHistory:
		return ReadSavedParentAllowed<MTPmessages_ReadSavedHistory>(
			from, end, selfId, allows, 0);
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
		return ReadWebViewAllowed<MTPmessages_SendWebViewData>(
			from, end, selfId, allows, knownBot);
	case mtpc_messages_requestWebView:
		return ReadWebViewAllowed<MTPmessages_RequestWebView>(
			from, end, selfId, allows, knownBot);
	case mtpc_messages_requestSimpleWebView:
		return ReadWebViewAllowed<MTPmessages_RequestSimpleWebView>(
			from, end, selfId, allows, knownBot);
	case mtpc_messages_requestMainWebView:
		return ReadWebViewAllowed<MTPmessages_RequestMainWebView>(
			from, end, selfId, allows, knownBot);
	case mtpc_messages_requestAppWebView:
		return ReadWebViewAllowed<MTPmessages_RequestAppWebView>(
			from, end, selfId, allows, knownBot);
	case mtpc_messages_prolongWebView:
		return ReadWebViewAllowed<MTPmessages_ProlongWebView>(
			from, end, selfId, allows, knownBot);
	case mtpc_messages_getBotApp:
		return ReadWebViewAllowed<MTPmessages_GetBotApp>(
			from, end, selfId, allows, knownBot);
	case mtpc_messages_getAttachMenuBot:
		return ReadWebViewAllowed<MTPmessages_GetAttachMenuBot>(
			from, end, selfId, allows, knownBot);
	case mtpc_messages_toggleBotInAttachMenu:
		return ReadWebViewAllowed<MTPmessages_ToggleBotInAttachMenu>(
			from, end, selfId, allows, knownBot);
	case mtpc_bots_canSendMessage:
		return ReadWebViewAllowed<MTPbots_CanSendMessage>(
			from, end, selfId, allows, knownBot);
	case mtpc_bots_allowSendMessage:
		return ReadWebViewAllowed<MTPbots_AllowSendMessage>(
			from, end, selfId, allows, knownBot);
#include "mtproto/allowlist_peer_requests.inc"
	default:
		return false;
	}
}

} // namespace

bool AllowlistDocumentContentAllowed(
		const QString &mime,
		const QVector<MTPDocumentAttribute> &attributes) {
	const auto normalized = mime.toLower();
	if (normalized.isEmpty() || normalized == u"image/gif"_q
		|| normalized == u"application/x-tgsticker"_q
		|| normalized == u"application/x-tgsdice"_q
		|| normalized == u"video/webm"_q) {
		return false;
	}
	for (const auto &attribute : attributes) {
		switch (attribute.type()) {
		case mtpc_documentAttributeAnimated:
		case mtpc_documentAttributeSticker:
		case mtpc_documentAttributeCustomEmoji:
		case mtpc_documentAttributeHasStickers:
			return false;
		case mtpc_documentAttributeFilename: {
			const auto &name = attribute.c_documentAttributeFilename().vfile_name();
			const auto lower = qs(name).toLower();
			if (!TextAllowed(name) || lower.endsWith(u".gif"_q)
				|| lower.endsWith(u".tgs"_q) || lower.endsWith(u".webm"_q)) {
				return false;
			}
		} break;
		case mtpc_documentAttributeImageSize:
		case mtpc_documentAttributeVideo:
			break;
		case mtpc_documentAttributeAudio: {
			const auto &audio = attribute.c_documentAttributeAudio();
			if ((audio.vtitle() && !TextAllowed(*audio.vtitle()))
				|| (audio.vperformer() && !TextAllowed(*audio.vperformer()))) {
				return false;
			}
		} break;
		default:
			return false;
		}
	}
	return true;
}

bool AllowlistUploadPrefixAllowed(const QByteArray &bytes) {
	return !bytes.isEmpty()
		&& !bytes.startsWith("GIF87a") && !bytes.startsWith("GIF89a")
		&& !bytes.startsWith(QByteArray::fromHex("1f8b"))
		&& !bytes.startsWith(QByteArray::fromHex("1a45dfa3"))
		&& !(bytes.startsWith("RIFF") && bytes.mid(8, 4) == "WEBP"
			&& ((bytes.mid(12, 4) == "VP8X" && bytes.size() > 20 && (bytes[20] & 2))
				|| bytes.contains("ANIM")));
}

AllowlistContentContext::AllowlistContentContext(Fn<QByteArray(uint64)> resolveDocument)
: _resolveDocument(std::move(resolveDocument)) {
}

void AllowlistContentContext::recordDocument(
		uint64 id,
		const QString &mime,
		const QVector<MTPDocumentAttribute> &attributes) {
	_documents[id] = AllowlistDocumentContentAllowed(mime, attributes);
}

void AllowlistContentContext::recordMessage(const MTPDmessage &message, bool scheduled) {
	if (message.vid().v <= 0) {
		return;
	}
	const auto key = std::pair(peerFromMTP(message.vpeer_id()),
		scheduled ? -message.vid().v : message.vid().v);
	_messageDocuments.erase(key);
	const auto documentAllowed = [&](const MTPDocument &document) {
		if (document.type() != mtpc_document) {
			return false;
		}
		const auto &data = document.c_document();
		_messageDocuments[key] = data.vid().v;
		recordDocument(data.vid().v, qs(data.vmime_type()), data.vattributes().v);
		return AllowlistDocumentContentAllowed(qs(data.vmime_type()), data.vattributes().v);
	};
	const auto mediaAllowed = [&](const MTPMessageMedia &media) {
		return media.match([](const MTPDmessageMediaEmpty &) {
			return true;
		}, [&](const MTPDmessageMediaPhoto &data) {
			return !data.vvideo();
		}, [&](const MTPDmessageMediaDocument &data) {
			return data.vdocument() && documentAllowed(*data.vdocument())
				&& !data.valt_documents();
		}, [](const auto &) {
			return false;
		});
	};
	_messages[key]
		= TextAllowed(message.vmessage()) && !message.vrich_message()
		&& !message.veffect() && !message.vreply_markup()
		&& (!message.ventities() || EntitiesAllowed(*message.ventities()))
		&& (!message.vmedia() || mediaAllowed(*message.vmedia()));
}

void AllowlistContentContext::forgetMessage(PeerId peer, int id, bool scheduled) {
	if (id > 0) {
		_messages.erase({ peer, scheduled ? -id : id });
		_messageDocuments.erase({ peer, scheduled ? -id : id });
	}
}

bool AllowlistContentContext::documentAllowed(const MTPInputDocument &document) const {
	return document.match([&](const MTPDinputDocument &data) {
		return documentContentAllowed(data.vid().v);
	}, [](const auto &) {
		return false;
	});
}

bool AllowlistContentContext::documentContentAllowed(uint64 id) const {
	const auto found = _documents.find(id);
	return found != _documents.end() && found->second && _resolveDocument
		&& AllowlistUploadPrefixAllowed(_resolveDocument(id));
}

bool AllowlistContentContext::messageAllowed(PeerId peer, int id, bool scheduled) const {
	if (id <= 0) {
		return false;
	}
	const auto found = _messages.find({ peer, scheduled ? -id : id });
	const auto document = _messageDocuments.find({ peer, scheduled ? -id : id });
	return found != _messages.end() && found->second
		&& (document == _messageDocuments.end() || documentContentAllowed(document->second));
}

bool AllowlistContentContext::uploadAllowed(const MTPInputFile &file) const {
	const auto check = [&](const auto &data) {
		const auto found = _uploads.find(data.vid().v);
		if (found == _uploads.end() || !found->second.allowed
			|| !TextAllowed(data.vname())) {
			return false;
		}
		const auto &proof = found->second;
		const auto parts = data.vparts().v;
		return parts > 0 && proof.parts.size() == size_t(parts)
			&& *proof.parts.begin() == 0 && *proof.parts.rbegin() == parts - 1
			&& (parts == 1 || proof.firstPartSize >= 32);
	};
	return file.match([&](const MTPDinputFile &data) {
		return check(data);
	}, [&](const MTPDinputFileBig &data) {
		return check(data);
	}, [](const auto &) {
		return false;
	});
}

bool AllowlistContentContext::recordUploadPart(uint64 id, int part, const QByteArray &bytes) {
	if (!id || part < 0 || part >= 16384 || bytes.isEmpty()) {
		return false;
	}
	auto &proof = _uploads[id];
	if (!part) {
		proof.allowed = AllowlistUploadPrefixAllowed(bytes);
		proof.firstPartSize = int(bytes.size());
	}
	if (!proof.allowed) {
		return false;
	}
	proof.parts.insert(part);
	return true;
}

bool AllowlistRequestAllowed(
		const details::SerializedRequest &request,
		UserId selfId,
		const Fn<bool(PeerId)> &allows,
		const Fn<bool(UserId)> &knownBot,
		AllowlistContentContext *content) {
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
	const auto conversationAllowed = Fn<bool(PeerId)>([&](PeerId peer) {
		return peer && peer != peerFromUser(selfId) && allows(peer);
	});
	return BodyAllowed(
		from,
		from + size / sizeof(mtpPrime),
		selfId,
		conversationAllowed,
		knownBot,
		content,
		0);
}

bool AllowlistWebViewAllowed(
		UserId bot,
		bool sameAccount,
		std::span<const PeerId> peers,
		const Fn<bool(PeerId)> &allows,
		const Fn<bool(UserId)> &knownBot) {
	if (!sameAccount || !bot || !knownBot || !knownBot(bot)
		|| !allows(peerFromUser(bot))) {
		return false;
	}
	for (const auto peer : peers) {
		if (!peer || !allows(peer)) {
			return false;
		}
	}
	return true;
}

bool AllowlistBotAppMatches(
		UserId bot,
		UserId appBot,
		const QString &requestedName,
		const QString &resolvedName,
		bool sameAccount) {
	return sameAccount
		&& bot
		&& bot == appBot
		&& !requestedName.isEmpty()
		&& requestedName == resolvedName;
}

bool AllowlistWebViewLocalUriAllowed(const QString &uri) {
	const auto url = QUrl(uri);
	if (!url.isValid() || url.scheme() != u"tg"_q
		|| url.host() != u"resolve"_q || !url.path().isEmpty()
		|| !url.userInfo().isEmpty() || url.port() != -1
		|| url.hasFragment()) {
		return false;
	}
	const auto query = QUrlQuery(url);
	const auto domain = query.queryItemValue(u"domain"_q);
	if (!QRegularExpression(u"^[A-Za-z][A-Za-z0-9_]{0,63}$"_q)
		.match(domain).hasMatch()) {
		return false;
	}
	auto seen = QStringList();
	for (const auto &[key, value] : query.queryItems()) {
		if (seen.contains(key)
			|| (key != u"domain"_q && key != u"appname"_q
				&& key != u"startapp"_q && key != u"mode"_q
				&& key != u"start"_q && key != u"post"_q
				&& key != u"comment"_q && key != u"thread"_q)) {
			return false;
		}
		seen.push_back(key);
	}
	return true;
}

} // namespace MTP

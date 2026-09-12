/*
This file is part of Telegram Desktop.
For license and copyright information see:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "mtproto/allowlist_request_guard.h"

#include <cstdlib>
#include <iostream>

/*
This executable checks requests before transport encryption and padding.
The real serializer object file also contains unused encryption helpers.
These link shims terminate immediately if any test reaches those helpers,
so the test cannot accidentally validate a path using replacement randomness.
*/
namespace bytes {

void set_random(span) {
	std::abort();
}

} // namespace bytes

namespace base {

void RandomFill(bytes::span) {
	std::abort();
}

} // namespace base

namespace base::assertion {

void log(const char *message, const char *file, int line) {
	std::cerr << file << ':' << line << ": " << message << '\n';
}

} // namespace base::assertion

namespace {

using Request = MTP::details::SerializedRequest;

template <typename ...Parts>
[[nodiscard]] Request Packet(const Parts &...parts) {
	auto body = mtpBuffer();
	(parts.write(body), ...);
	auto result = Request::Prepare(body.size());
	result->append(body);
	return result;
}

[[nodiscard]] Request Text(const MTPInputPeer &peer) {
	return Packet(
		MTP_int(mtpc_messages_sendMessage),
		MTP_int(0),
		peer,
		MTP_string("test"),
		MTP_long(1));
}

} // namespace

int main() {
	const auto user = MTPInputPeer(MTP_inputPeerUser(MTP_long(42), MTP_long(1)));
	const auto blocked = MTPInputPeer(MTP_inputPeerUser(MTP_long(43), MTP_long(1)));
	const auto group = MTPInputPeer(MTP_inputPeerChat(MTP_long(42)));
	const auto channel = MTPInputPeer(MTP_inputPeerChannel(MTP_long(42), MTP_long(1)));
	const auto bot = MTPInputUser(MTP_inputUser(MTP_long(42), MTP_long(1)));
	const auto blockedBot = MTPInputUser(MTP_inputUser(MTP_long(43), MTP_long(1)));
	const auto allows = Fn<bool(PeerId)>([](PeerId id) {
		return id == PeerId(UserId(42));
	});
	const auto allowsNone = Fn<bool(PeerId)>([](PeerId) {
		return false;
	});
	auto failures = 0;
	auto checks = 0;
	const auto check = [&](const char *name, const Request &request, bool expected) {
		++checks;
		const auto actual = MTP::AllowlistRequestAllowed(request, UserId(99), allows);
		if (actual != expected) {
			++failures;
			std::cerr << "FAIL: " << name << '\n';
		}
	};
	check("allowed text", Text(user), true);
	check("blocked text", Text(blocked), false);
	check("type-safe basic group ID", Text(group), false);
	check("type-safe channel ID", Text(channel), false);
	check("self is not implicitly allowed", Text(MTP_inputPeerSelf()), false);
	check("empty peer", Text(MTP_inputPeerEmpty()), false);
	check("channel join requires channel ID", Request::Serialize(
		MTPchannels_JoinChannel(MTP_inputChannel(MTP_long(42), MTP_long(1)))), false);
	check("read before setup", Packet(MTP_int(mtpc_help_getConfig)), true);
	check("unknown method", Packet(MTP_int(0x12345678)), false);
	check("missing body", Request(), false);

	for (const auto &peer : { user, blocked }) {
		const auto expected = peer.c_inputPeerUser().vuser_id().v == 42;
		check("media", Packet(MTP_int(mtpc_messages_sendMedia), MTP_int(0),
			peer, MTPInputMedia(MTP_inputMediaEmpty()), MTP_string("caption"),
			MTP_long(1)), expected);
		check("album", Packet(MTP_int(mtpc_messages_sendMultiMedia), MTP_int(0),
			peer, MTPVector<MTPInputSingleMedia>()), expected);
		check("edit", Packet(MTP_int(mtpc_messages_editMessage), MTP_int(1 << 11),
			peer, MTP_int(1), MTP_string("replacement")), expected);
		check("inline result", Packet(MTP_int(mtpc_messages_sendInlineBotResult),
			MTP_int(0), peer, MTP_long(1), MTP_long(2), MTP_string("result")), expected);
		check("bot callback", Packet(MTP_int(mtpc_messages_getBotCallbackAnswer),
			MTP_int(0), peer, MTP_int(1)), expected);
		check("vote", Packet(MTP_int(mtpc_messages_sendVote), peer, MTP_int(1),
			MTPVector<MTPbytes>()), expected);
		check("reaction", Packet(MTP_int(mtpc_messages_sendReaction), MTP_int(0),
			peer, MTP_int(1)), expected);
		check("scheduled message", Packet(MTP_int(mtpc_messages_sendScheduledMessages),
			peer, MTPVector<MTPint>()), expected);
		check("quick reply", Packet(MTP_int(mtpc_messages_sendQuickReplyMessages),
			peer, MTP_int(1), MTPVector<MTPint>(), MTPVector<MTPlong>()), expected);
		check("forward destination", Packet(MTP_int(mtpc_messages_forwardMessages),
			MTP_int(0), blocked, MTPVector<MTPint>(), MTPVector<MTPlong>(),
			peer), expected);
		check("typing", Packet(MTP_int(mtpc_messages_setTyping), MTP_int(0),
			peer, MTPSendMessageAction(MTP_sendMessageTypingAction())), expected);
		check("story reaction", Packet(MTP_int(mtpc_stories_sendReaction), MTP_int(0),
			peer, MTP_int(1), MTPReaction(MTP_reactionEmpty())), expected);
	}
	check("bot start", Request::Serialize(MTPmessages_StartBot(
		bot, user, MTP_long(1), MTP_string("test"))), true);
	check("inline query allowed bot", Request::Serialize(MTPmessages_GetInlineBotResults(
		MTP_flags(0), bot, user, MTPInputGeoPoint(), MTP_string("test"),
		MTP_string(""))), true);
	check("inline query blocked bot", Request::Serialize(MTPmessages_GetInlineBotResults(
		MTP_flags(0), blockedBot, user, MTPInputGeoPoint(), MTP_string("test"),
		MTP_string(""))), false);
	check("blocked bot in allowed chat", Request::Serialize(MTPmessages_StartBot(
		blockedBot, user, MTP_long(1), MTP_string("test"))), false);
	check("webview data destination", Request::Serialize(MTPmessages_SendWebViewData(
		blockedBot, MTP_long(1), MTP_string("button"), MTP_string("data"))), false);
	check("from-message destination ID", Text(MTP_inputPeerUserFromMessage(
		user, MTP_int(1), MTP_long(43))), false);
	check("from-message context is not destination", Text(MTP_inputPeerUserFromMessage(
		blocked, MTP_int(1), MTP_long(42))), true);
	check("monoforum recipient", Packet(MTP_int(mtpc_messages_sendMessage),
		MTP_int(1), user, MTPInputReplyTo(MTP_inputReplyToMonoForum(blocked)),
		MTP_string("test"), MTP_long(1)), false);
	check("draft reply precedes peer", Packet(MTP_int(mtpc_messages_saveDraft),
		MTP_int(1 << 4), MTPInputReplyTo(MTP_inputReplyToMonoForum(user)), user,
		MTP_string("test")), true);
	check("draft monoforum recipient", Packet(MTP_int(mtpc_messages_saveDraft),
		MTP_int(1 << 4), MTPInputReplyTo(MTP_inputReplyToMonoForum(blocked)), user,
		MTP_string("test")), false);
	check("screenshot notification required reply", Packet(
		MTP_int(mtpc_messages_sendScreenshotNotification), user,
		MTPInputReplyTo(MTP_inputReplyToMonoForum(user)), MTP_long(1)), true);

	for (const auto type : {
		mtpc_messages_requestWebView,
		mtpc_messages_sendWebViewResultMessage,
		mtpc_messages_editInlineBotMessage,
		mtpc_account_updateBusinessGreetingMessage,
		mtpc_account_updateBusinessAwayMessage,
		mtpc_phone_requestCall,
		mtpc_phone_acceptCall,
		mtpc_phone_sendGroupCallMessage,
		mtpc_stories_sendStory,
		mtpc_stories_startLive,
		mtpc_messages_createChat,
		mtpc_messages_importChatInvite,
	}) {
		check("unsupported communication", Packet(MTP_int(type)), false);
	}
	auto truncated = Text(user);
	truncated->removeLast();
	(*truncated)[Request::kMessageLengthPosition] -= sizeof(mtpPrime);
	check("truncated known message", truncated, false);
	auto wrongLength = Text(user);
	(*wrongLength)[Request::kMessageLengthPosition] = 0;
	check("corrupt body length", wrongLength, false);
	const auto wrapped = Packet(MTP_int(mtpc_invokeWithoutUpdates),
		MTP_int(mtpc_messages_sendMessage), MTP_int(0), blocked,
		MTP_string("test"), MTP_long(1));
	check("wrapper cannot bypass destination", wrapped, false);
	++checks;
	if (MTP::AllowlistRequestAllowed(Text(user), UserId(42), allowsNone)) {
		++failures;
		std::cerr << "FAIL: unconfigured or another account denied\n";
	}
	const auto channelAllows = Fn<bool(PeerId)>([](PeerId id) {
		return id == PeerId(ChannelId(42));
	});
	++checks;
	if (!MTP::AllowlistRequestAllowed(
			Request::Serialize(MTPchannels_JoinChannel(
				MTP_inputChannel(MTP_long(42), MTP_long(1)))),
			UserId(99),
			channelAllows)) {
		++failures;
		std::cerr << "FAIL: explicitly allowed channel join\n";
	}
	std::cout << checks << " checks; " << failures << " failures\n";
	return failures ? 1 : 0;
}

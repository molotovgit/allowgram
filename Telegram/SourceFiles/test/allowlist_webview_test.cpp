/*
This file is part of Telegram Desktop.
For license and copyright information see:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "mtproto/allowlist_request_guard.h"
#include "mtproto/allowlist_webview_guard.h"

#include <iostream>

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

} // namespace

void CheckAllowlistWebViews(int &checks, int &failures) {
	constexpr auto first = uint64(8558994389ULL);
	constexpr auto second = uint64(9558994390ULL);
	constexpr auto excluded = uint64(9558994391ULL);
	constexpr auto unknown = uint64(9558994392ULL);
	constexpr auto human = uint64(9558994393ULL);
	auto configured = true;
	auto account = 1;
	const auto allows = Fn<bool(PeerId)>([&](PeerId peer) {
		return configured && (account == 1)
			&& (peer == peerFromUser(UserId(first))
				|| peer == peerFromUser(UserId(second))
				|| peer == peerFromUser(UserId(unknown))
				|| peer == peerFromUser(UserId(human))
				|| peer == PeerId(ChatId(77)));
	});
	const auto knownBot = Fn<bool(UserId)>([&](UserId bot) {
		return account == 1
			&& (bot == UserId(first) || bot == UserId(second) || bot == UserId(excluded));
	});
	const auto check = [&](const char *name, bool actual, bool expected) {
		++checks;
		if (actual != expected) {
			++failures;
			std::cerr << "FAIL: webview " << name << '\n';
		}
	};
	const auto accepted = [&](const Request &request) {
		return MTP::AllowlistRequestAllowed(request, UserId(99), allows, knownBot);
	};
	const auto user = [](uint64 id) {
		return MTPInputUser(MTP_inputUser(MTP_long(id), MTP_long(123)));
	};
	const auto peer = [](uint64 id) {
		return MTPInputPeer(MTP_inputPeerUser(MTP_long(id), MTP_long(123)));
	};
	const auto group = MTPInputPeer(MTP_inputPeerChat(MTP_long(77)));
	const auto blocked = MTPInputPeer(MTP_inputPeerChat(MTP_long(78)));
	const auto app = [&](uint64 id) {
		return MTPInputBotApp(MTP_inputBotAppShortName(user(id), MTP_string("dashboard")));
	};
	const auto main = [&](const MTPInputPeer &context, const MTPInputUser &bot) {
		return Packet(MTP_int(mtpc_messages_requestMainWebView), MTP_int(0),
			context, bot, MTP_string("tdesktop"));
	};
	for (const auto id : { first, second, excluded, unknown, human, uint64(0) }) {
		const auto expected = id == first || id == second;
		const auto bot = user(id);
		const auto requests = std::vector<Request>{
			Packet(MTP_int(mtpc_messages_requestWebView), MTP_int(0),
				group, bot, MTP_string("tdesktop")),
			main(group, bot),
			Packet(MTP_int(mtpc_messages_requestSimpleWebView), MTP_int(0),
				bot, MTP_string("tdesktop")),
			Packet(MTP_int(mtpc_messages_prolongWebView), MTP_int(0),
				group, bot, MTP_long(123)),
			Packet(MTP_int(mtpc_messages_getBotApp), app(id), MTP_long(0)),
			Packet(MTP_int(mtpc_messages_requestAppWebView), MTP_int(0),
				group, app(id), MTP_string("tdesktop")),
			Packet(MTP_int(mtpc_messages_sendWebViewData), bot, MTP_long(1),
				MTP_string("Open"), MTP_string("data")),
			Packet(MTP_int(mtpc_messages_getAttachMenuBot), bot),
			Packet(MTP_int(mtpc_messages_toggleBotInAttachMenu), MTP_int(0),
				bot, MTPBool(MTP_boolTrue())),
			Packet(MTP_int(mtpc_bots_canSendMessage), bot),
			Packet(MTP_int(mtpc_bots_allowSendMessage), bot),
		};
		for (const auto &request : requests) {
			check("lifecycle bot and allowed-group authorization", accepted(request), expected);
			check("missing resolver fails closed", MTP::AllowlistRequestAllowed(
				request, UserId(99), allows), false);
			configured = false;
			check("policy revoked before request", accepted(request), false);
			configured = true;
			account = 2;
			check("other account cannot reuse authorization", accepted(request), false);
			account = 1;
		}
	}
	check("allowed bot in its own chat", accepted(main(peer(first), user(first))), true);
	check("excluded destination", accepted(main(blocked, user(first))), false);
	check("empty bot", accepted(main(group, MTP_inputUserEmpty())), false);
	check("self is not a bot", accepted(main(group, MTP_inputUserSelf())), false);
	check("allowed source access context", accepted(main(group,
		MTP_inputUserFromMessage(group, MTP_int(1), MTP_long(first)))), true);
	check("excluded source access context", accepted(main(group,
		MTP_inputUserFromMessage(blocked, MTP_int(1), MTP_long(first)))), false);
	check("forwarded excluded bot", accepted(main(group,
		MTP_inputUserFromMessage(group, MTP_int(1), MTP_long(excluded)))), false);
	check("excluded peer access context", accepted(main(
		MTP_inputPeerUserFromMessage(blocked, MTP_int(1), MTP_long(first)), user(first))), false);
	for (const auto context : { group, blocked }) {
		for (const auto target : { group, blocked }) {
			const auto expected = context.c_inputPeerChat().vchat_id().v == 77
				&& target.c_inputPeerChat().vchat_id().v == 77;
			check("send-as peer", accepted(Packet(MTP_int(mtpc_messages_requestWebView),
				MTP_int(1U << 13), context, user(first), MTP_string("tdesktop"), target)), expected);
			check("prolong send-as peer", accepted(Packet(MTP_int(mtpc_messages_prolongWebView),
				MTP_int(1U << 13), context, user(first), MTP_long(123), target)), expected);
			check("reply source peer", accepted(Packet(MTP_int(mtpc_messages_requestWebView),
				MTP_int(1), context, user(first), MTP_string("tdesktop"),
				MTP_int(mtpc_inputReplyToMessage), MTP_int(2), MTP_int(1), target)), expected);
			check("story reply peer", accepted(Packet(MTP_int(mtpc_messages_prolongWebView),
				MTP_int(1), context, user(first), MTP_long(123),
				MTPInputReplyTo(MTP_inputReplyToStory(target, MTP_int(1))))), expected);
		}
	}
	check("optional fields retain reply checks", accepted(Packet(
		MTP_int(mtpc_messages_requestWebView), MTP_int(15), group, user(first),
		MTP_string("https://example.test/app"), MTP_string("start"),
		MTPDataJSON(MTP_dataJSON(MTP_string("{}"))), MTP_string("tdesktop"),
		MTPInputReplyTo(MTP_inputReplyToMonoForum(blocked)))), false);
	check("optional fields with allowed reply", accepted(Packet(
		MTP_int(mtpc_messages_requestWebView), MTP_int(15), group, user(first),
		MTP_string("https://example.test/app"), MTP_string("start"),
		MTPDataJSON(MTP_dataJSON(MTP_string("{}"))), MTP_string("tdesktop"),
		MTPInputReplyTo(MTP_inputReplyToMonoForum(group)))), true);
	const auto opaque = MTPInputBotApp(MTP_inputBotAppID(MTP_long(1), MTP_long(2)));
	check("opaque lookup owner is unknown", accepted(Packet(
		MTP_int(mtpc_messages_getBotApp), opaque, MTP_long(0))), false);
	check("opaque launch cannot trust forged cached owner", accepted(Packet(
		MTP_int(mtpc_messages_requestAppWebView), MTP_int(0), group,
		opaque, MTP_string("tdesktop"))), false);
	check("mismatched app owner", accepted(Packet(MTP_int(mtpc_messages_requestAppWebView),
		MTP_int(0), peer(first), app(excluded), MTP_string("tdesktop"))), false);
	check("empty app name", accepted(Packet(MTP_int(mtpc_messages_getBotApp),
		MTP_inputBotAppShortName(user(first), MTP_string()), MTP_long(0))), false);
	const auto peers = std::vector<PeerId>{ PeerId(ChatId(77)) };
	check("live context allowed", MTP::AllowlistWebViewAllowed(
		UserId(first), true, peers, allows, knownBot), true);
	check("cross-account controller or app owner", MTP::AllowlistWebViewAllowed(
		UserId(first), false, peers, allows, knownBot), false);
	check("expired policy at callback or bridge", [&] {
		configured = false;
		return MTP::AllowlistWebViewAllowed(UserId(first), true, peers, allows, knownBot);
	}(), false);
	configured = true;
	check("excluded context on cached activation", MTP::AllowlistWebViewAllowed(
		UserId(first), true, std::vector{ PeerId(ChatId(78)) }, allows, knownBot), false);
	check("server app binding", MTP::AllowlistBotAppMatches(
		UserId(first), UserId(first), u"dashboard"_q, u"dashboard"_q, true), true);
	check("server app name mismatch", MTP::AllowlistBotAppMatches(
		UserId(first), UserId(first), u"dashboard"_q, u"other"_q, true), false);
	check("cached app owner mismatch", MTP::AllowlistBotAppMatches(
		UserId(first), UserId(second), u"dashboard"_q, u"dashboard"_q, true), false);
	check("cached app account mismatch", MTP::AllowlistBotAppMatches(
		UserId(first), UserId(first), u"dashboard"_q, u"dashboard"_q, false), false);
	for (const auto uri : { u"tg://resolve?domain=ClassAAssistant_bot&startapp=x"_q,
		u"tg://resolve?domain=AnotherBot&appname=dashboard"_q }) {
		check("normal app link can resolve its independent owner", MTP::AllowlistWebViewLocalUriAllowed(uri), true);
	}
	for (const auto uri : { u"tg://resolve?domain=Bot&startgroup=admin"_q,
		u"tg://resolve?domain=Bot&admin=all"_q, u"tg://join?invite=secret"_q,
		u"tg://resolve?domain=Bot&domain=ExcludedBot"_q, u"tg://passport?bot_id=1"_q,
		u"tg://resolve?domain=Bot&attach=ExcludedBot"_q, u"ton://transfer/id"_q,
		u"tg://resolve?domain="_q, u"tg://resolve?domain=Bot&text=hidden"_q }) {
		check("unsafe or ambiguous bridge link denied", MTP::AllowlistWebViewLocalUriAllowed(uri), false);
	}
	check("excluded resolved deep-link target", accepted(main(group, user(excluded))), false);
	check("custom bridge remains denied", accepted(Request::Serialize(
		MTPbots_InvokeWebViewCustomMethod(user(first), MTP_string("admin"),
			MTP_dataJSON(MTP_string("{}"))))), false);
	check("opaque join app remains denied", accepted(Packet(
		MTP_int(mtpc_messages_requestChatJoinWebView), MTP_int(0),
		MTP_long(1), MTP_string("tdesktop"))), false);
}

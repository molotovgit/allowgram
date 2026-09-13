/*
This file is part of Telegram Desktop.
For license and copyright information see:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "mtproto/allowlist_request_guard.h"
#include "mtproto/allowlist_message_guard.h"

#include <cstdlib>
#include <iostream>
#include <QtCore/QDir>
#include <QtCore/QFile>
#include <QtCore/QFileInfo>

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
		MTP_int(2 | (0)),
		peer,
		MTP_string("test"),
		MTP_long(1));
}

[[nodiscard]] Request Forward(
		const MTPInputPeer &source,
		const MTPInputPeer &destination) {
	return Packet(
		MTP_int(mtpc_messages_forwardMessages),
		MTP_int(0),
		source,
		MTPVector<MTPint>(MTP_vector<MTPint>({ MTP_int(17) })),
		MTPVector<MTPlong>(MTP_vector<MTPlong>({ MTP_long(1) })),
		destination);
}

[[nodiscard]] MTPMessage IncomingMessage(
		const MTPPeer &conversation,
		const MTPPeer &sender,
		const QString &text = u"incoming content"_q,
		const MTPMessageMedia &media = MTP_messageMediaEmpty(),
		uint64 effect = 0,
		const std::optional<MTPReplyMarkup> &markup = std::nullopt) {
	return MTP_message(
		MTP_flags(MTPDmessage::Flag::f_from_id | MTPDmessage::Flag::f_media
			| (effect ? MTPDmessage::Flag::f_effect : MTPDmessage::Flags())
			| (markup ? MTPDmessage::Flag::f_reply_markup : MTPDmessage::Flags())),
		MTP_int(17),
		sender,
		MTPint(),
		MTPstring(),
		conversation,
		MTPPeer(),
		MTPMessageFwdHeader(),
		MTPlong(),
		MTPlong(),
		MTPPeer(),
		MTPMessageReplyHeader(),
		MTP_int(1700000000),
		MTP_string(text),
		media,
		markup.value_or(MTPReplyMarkup()),
		MTPVector<MTPMessageEntity>(),
		MTPint(),
		MTPint(),
		MTPMessageReplies(),
		MTPint(),
		MTPstring(),
		MTPlong(),
		MTPMessageReactions(),
		MTPVector<MTPRestrictionReason>(),
		MTPint(),
		MTPint(),
		MTP_long(effect),
		MTPFactCheck(),
		MTPint(),
		MTPlong(),
		MTPSuggestedPost(),
		MTPint(),
		MTPstring(),
		MTPRichMessage());
}

[[nodiscard]] MTPMessage IncomingService(
		const MTPPeer &conversation,
		const MTPPeer &sender) {
	return MTP_messageService(
		MTP_flags(MTPDmessageService::Flag::f_from_id),
		MTP_int(18),
		sender,
		conversation,
		MTPPeer(),
		MTPMessageReplyHeader(),
		MTP_int(1700000001),
		MTP_messageActionChatEditTitle(MTP_string("Group renamed")),
		MTPMessageReactions(),
		MTPint());
}

void CheckIncomingMessages(int &checks, int &failures) {
	const auto user = MTPPeer(MTP_peerUser(MTP_long(42)));
	const auto otherUser = MTPPeer(MTP_peerUser(MTP_long(43)));
	const auto group = MTPPeer(MTP_peerChat(MTP_long(42)));
	const auto otherGroup = MTPPeer(MTP_peerChat(MTP_long(43)));
	const auto channel = MTPPeer(MTP_peerChannel(MTP_long(42)));
	const auto otherChannel = MTPPeer(MTP_peerChannel(MTP_long(43)));
	const auto userAllowed = [](PeerId id) {
		return id == PeerId(UserId(42));
	};
	const auto groupAllowed = [](PeerId id) {
		return id == PeerId(ChatId(42)) || id == PeerId(UserId(42));
	};
	const auto channelAllowed = [](PeerId id) {
		return id == PeerId(ChannelId(42)) || id == PeerId(UserId(42));
	};
	const auto unconfigured = [](PeerId) { return false; };
	const auto allAllowed = [](PeerId) { return true; };
	const auto check = [&](
			const char *name,
			const MTPMessage &message,
			const auto &allows,
			bool expected) {
		auto serialized = mtpBuffer();
		message.write(serialized);
		const auto begin = serialized.constData();
		const auto end = begin + serialized.size();
		auto from = begin;
		auto decoded = MTPMessage();
		++checks;
		if (!decoded.read(from, end) || from != end) {
			++failures;
			std::cerr << "FAIL: incoming TL roundtrip: " << name << '\n';
			return;
		}
		++checks;
		if (MTP::AllowlistMessageAllowed(message, allows) != expected
			|| MTP::AllowlistMessageAllowed(decoded, allows) != expected) {
			++failures;
			std::cerr << "FAIL: incoming policy: " << name << '\n';
		}
	};
	check("allowed direct conversation", IncomingMessage(user, otherUser),
		userAllowed, true);
	check("denied direct conversation", IncomingMessage(otherUser, user),
		userAllowed, false);
	check("user ID does not allow same-number group", IncomingMessage(group, user),
		userAllowed, false);
	check("user ID does not allow same-number channel", IncomingMessage(channel, user),
		userAllowed, false);
	check("allowed group retains denied sender", IncomingMessage(group, otherUser),
		groupAllowed, true);
	check("allowed sender does not allow denied group", IncomingMessage(otherGroup, user),
		groupAllowed, false);
	check("allowed channel retains denied sender", IncomingMessage(channel, otherUser),
		channelAllowed, true);
	check("allowed sender does not allow denied channel", IncomingMessage(otherChannel, user),
		channelAllowed, false);
	check("allowed group service message", IncomingService(group, otherUser),
		groupAllowed, true);
	check("denied group service message", IncomingService(otherGroup, user),
		groupAllowed, false);
	check("allowed channel service message", IncomingService(channel, otherUser),
		channelAllowed, true);
	check("denied channel service message", IncomingService(otherChannel, user),
		channelAllowed, false);
	check("message denied before setup", IncomingMessage(user, user),
		unconfigured, false);
	check("service denied before setup", IncomingService(group, user),
		unconfigured, false);
	check("empty message with permitted peer is denied", MTP_messageEmpty(
		MTP_flags(MTPDmessageEmpty::Flag::f_peer_id), MTP_int(17), user),
		allAllowed, false);
	check("empty message without peer is denied", MTP_messageEmpty(
		MTP_flags(0), MTP_int(17), MTPPeer()), allAllowed, false);
}

} // namespace

void CheckAllowlistWebViews(int &checks, int &failures);

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
	CheckIncomingMessages(checks, failures);
	CheckAllowlistWebViews(checks, failures);
	auto documentBytes = std::map<uint64, QByteArray>();
	auto content = MTP::AllowlistContentContext([&](uint64 id) {
		return documentBytes[id];
	});
	for (const auto &source : { MTPPeer(MTP_peerUser(MTP_long(42))),
		MTPPeer(MTP_peerChat(MTP_long(42))), MTPPeer(MTP_peerChannel(MTP_long(42))) }) {
		content.recordMessage(IncomingMessage(source, MTP_peerUser(MTP_long(43))).c_message());
	}
	const auto checkWith = [&](
			const char *name,
			const Request &request,
			bool expected,
			const Fn<bool(PeerId)> &predicate) {
		++checks;
		const auto actual = MTP::AllowlistRequestAllowed(request, UserId(99), predicate, nullptr, &content);
		if (actual != expected) {
			++failures;
			std::cerr << "FAIL: " << name << '\n';
		}
	};
	const auto check = [&](const char *name, const Request &request, bool expected) {
		checkWith(name, request, expected, allows);
	};
	check("allowed text", Text(user), true);
	const auto existingConversation = Fn<bool(PeerId)>([](PeerId peer) {
		return peer == PeerId(ChatId(42)) || peer == PeerId(ChannelId(42));
	});
	const auto createGroup = MTPmessages_CreateChat(MTP_flags(0),
		MTP_vector<MTPInputUser>({ bot }), MTP_string("New group"), MTPint());
	check("valid new group creation denied", Request::Serialize(createGroup), false);
	check("wrapped new group creation denied", Packet(
		MTP_int(mtpc_invokeWithoutUpdates), createGroup), false);
	for (const auto flags : { MTPchannels_CreateChannel::Flag::f_broadcast,
		MTPchannels_CreateChannel::Flag::f_megagroup,
		MTPchannels_CreateChannel::Flag::f_forum }) {
		const auto createChannel = MTPchannels_CreateChannel(MTP_flags(flags),
			MTP_string("New conversation"), MTP_string("Description"),
			MTPInputGeoPoint(), MTPstring(), MTPint());
		check("valid new channel or group creation denied",
			Request::Serialize(createChannel), false);
		check("wrapped new channel creation denied", Packet(
			MTP_int(mtpc_invokeWithoutUpdates), createChannel), false);
	}
	checkWith("existing allowed group text retained", Text(group), true,
		existingConversation);
	checkWith("existing allowed channel text retained", Text(channel), true,
		existingConversation);
	checkWith("joining an existing allowed channel retained", Request::Serialize(
		MTPchannels_JoinChannel(MTP_inputChannel(MTP_long(42), MTP_long(1)))),
		true, existingConversation);
	const auto callProtocol = MTP_phoneCallProtocol(MTP_flags(0),
		MTP_int(65), MTP_int(100),
		MTP_vector<MTPstring>({ MTP_string("synthetic") }));
	const auto phoneCall = MTP_inputPhoneCall(MTP_long(1), MTP_long(1));
	const auto groupCall = MTP_inputGroupCall(MTP_long(1), MTP_long(1));
	check("unbound discard cannot target an arbitrary call", Request::Serialize(
		MTPphone_DiscardCall(MTP_flags(0), phoneCall, MTP_int(0),
			MTP_phoneCallDiscardReasonHangup(), MTP_long(0))), false);
	check("private call config needs an authorized pending call", Request::Serialize(
		MTPphone_GetCallConfig()), false);
	check("DH bootstrap needs an authorized pending call", Request::Serialize(
		MTPmessages_GetDhConfig(MTP_int(0), MTP_int(256))), false);
	check("group call lookup is not private call bootstrap", Request::Serialize(
		MTPphone_GetGroupCall(groupCall, MTP_int(5))), false);
	check("wrapped unbound discard remains denied", Packet(
		MTP_int(mtpc_invokeWithLayer), MTP_int(222),
		MTPphone_DiscardCall(MTP_flags(0), phoneCall, MTP_int(0),
			MTP_phoneCallDiscardReasonHangup(), MTP_long(0))), false);
	for (const auto video : { false, true }) {
		const auto request = MTPphone_RequestCall(MTP_flags(video
			? MTPphone_RequestCall::Flag::f_video : MTPphone_RequestCall::Flags()),
			bot, MTP_int(1), MTP_bytes("synthetic"), callProtocol);
		check("valid voice/video call initiation denied", Request::Serialize(request), false);
		check("wrapped voice/video call initiation denied",
			Packet(MTP_int(mtpc_invokeWithoutUpdates), request), false);
	}
	check("valid incoming call acceptance denied", Request::Serialize(
		MTPphone_AcceptCall(phoneCall, MTP_bytes("synthetic"), callProtocol)), false);
	check("valid call confirmation denied", Request::Serialize(
		MTPphone_ConfirmCall(phoneCall, MTP_bytes("synthetic"), MTP_long(1),
			callProtocol)), false);
	for (const auto &peer : { group, channel }) {
		const auto request = MTPphone_CreateGroupCall(MTP_flags(0), peer,
			MTP_int(1), MTPstring(), MTPint());
		check("valid group/channel call creation denied", Request::Serialize(request), false);
		check("wrapped group/channel call creation denied",
			Packet(MTP_int(mtpc_invokeWithoutUpdates), request), false);
	}
	check("valid group call join denied", Request::Serialize(MTPphone_JoinGroupCall(
		MTP_flags(0), groupCall, user, MTPstring(), MTPint256(), MTPbytes(),
		MTP_dataJSON(MTP_string("{}")))), false);
	check("valid group call presentation denied", Request::Serialize(
		MTPphone_JoinGroupCallPresentation(groupCall, MTP_dataJSON(MTP_string("{}")))), false);
	check("valid conference call creation denied", Request::Serialize(
		MTPphone_CreateConferenceCall(MTP_flags(0), MTP_int(1), MTPint256(),
			MTPbytes(), MTPDataJSON())), false);

	const auto checkCall = [&](const char *name, const Request &request,
			bool expected, MTP::AllowlistCallContext &context, UserId self = UserId(99)) {
		++checks;
		if (MTP::AllowlistRequestAllowed(request, self, allows, nullptr, nullptr, &context) != expected) {
			++failures;
			std::cerr << "FAIL: " << name << '\n';
		}
	};
	const auto checkProof = [&](bool pass, const char *name) {
		++checks;
		if (!pass) {
			++failures;
			std::cerr << "FAIL: " << name << '\n';
		}
	};
	auto eligible = true;
	const auto eligibleUser = Fn<bool(UserId)>([&](UserId peer) {
		return eligible && peer == UserId(42);
	});
	auto calls = MTP::AllowlistCallContext(UserId(99), eligibleUser);
	auto otherSession = MTP::AllowlistCallContext(UserId(99), eligibleUser);
	const auto outgoingToken = calls.begin(UserId(42), true);
	checkProof(outgoingToken != 0, "explicitly eligible user receives outgoing token");
	checkProof(!calls.begin(UserId(43), true), "denied user cannot create call proof");
	checkProof(!calls.begin(UserId(99), true), "self cannot create call proof");
	checkProof(!calls.begin(UserId(), true), "missing user cannot create call proof");
	for (const auto video : { false, true }) {
		const auto makeRequest = [&](const MTPInputUser &peer) {
			return MTPphone_RequestCall(MTP_flags(video
				? MTPphone_RequestCall::Flag::f_video : MTPphone_RequestCall::Flags()),
				peer, MTP_int(1), MTP_bytes(QByteArray(32, 'a')), callProtocol);
		};
		const auto request = makeRequest(bot);
		checkCall("allowed voice/video request reaches serialized boundary", Request::Serialize(request), true, calls);
		checkCall("allowed request inside no-update wrapper", Packet(MTP_int(mtpc_invokeWithoutUpdates), request), true, calls);
		checkCall("allowed request inside layer wrapper", Packet(MTP_int(mtpc_invokeWithLayer), MTP_int(222), request), true, calls);
		checkCall("allowed request inside dependency wrapper", Packet(MTP_int(mtpc_invokeAfterMsg), MTP_long(77), request), true, calls);
		checkCall("pending call is account scoped", Request::Serialize(request), false, calls, UserId(100));
		checkCall("pending call cannot cross session", Request::Serialize(request), false, otherSession);
		for (const auto &target : { MTPInputUser(blockedBot), MTPInputUser(MTP_inputUserSelf()),
			MTPInputUser(MTP_inputUserEmpty()), MTPInputUser(MTP_inputUser(MTP_long(99), MTP_long(1))),
			MTPInputUser(MTP_inputUserFromMessage(user, MTP_int(17), MTP_long(43))) }) {
			checkCall("actual recipient cannot borrow allowed call", Request::Serialize(makeRequest(target)), false, calls);
			checkCall("wrapped actual recipient cannot borrow allowed call", Packet(MTP_int(mtpc_invokeWithoutUpdates), makeRequest(target)), false, calls);
		}
		checkCall("from-message recipient with denied context is denied", Request::Serialize(makeRequest(
			MTP_inputUserFromMessage(blocked, MTP_int(17), MTP_long(42)))), false, calls);
		checkCall("from-message allowed recipient and context", Request::Serialize(makeRequest(
			MTP_inputUserFromMessage(user, MTP_int(17), MTP_long(42)))), true, calls);
	}

	for (const auto size : { 0, 31, 33 }) {
		const auto malformed = MTPphone_RequestCall(MTP_flags(0), bot, MTP_int(1),
			MTP_bytes(QByteArray(size, 'a')), callProtocol);
		checkCall("malformed private call key hash is denied", Request::Serialize(malformed), false, calls);
		checkCall("wrapped malformed private call key hash is denied", Packet(MTP_int(mtpc_invokeWithoutUpdates), malformed), false, calls);
	}
	for (const auto &protocol : {
		MTP_phoneCallProtocol(MTP_flags(0), MTP_int(100), MTP_int(65), MTP_vector<MTPstring>({ MTP_string("synthetic") })),
		MTP_phoneCallProtocol(MTP_flags(0), MTP_int(0), MTP_int(100), MTP_vector<MTPstring>({ MTP_string("synthetic") })),
		MTP_phoneCallProtocol(MTP_flags(0), MTP_int(65), MTP_int(100), MTP_vector<MTPstring>({ MTP_string("") })) }) {
		const auto malformed = MTPphone_RequestCall(MTP_flags(0), bot, MTP_int(1), MTP_bytes(QByteArray(32, 'a')), protocol);
		checkCall("inconsistent range or invalid populated version is denied", Request::Serialize(malformed), false, calls);
	}

	checkCall("protocol range is not arbitrarily restricted", Request::Serialize(MTPphone_RequestCall(
		MTP_flags(0), bot, MTP_int(1), MTP_bytes(QByteArray(32, 'a')),
		MTP_phoneCallProtocol(MTP_flags(0), MTP_int(1), MTP_int(1000),
			MTP_vector<MTPstring>({ MTP_string("future-version") })))), true, calls);

	const auto defaultProtocol = MTP_phoneCallProtocol(MTP_flags(0), MTP_int(65), MTP_int(100),
		MTP_vector<MTPstring>({}));
	checkCall("default protocol fallback remains valid for outgoing request", Request::Serialize(MTPphone_RequestCall(
		MTP_flags(0), bot, MTP_int(1), MTP_bytes(QByteArray(32, 'a')), defaultProtocol)), true, calls);

	checkCall("authorized private bootstrap config", Request::Serialize(MTPphone_GetCallConfig()), true, calls);
	checkCall("authorized private DH bootstrap", Request::Serialize(MTPmessages_GetDhConfig(MTP_int(0), MTP_int(256))), true, calls);
	const auto waiting = [&](uint64 id, uint64 hash, uint64 admin, uint64 participant) {
		return MTPPhoneCall(MTP_phoneCallWaiting(MTP_flags(0), MTP_long(id), MTP_long(hash),
			MTP_int(1700000000), MTP_long(admin), MTP_long(participant), callProtocol, MTPint()));
	};
	checkProof(!calls.bind(outgoingToken, waiting(1, 1, 100, 42)), "foreign account response cannot bind");
	checkProof(!calls.bind(outgoingToken, waiting(1, 1, 99, 43)), "mismatched recipient response cannot bind");
	checkProof(!calls.bind(outgoingToken, waiting(0, 1, 99, 42)), "zero call ID cannot bind");
	checkProof(!calls.bind(outgoingToken, waiting(1, 0, 99, 42)), "zero call hash cannot bind");
	checkProof(calls.bind(outgoingToken, waiting(1, 1, 99, 42)), "validated request response binds exact identity");
	checkProof(!calls.bind(outgoingToken, waiting(2, 2, 99, 42)), "bound proof cannot be rebound");
	const auto confirm = Request::Serialize(MTPphone_ConfirmCall(phoneCall, MTP_bytes("synthetic"), MTP_long(1), callProtocol));
	const auto accept = Request::Serialize(MTPphone_AcceptCall(phoneCall, MTP_bytes("synthetic"), callProtocol));
	const auto received = Request::Serialize(MTPphone_ReceivedCall(phoneCall));
	const auto signaling = Request::Serialize(MTPphone_SendSignalingData(phoneCall, MTP_bytes("synthetic")));
	const auto discard = Request::Serialize(MTPphone_DiscardCall(MTP_flags(0), phoneCall, MTP_int(0), MTP_phoneCallDiscardReasonHangup(), MTP_long(0)));
	checkCall("confirm requires key exchange phase", confirm, false, calls);
	checkProof(calls.exchange(outgoingToken), "validated outgoing call enters exchange");
	checkCall("associated outgoing confirmation passes", confirm, true, calls);
	checkCall("associated confirmation retains default protocol fallback", Request::Serialize(MTPphone_ConfirmCall(
		phoneCall, MTP_bytes("synthetic"), MTP_long(1), defaultProtocol)), true, calls);
	const auto invalidProtocol = MTP_phoneCallProtocol(MTP_flags(0), MTP_int(100), MTP_int(65),
		MTP_vector<MTPstring>({ MTP_string("synthetic") }));
	checkCall("associated confirm rejects inconsistent protocol", Request::Serialize(MTPphone_ConfirmCall(
		phoneCall, MTP_bytes("synthetic"), MTP_long(1), invalidProtocol)), false, calls);
	checkCall("incoming acceptance cannot use outgoing proof", accept, false, calls);
	checkCall("incoming received cannot use outgoing proof", received, false, calls);
	checkCall("signaling before confirmed call is denied", signaling, false, calls);
	checkProof(calls.activate(outgoingToken), "validated outgoing exchange activates");
	checkCall("associated active signaling passes", signaling, true, calls);
	checkCall("associated hangup passes", discard, true, calls);
	for (const auto id : { 0ULL, 1ULL, 2ULL }) {
		for (const auto hash : { 0ULL, 1ULL, 2ULL }) {
			const auto input = MTP_inputPhoneCall(MTP_long(id), MTP_long(hash));
			const auto request = MTPphone_SendSignalingData(input, MTP_bytes("synthetic"));
			checkCall("exact call ID and hash required", Request::Serialize(request), id == 1 && hash == 1, calls);
			checkCall("wrapper preserves exact call identity", Packet(MTP_int(mtpc_invokeWithLayer), MTP_int(222), request), id == 1 && hash == 1, calls);
			checkCall("same identity cannot cross account", Request::Serialize(request), false, calls, UserId(100));
			checkCall("same identity cannot cross session", Request::Serialize(request), false, otherSession);
		}
	}
	checkProof(!calls.validate(outgoingToken, waiting(1, 2, 99, 42)), "update cannot replace access hash");
	checkProof(!calls.validate(outgoingToken, waiting(1, 1, 99, 43)), "update cannot replace peer");
	eligible = false;
	checkCall("revocation denies active signaling immediately", signaling, false, calls);
	checkCall("revocation retains only exact cleanup", discard, true, calls);
	checkCall("revocation denies call config", Request::Serialize(MTPphone_GetCallConfig()), false, calls);
	checkProof(!calls.begin(UserId(42), false), "fresh revoked incoming user receives no proof");
	eligible = true;
	checkCall("reallowing peer cannot revive revoked call", signaling, false, calls);
	calls.forget(outgoingToken);
	checkCall("finished call cannot send cleanup", discard, false, calls);
	const auto nextToken = calls.begin(UserId(42), true);
	checkProof(nextToken && nextToken != outgoingToken, "new call has a new generation");
	checkProof(!calls.bind(nextToken, waiting(1, 1, 99, 42)), "retired call identity cannot be reused");
	checkProof(!calls.bind(outgoingToken, waiting(2, 2, 99, 42)), "stale token cannot bind a new identity");
	eligible = false;
	calls.close(nextToken);
	checkProof(calls.bind(nextToken, waiting(2, 2, 99, 42)), "validated late response can bind only for cleanup");
	checkProof(!calls.authorized(nextToken), "late response cannot revive revoked outgoing call");
	calls.forget(nextToken);
	eligible = true;
	const auto incomingToken = calls.begin(UserId(42), false);
	const auto requested = [&](uint64 id, uint64 hash, uint64 admin, uint64 participant) {
		return MTPPhoneCall(MTP_phoneCallRequested(MTP_flags(0), MTP_long(id), MTP_long(hash),
			MTP_int(1700000000), MTP_long(admin), MTP_long(participant), MTP_bytes(QByteArray(32, 'a')), callProtocol));
	};
	checkProof(!calls.bind(incomingToken, requested(3, 3, 43, 99)), "fresh denied caller cannot bind existing proof");
	checkProof(!calls.bind(incomingToken, requested(3, 3, 42, 100)), "foreign incoming recipient cannot bind");
	checkProof(!calls.bind(incomingToken, MTP_phoneCallRequested(MTP_flags(0), MTP_long(3), MTP_long(3),
		MTP_int(1700000000), MTP_long(42), MTP_long(99), MTP_bytes(QByteArray(32, 'a')), invalidProtocol)),
		"incoming malformed protocol cannot bind permission");
	checkProof(calls.bind(incomingToken, requested(3, 3, 42, 99)), "valid incoming request binds exact peer/account");
	const auto incoming = MTP_inputPhoneCall(MTP_long(3), MTP_long(3));
	checkCall("associated incoming receipt passes", Request::Serialize(MTPphone_ReceivedCall(incoming)), true, calls);
	checkProof(calls.exchange(incomingToken), "incoming acceptance enters exchange");
	checkCall("associated incoming acceptance passes", Request::Serialize(MTPphone_AcceptCall(incoming, MTP_bytes("synthetic"), callProtocol)), true, calls);
	checkCall("associated acceptance retains default protocol fallback", Request::Serialize(MTPphone_AcceptCall(
		incoming, MTP_bytes("synthetic"), defaultProtocol)), true, calls);
	checkCall("associated accept rejects inconsistent protocol", Request::Serialize(MTPphone_AcceptCall(
		incoming, MTP_bytes("synthetic"), invalidProtocol)), false, calls);
	checkCall("outgoing confirmation cannot borrow incoming proof", Request::Serialize(MTPphone_ConfirmCall(incoming, MTP_bytes("synthetic"), MTP_long(1), callProtocol)), false, calls);
	checkProof(calls.activate(incomingToken), "incoming confirmed call activates");
	checkCall("incoming active signaling passes", Request::Serialize(MTPphone_SendSignalingData(incoming, MTP_bytes("synthetic"))), true, calls);
	for (const auto &request : { Request::Serialize(MTPphone_SaveCallDebug(incoming, MTP_dataJSON(MTP_string("{}")))),
		Request::Serialize(MTPphone_SetCallRating(MTP_flags(0), incoming, MTP_int(5), MTP_string("test"))),
		Request::Serialize(MTPphone_GetGroupCall(groupCall, MTP_int(5))),
		Request::Serialize(MTPphone_DiscardCall(MTP_flags(0), incoming, MTP_int(0), MTP_phoneCallDiscardReasonMigrateConferenceCall(MTP_string("synthetic")), MTP_long(0))) }) {
		checkCall("active call cannot permit debug rating group lookup or migration", request, false, calls);
	}
	calls.close(incomingToken);
	checkCall("closing incoming call cannot accept", Request::Serialize(MTPphone_AcceptCall(incoming, MTP_bytes("synthetic"), callProtocol)), false, calls);
	calls.forget(incomingToken);

	auto defaultCalls = MTP::AllowlistCallContext(UserId(99), eligibleUser);
	const auto defaultIncoming = defaultCalls.begin(UserId(42), false);
	checkProof(defaultCalls.bind(defaultIncoming, MTP_phoneCallRequested(MTP_flags(0), MTP_long(4), MTP_long(4),
		MTP_int(1700000000), MTP_long(42), MTP_long(99), MTP_bytes(QByteArray(32, 'a')), defaultProtocol)),
		"incoming request can bind the supported default protocol");
	checkProof(defaultCalls.validate(defaultIncoming, MTP_phoneCall(MTP_flags(0), MTP_long(4), MTP_long(4),
		MTP_int(1700000000), MTP_long(42), MTP_long(99), MTP_bytes("synthetic"), MTP_long(1), defaultProtocol,
		MTP_vector<MTPPhoneConnection>({}), MTP_int(1700000000), MTPDataJSON())),
		"full call envelope retains default protocol fallback");
	const auto defaultOutgoing = defaultCalls.begin(UserId(42), true);
	checkProof(defaultCalls.bind(defaultOutgoing, MTP_phoneCallWaiting(MTP_flags(0), MTP_long(5), MTP_long(5),
		MTP_int(1700000000), MTP_long(99), MTP_long(42), defaultProtocol, MTPint())),
		"outgoing response can bind the supported default protocol");


	auto emojiData = QFile(QFileInfo(QString::fromUtf8(__FILE__)).dir()
		.absoluteFilePath(u"../../lib_ui/emoji.txt"_q));
	if (!emojiData.open(QIODevice::ReadOnly)) {
		std::cerr << "Cannot read pinned Telegram emoji test data" << std::endl;
		return 1;
	}
	const auto sequences = QString::fromUtf8(emojiData.readAll()).split(QChar(34));
	auto sequenceCount = 0;
	for (auto index = 1; index < sequences.size(); index += 2) {
		++sequenceCount;
		check("complete Telegram emoji sequence denied", Packet(MTP_int(mtpc_messages_sendMessage),
			MTP_int(2 | (0)), user, MTP_string(sequences[index]), MTP_long(1)), false);
	}
	if (sequenceCount < 4800) {
		std::cerr << "Incomplete Telegram emoji sequence coverage" << std::endl;
		return 1;
	}
	for (const auto &value : {
		u"\U0001F600"_q, u"\u2764\uFE0F"_q,
		u"\U0001F468\u200D\U0001F469\u200D\U0001F467"_q,
		u"\U0001F1FA\U0001F1FF"_q, u"\U0001F44D\U0001F3FD"_q,
		u"1\uFE0F\u20E3"_q, u"#\u20E3"_q, u"*\uFE0F\u20E3"_q,
		u"\U0001FAE9"_q, u"\u263A\uFE0E"_q }) {
		check("Unicode emoji send denied", Packet(MTP_int(mtpc_messages_sendMessage),
			MTP_int(2 | (0)), user, MTP_string(value), MTP_long(1)), false);
		check("Unicode emoji scheduled send denied", Packet(MTP_int(mtpc_messages_sendMessage),
			MTP_int(2 | (1 << 10)), user, MTP_string(value), MTP_long(1), MTP_int(1900000000)), false);
		check("Unicode emoji caption denied", Packet(MTP_int(mtpc_messages_sendMedia),
			MTP_int(0), user, MTPInputMedia(MTP_inputMediaEmpty()), MTP_string(value), MTP_long(1)), false);
		check("Unicode emoji edit denied", Packet(MTP_int(mtpc_messages_editMessage),
			MTP_int(2 | (1 << 11)), user, MTP_int(1), MTP_string(value)), false);
		check("Unicode emoji album caption denied", Packet(MTP_int(mtpc_messages_sendMultiMedia),
			MTP_int(0), user, MTPVector<MTPInputSingleMedia>(MTP_vector<MTPInputSingleMedia>({
				MTP_inputSingleMedia(MTP_flags(0), MTP_inputMediaEmpty(), MTP_long(1),
					MTP_string(value), MTPVector<MTPMessageEntity>()) }))), false);
	}
	for (const auto &value : { u"0123456789 # * ! ?.,:; -1001234567890"_q,
		u"O'zbekiston \u040E\u0437\u0431\u0435\u043A \u4F60\u597D \u65E5\u672C\u8A9E"_q }) {
		check("ordinary multilingual text permitted", Packet(MTP_int(mtpc_messages_sendMessage),
			MTP_int(2 | (0)), user, MTP_string(value), MTP_long(1)), true);
	}
	const auto customEntities = MTPVector<MTPMessageEntity>(MTP_vector<MTPMessageEntity>({
		MTP_messageEntityCustomEmoji(MTP_int(0), MTP_int(1), MTP_long(123)) }));
	check("custom emoji entity send denied", Packet(MTP_int(mtpc_messages_sendMessage),
		MTP_int(2 | (1 << 3)), user, MTP_string("x"), MTP_long(1), customEntities), false);
	check("custom emoji entity edit denied", Packet(MTP_int(mtpc_messages_editMessage),
		MTP_int(2 | ((1 << 11) | (1 << 3))), user, MTP_int(1), MTP_string("x"), customEntities), false);
	check("custom emoji entity caption denied", Packet(MTP_int(mtpc_messages_sendMedia),
		MTP_int(1 << 3), user, MTPInputMedia(MTP_inputMediaEmpty()), MTP_string("x"),
		MTP_long(1), customEntities), false);
	check("blocked text", Text(blocked), false);
	for (const auto &data : { u"python"_q, u"https://example.org"_q, u"\U0001F600"_q }) {
		for (const auto &entity : {
			MTPMessageEntity(MTP_messageEntityPre(MTP_int(0), MTP_int(1), MTP_string(data))),
			MTPMessageEntity(MTP_messageEntityTextUrl(MTP_int(0), MTP_int(1), MTP_string(data))) }) {
			check("entity-owned text uses content classifier", Packet(MTP_int(mtpc_messages_sendMessage),
				MTP_int(2 | (1 << 3)), user, MTP_string("x"), MTP_long(1),
				MTPVector<MTPMessageEntity>(MTP_vector<MTPMessageEntity>({ entity }))), data != u"\U0001F600"_q);
		}
	}
	check("emoji interaction action denied", Packet(MTP_int(mtpc_messages_setTyping), MTP_int(0),
		user, MTPSendMessageAction(MTP_sendMessageEmojiInteraction(MTP_string(u"\U0001F600"_q),
			MTP_int(17), MTP_dataJSON(MTP_string("{}"))))), false);
	check("emoji seen interaction denied", Packet(MTP_int(mtpc_messages_setTyping), MTP_int(0),
		user, MTPSendMessageAction(MTP_sendMessageEmojiInteractionSeen(MTP_string(u"\U0001F600"_q)))), false);
	for (const auto &text : { u"plain streamed text"_q, u"\U0001F600"_q }) {
		check("streamed draft content classified", Packet(MTP_int(mtpc_messages_setTyping), MTP_int(0),
			user, MTPSendMessageAction(MTP_sendMessageTextDraftAction(MTP_flags(0), MTP_long(1),
				MTP_textWithEntities(MTP_string(text), MTPVector<MTPMessageEntity>())))), text.startsWith(u"plain"_q));
	}
	check("dice constructor denied", Packet(MTP_int(mtpc_messages_sendMedia),
		MTP_int(0), user, MTPInputMedia(MTP_inputMediaDice(MTP_string("dice"))),
		MTP_string(""), MTP_long(1)), false);
	check("emoji reaction denied", Packet(MTP_int(mtpc_messages_sendReaction),
		MTP_int(1), user, MTP_int(1), MTPVector<MTPReaction>(MTP_vector<MTPReaction>({
			MTP_reactionEmoji(MTP_string(u"\U0001F44D"_q)) }))), false);
	check("custom emoji reaction denied", Packet(MTP_int(mtpc_messages_sendReaction),
		MTP_int(1), user, MTP_int(1), MTPVector<MTPReaction>(MTP_vector<MTPReaction>({
			MTP_reactionCustomEmoji(MTP_long(1)) }))), false);
	check("GIF bytes cannot upload without evidence", Request::Serialize(
		MTPupload_SaveFilePart(MTP_long(10), MTP_int(0), MTP_bytes("GIF89a"))), false);
	check("unknown cached document denied", Packet(MTP_int(mtpc_messages_sendMedia),
		MTP_int(0), user, MTPInputMedia(MTP_inputMediaDocument(MTP_flags(0),
			MTP_inputDocument(MTP_long(123), MTP_long(1), MTP_bytes("reference")),
			MTPInputPhoto(), MTPint(), MTPint(), MTPstring())),
		MTP_string("caption"), MTP_long(1)), false);
	check("external document cannot be classified", Packet(MTP_int(mtpc_messages_sendMedia),
		MTP_int(0), user, MTPInputMedia(MTP_inputMediaDocumentExternal(MTP_flags(0),
			MTP_string("https://example.org/animation"), MTPint(), MTPInputPhoto(), MTPint())),
		MTP_string("caption"), MTP_long(1)), false);
	check("type-safe basic group ID", Text(group), false);
	const auto documentReference = MTPInputDocument(MTP_inputDocument(
		MTP_long(123), MTP_long(1), MTP_bytes("reference")));
	const auto documentMedia = MTPInputMedia(MTP_inputMediaDocument(MTP_flags(0),
		documentReference, MTPInputPhoto(), MTPint(), MTPint(), MTPstring()));
	const auto documentSend = Packet(MTP_int(mtpc_messages_sendMedia), MTP_int(0),
		user, documentMedia, MTP_string("plain caption"), MTP_long(1));
	content.recordDocument(123, u"application/pdf"_q, { MTP_documentAttributeFilename(MTP_string("report.pdf")) });
	check("cached metadata alone is not content evidence", documentSend, false);
	documentBytes[123] = "%PDF-1.7 ordinary";
	check("known ordinary document permitted", documentSend, true);
	for (const auto &query : { u"ordinary query"_q, u"query \U0001F600"_q }) {
		check("referenced document query uses text policy", Packet(MTP_int(mtpc_messages_sendMedia),
			MTP_int(0), user, MTPInputMedia(MTP_inputMediaDocument(MTP_flags(MTPDinputMediaDocument::Flag::f_query),
				documentReference, MTPInputPhoto(), MTPint(), MTPint(), MTP_string(query))),
			MTP_string("caption"), MTP_long(1)), query.startsWith(u"ordinary"_q));
	}
	documentBytes[123] = "GIF89a renamed report.pdf";
	check("cached renamed GIF bytes override benign metadata", documentSend, false);
	documentBytes[123] = "%PDF-1.7 ordinary";
	content.recordDocument(123, u"image/gif"_q, { MTP_documentAttributeFilename(MTP_string("renamed.bin")) });
	check("cached GIF metadata denied", documentSend, false);
	content.recordDocument(123, u"video/mp4"_q, { MTP_documentAttributeAnimated() });
	check("cached GIF animation attribute denied", documentSend, false);
	content.recordDocument(123, u"image/webp"_q, { MTP_documentAttributeSticker(MTP_flags(0),
		MTP_string("sticker"), MTP_inputStickerSetEmpty(), MTPMaskCoords()) });
	check("cached static sticker denied", documentSend, false);
	content.recordDocument(123, u"video/mp4"_q, { MTP_documentAttributeVideo(MTP_flags(0),
		MTP_double(1.), MTP_int(32), MTP_int(32), MTPint(), MTPdouble(), MTPstring()) });
	check("ordinary nonanimated video permitted", documentSend, true);
	const auto audio = [&](const QString &title, const QString &performer) {
		content.recordDocument(123, u"audio/mpeg"_q, { MTP_documentAttributeAudio(
			MTP_flags(MTPDdocumentAttributeAudio::Flag::f_title | MTPDdocumentAttributeAudio::Flag::f_performer),
			MTP_int(1), MTP_string(title), MTP_string(performer), MTPbytes()) });
	};
	audio(u"Ordinary title"_q, u"Performer"_q);
	check("ordinary audio metadata permitted", documentSend, true);
	audio(u"title \U0001F600"_q, u"Performer"_q);
	check("audio title emoji denied", documentSend, false);
	audio(u"Ordinary title"_q, u"performer \U0001F600"_q);
	check("audio performer emoji denied", documentSend, false);
	check("emoji status addition denied", Request::Serialize(MTPaccount_UpdateEmojiStatus(
		MTP_emojiStatus(MTP_flags(0), MTP_long(123), MTPint()))), false);
	check("emoji status removal retained", Request::Serialize(MTPaccount_UpdateEmojiStatus(
		MTP_emojiStatusEmpty())), true);
	check("ordinary document upload part permitted", Request::Serialize(
		MTPupload_SaveFilePart(MTP_long(11), MTP_int(0), MTP_bytes("%PDF-1.7 plain document"))), true);
	const auto upload = MTPInputFile(MTP_inputFile(MTP_long(11), MTP_int(1),
		MTP_string("report.pdf"), MTP_string("checksum")));
	const auto uploadedDocument = [&](const QString &mime, const QVector<MTPDocumentAttribute> &attributes) {
		return Packet(MTP_int(mtpc_messages_sendMedia), MTP_int(0), user,
			MTPInputMedia(MTP_inputMediaUploadedDocument(MTP_flags(0), upload, MTPInputFile(),
				MTP_string(mime), MTP_vector<MTPDocumentAttribute>(attributes),
				MTPVector<MTPInputDocument>(), MTPInputPhoto(), MTPint(), MTPint())),
			MTP_string("caption"), MTP_long(1));
	};
	check("ordinary uploaded PDF preserved", uploadedDocument(u"application/pdf"_q, {}), true);
	check("uploaded GIF MIME denied", uploadedDocument(u"image/gif"_q, {}), false);
	check("uploaded animation attribute denied", uploadedDocument(u"video/mp4"_q,
		{ MTP_documentAttributeAnimated() }), false);
	check("uploaded custom emoji attribute denied", uploadedDocument(u"image/webp"_q,
		{ MTP_documentAttributeCustomEmoji(MTP_flags(0), MTP_string("x"), MTP_inputStickerSetEmpty()) }), false);
	check("renamed compressed TGS bytes denied", Request::Serialize(MTPupload_SaveFilePart(
		MTP_long(12), MTP_int(0), MTP_bytes(QByteArray::fromHex("1f8b080000000000")))), false);
	check("renamed WebM sticker bytes denied", Request::Serialize(MTPupload_SaveFilePart(
		MTP_long(13), MTP_int(0), MTP_bytes(QByteArray::fromHex("1a45dfa300000000")))), false);
	content.recordMessage(IncomingMessage(MTP_peerUser(MTP_long(42)), MTP_peerUser(MTP_long(43)),
		u"edited \U0001F600"_q).c_message());
	check("cached forward rechecks edited emoji", Forward(user, user), false);
	content.recordMessage(IncomingMessage(MTP_peerUser(MTP_long(42)), MTP_peerUser(MTP_long(43)),
		u"scheduled \U0001F600"_q).c_message(), true);
	check("reschedule unsafe cached message denied", Packet(MTP_int(mtpc_messages_editMessage),
		MTP_int(1 << 15), user, MTP_int(17), MTP_int(1900000000)), false);
	content.recordMessage(IncomingMessage(MTP_peerUser(MTP_long(42)), MTP_peerUser(MTP_long(43))).c_message());
	check("cached plain forward preserved", Forward(user, user), true);
	content.recordMessage(IncomingMessage(MTP_peerUser(MTP_long(42)), MTP_peerUser(MTP_long(43)),
		u"plain body"_q, MTP_messageMediaEmpty(), 123).c_message());
	check("cached forward with effect denied", Forward(user, user), false);
	content.recordMessage(IncomingMessage(MTP_peerUser(MTP_long(42)), MTP_peerUser(MTP_long(43)),
		u"plain body"_q, MTP_messageMediaEmpty(), 0,
		MTP_replyInlineMarkup(MTP_flags(0), MTP_vector<MTPKeyboardInlineButtonRow>({}))).c_message());
	check("cached forward with opaque reply markup denied", Forward(user, user), false);
	content.recordMessage(IncomingMessage(MTP_peerUser(MTP_long(42)), MTP_peerUser(MTP_long(43))).c_message());
	check("ordinary cache does not authorize scheduled ID", Packet(MTP_int(mtpc_messages_editMessage),
		MTP_int(1 << 15), user, MTP_int(17), MTP_int(1900000000)), false);
	content.recordMessage(IncomingMessage(MTP_peerUser(MTP_long(42)), MTP_peerUser(MTP_long(43))).c_message(), true);
	check("reschedule ordinary cached message preserved", Packet(MTP_int(mtpc_messages_editMessage),
		MTP_int(1 << 15), user, MTP_int(17), MTP_int(1900000000)), true);
	check("reschedule unknown content denied", Packet(MTP_int(mtpc_messages_editMessage),
		MTP_int(1 << 15), user, MTP_int(8181), MTP_int(1900000000)), false);
	const auto photoMedia = MTPInputMedia(MTP_inputMediaPhoto(MTP_flags(0),
		MTP_inputPhoto(MTP_long(124), MTP_long(1), MTP_bytes("reference")), MTPint(), MTPInputDocument()));
	const auto photoSend = [&](const QString &caption) {
		return Packet(MTP_int(mtpc_messages_sendMedia), MTP_int(0), user,
			photoMedia, MTP_string(caption), MTP_long(1));
	};
	check("valid ordinary photo caption permitted", photoSend(u"plain caption"_q), true);
	check("same photo with emoji caption denied", photoSend(u"caption \U0001F600"_q), false);
	check("same photo with custom entity denied", Packet(MTP_int(mtpc_messages_sendMedia),
		MTP_int(1 << 3), user, photoMedia, MTP_string("x"), MTP_long(1), customEntities), false);
	const auto photoSingle = [&](const QString &caption) {
		return MTP_inputSingleMedia(MTP_flags(0), photoMedia, MTP_long(1),
			MTP_string(caption), MTPVector<MTPMessageEntity>());
	};
	const auto album = [&](const QString &lastCaption) {
		return Packet(MTP_int(mtpc_messages_sendMultiMedia), MTP_int(0), user,
			MTPVector<MTPInputSingleMedia>(MTP_vector<MTPInputSingleMedia>({
				photoSingle(u"first"_q), photoSingle(lastCaption) })));
	};
	check("ordinary photo album permitted", album(u"second"_q), true);
	check("mixed album with emoji caption denied", album(u"\U0001F600"_q), false);
	check("embedded NUL does not hide emoji", Packet(MTP_int(mtpc_messages_sendMessage),
		MTP_int(2 | (2)), user, MTP_bytes(QByteArray::fromHex("6f6b00f09f9880")), MTP_long(1)), false);
	check("malformed UTF8 rejected", Packet(MTP_int(mtpc_messages_sendMessage),
		MTP_int(2 | (2)), user, MTP_bytes(QByteArray::fromHex("f0808080")), MTP_long(1)), false);
	check("URL with automatic preview rejected", Packet(MTP_int(mtpc_messages_sendMessage),
		MTP_int(0), user, MTP_string("https://example.org/image"), MTP_long(1)), false);
	check("plain URL with preview disabled permitted", Packet(MTP_int(mtpc_messages_sendMessage),
		MTP_int(2 | (2)), user, MTP_string("https://example.org/image"), MTP_long(1)), true);
	check("explicit opaque webpage rejected", Packet(MTP_int(mtpc_messages_sendMedia), MTP_int(0),
		user, MTPInputMedia(MTP_inputMediaWebPage(MTP_flags(0), MTP_string("https://example.org/image"))),
		MTP_string("caption"), MTP_long(1)), false);
	check("URL edit with automatic preview rejected", Packet(MTP_int(mtpc_messages_editMessage),
		MTP_int(1 << 11), user, MTP_int(17), MTP_string("https://example.org/image")), false);
	check("text effect rejected", Packet(MTP_int(mtpc_messages_sendMessage), MTP_int(2 | (2 | (1 << 18))),
		user, MTP_string("plain"), MTP_long(1), MTP_long(321)), false);
	check("photo effect rejected", Packet(MTP_int(mtpc_messages_sendMedia), MTP_int(1 << 18),
		user, photoMedia, MTP_string("plain"), MTP_long(1), MTP_long(321)), false);
	check("album effect rejected", Packet(MTP_int(mtpc_messages_sendMultiMedia), MTP_int(1 << 18),
		user, MTPVector<MTPInputSingleMedia>(MTP_vector<MTPInputSingleMedia>({ photoSingle(u"plain"_q) })),
		MTP_long(321)), false);
	check("forward effect rejected", Packet(MTP_int(mtpc_messages_forwardMessages), MTP_int(1 << 18),
		user, MTPVector<MTPint>(MTP_vector<MTPint>({ MTP_int(17) })),
		MTPVector<MTPlong>(MTP_vector<MTPlong>({ MTP_long(1) })), user, MTP_long(321)), false);
	if (MTP::AllowlistRequestAllowed(Forward(user, user), UserId(99), allows)) {
		std::cerr << "Forward accepted without content context" << std::endl;
		++failures;
	}
	++checks;
	check("type-safe channel ID", Text(channel), false);
	check("self is not implicitly allowed", Text(MTP_inputPeerSelf()), false);
	const auto includesSelf = Fn<bool(PeerId)>([](PeerId) { return true; });
	const auto explicitSelf = MTPInputPeer(MTP_inputPeerUser(MTP_long(99), MTP_long(1)));
	for (const auto &self : { MTPInputPeer(MTP_inputPeerSelf()), explicitSelf,
		MTPInputPeer(MTP_inputPeerUserFromMessage(user, MTP_int(17), MTP_long(99))) }) {
		checkWith("Saved Messages send denied even when allowlisted", Text(self), false, includesSelf);
		checkWith("Saved Messages forward destination denied", Forward(user, self), false, includesSelf);
		checkWith("Saved Messages forward source denied", Forward(self, user), false, includesSelf);
		checkWith("Saved Messages history read denied", Packet(
			MTP_int(mtpc_messages_getHistory), self, MTP_int(0), MTP_int(0),
			MTP_int(0), MTP_int(20), MTP_int(0), MTP_int(0), MTP_long(0)), false, includesSelf);
	}
	checkWith("ordinary history remains readable", Packet(
		MTP_int(mtpc_messages_getHistory), user, MTP_int(0), MTP_int(0),
		MTP_int(0), MTP_int(20), MTP_int(0), MTP_int(0), MTP_long(0)), true, includesSelf);
	checkWith("get-me metadata remains available", Request::Serialize(
		MTPusers_GetUsers(MTP_vector<MTPInputUser>({ MTP_inputUserSelf() }))), true, includesSelf);
	checkWith("Saved dialogs denied", Packet(MTP_int(mtpc_messages_getSavedDialogs),
		MTP_int(0), MTP_int(0), MTP_int(0), MTPInputPeer(MTP_inputPeerEmpty()),
		MTP_int(20), MTP_long(0)), false, includesSelf);
	checkWith("Saved pinned dialogs denied", Packet(MTP_int(mtpc_messages_getPinnedSavedDialogs)), false, includesSelf);
	checkWith("Saved tags denied", Packet(MTP_int(mtpc_messages_getSavedReactionTags),
		MTP_int(0), MTP_long(0)), false, includesSelf);
	checkWith("Saved dialogs by ID denied", Packet(MTP_int(mtpc_messages_getSavedDialogsByID),
		MTP_int(0), MTPVector<MTPInputPeer>(MTP_vector<MTPInputPeer>({ user }))), false, includesSelf);
	for (const auto &parent : { MTPInputPeer(MTP_inputPeerSelf()), explicitSelf, channel }) {
		const auto expected = parent.type() == mtpc_inputPeerChannel;
		checkWith("Saved history parent distinguishes channel messaging", Packet(
			MTP_int(mtpc_messages_getSavedHistory), MTP_int(1), parent, user,
			MTP_int(0), MTP_int(0), MTP_int(0), MTP_int(20), MTP_int(0), MTP_int(0), MTP_long(0)), expected, includesSelf);
		checkWith("Saved read marker parent distinguishes channel messaging", Packet(
			MTP_int(mtpc_messages_readSavedHistory), parent, user, MTP_int(1)), expected, includesSelf);
	}
	checkWith("Implicit Saved history denied", Packet(MTP_int(mtpc_messages_getSavedHistory),
		MTP_int(0), user, MTP_int(0), MTP_int(0), MTP_int(0), MTP_int(20),
		MTP_int(0), MTP_int(0), MTP_long(0)), false, includesSelf);
	check("empty peer", Text(MTP_inputPeerEmpty()), false);
	check("channel join requires channel ID", Request::Serialize(
		MTPchannels_JoinChannel(MTP_inputChannel(MTP_long(42), MTP_long(1)))), false);
	check("read before setup", Packet(MTP_int(mtpc_help_getConfig)), true);
	check("unknown method", Packet(MTP_int(0x12345678)), false);
	check("missing body", Request(), false);
	check("takeout export session", Request::Serialize(
		MTPaccount_InitTakeoutSession(MTP_flags(0), MTPlong())), false);
	check("takeout wrapper around safe read", Packet(
		MTP_int(mtpc_invokeWithTakeout), MTP_long(1),
		MTP_int(mtpc_help_getConfig)), false);
	check("takeout wrapper around allowed send", Packet(
		MTP_int(mtpc_invokeWithTakeout), MTP_long(1),
		MTP_int(mtpc_messages_sendMessage), MTP_int(2 | (0)), user,
		MTP_string("test"), MTP_long(1)), false);
	check("rich message allowed user", Request::Serialize(
		MTPmessages_GetRichMessage(user, MTP_int(1))), true);
	check("rich message denied user", Request::Serialize(
		MTPmessages_GetRichMessage(blocked, MTP_int(1))), false);
	check("rich message type-safe channel ID", Request::Serialize(
		MTPmessages_GetRichMessage(channel, MTP_int(1))), false);
	check("raw web page fetch has no allowed conversation", Request::Serialize(
		MTPmessages_GetWebPage(MTP_string("https://t.me/example/1"), MTP_int(0))), false);
	check("raw web page preview has no allowed conversation", Request::Serialize(
		MTPmessages_GetWebPagePreview(MTP_flags(0),
			MTP_string("https://t.me/example/1"), MTPVector<MTPMessageEntity>())), false);

	for (const auto &peer : { user, blocked }) {
		const auto expected = peer.c_inputPeerUser().vuser_id().v == 42;
		check("media", Packet(MTP_int(mtpc_messages_sendMedia), MTP_int(0),
			peer, MTPInputMedia(MTP_inputMediaEmpty()), MTP_string("caption"),
			MTP_long(1)), expected);
		check("album", Packet(MTP_int(mtpc_messages_sendMultiMedia), MTP_int(0),
			peer, MTPVector<MTPInputSingleMedia>()), expected);
		check("edit", Packet(MTP_int(mtpc_messages_editMessage), MTP_int(2 | (1 << 11)),
			peer, MTP_int(1), MTP_string("replacement")), expected);
		check("inline result", Packet(MTP_int(mtpc_messages_sendInlineBotResult),
			MTP_int(0), peer, MTP_long(1), MTP_long(2), MTP_string("result")), false);
		check("bot callback", Packet(MTP_int(mtpc_messages_getBotCallbackAnswer),
			MTP_int(0), peer, MTP_int(1)), expected);
		check("vote", Packet(MTP_int(mtpc_messages_sendVote), peer, MTP_int(1),
			MTPVector<MTPbytes>()), expected);
		check("reaction", Packet(MTP_int(mtpc_messages_sendReaction), MTP_int(0),
			peer, MTP_int(1)), expected);
		check("scheduled message", Packet(MTP_int(mtpc_messages_sendScheduledMessages),
			peer, MTPVector<MTPint>()), false);
		check("quick reply", Packet(MTP_int(mtpc_messages_sendQuickReplyMessages),
			peer, MTP_int(1), MTPVector<MTPint>(), MTPVector<MTPlong>()), false);
		check("forward destination", Forward(user, peer), expected);
		check("forward denied source", Forward(blocked, peer), false);
		check("typing", Packet(MTP_int(mtpc_messages_setTyping), MTP_int(0),
			peer, MTPSendMessageAction(MTP_sendMessageTypingAction())), expected);
		check("story reaction", Packet(MTP_int(mtpc_stories_sendReaction), MTP_int(0),
			peer, MTP_int(1), MTPReaction(MTP_reactionEmpty())), expected);
	}
	check("forward source group ID remains type-safe", Forward(group, user), false);
	check("forward source channel ID remains type-safe", Forward(channel, user), false);
	check("forward Saved Messages source is not implicit", Forward(
		MTP_inputPeerSelf(), user), false);
	check("forward from-message source uses actual conversation", Forward(
		MTP_inputPeerUserFromMessage(user, MTP_int(17), MTP_long(43)), user), false);
	check("forward from-message context is not the source", Forward(
		MTP_inputPeerUserFromMessage(blocked, MTP_int(17), MTP_long(42)), user), true);
	const auto forwardingAllows = Fn<bool(PeerId)>([](PeerId id) {
		return id == PeerId(UserId(42))
			|| id == PeerId(UserId(99))
			|| id == PeerId(ChatId(42))
			|| id == PeerId(ChannelId(42));
	});
	checkWith("forward allowed group to Saved Messages", Forward(
		group, MTP_inputPeerSelf()), false, forwardingAllows);
	checkWith("forward allowed channel to allowed user", Forward(
		channel, user), true, forwardingAllows);
	checkWith("forward denied source to allowed Saved Messages", Forward(
		blocked, MTP_inputPeerSelf()), false, forwardingAllows);
	checkWith("forward allowed group to denied destination", Forward(
		group, blocked), false, forwardingAllows);
	for (const auto &source : { user, blocked, group, channel }) {
		const auto expected = false;
		const auto story = MTPInputMedia(MTP_inputMediaStory(source, MTP_int(17)));
		check("story forward source", Packet(MTP_int(mtpc_messages_sendMedia),
			MTP_int(0), user, story, MTP_string(""), MTP_long(1)), expected);
		check("story upload source", Packet(MTP_int(mtpc_messages_uploadMedia),
			MTP_int(0), user, story), expected);
		check("story edit source", Packet(MTP_int(mtpc_messages_editMessage),
			MTP_int(2 | ((1 << 11) | (1 << 14))), user, MTP_int(1),
			MTP_string("edited"), story), expected);
		const auto single = MTP_inputSingleMedia(MTP_flags(0), story,
			MTP_long(1), MTP_string(""), MTPVector<MTPMessageEntity>());
		check("story album source", Packet(MTP_int(mtpc_messages_sendMultiMedia),
			MTP_int(0), user, MTPVector<MTPInputSingleMedia>(
				MTP_vector<MTPInputSingleMedia>({ single }))), expected);
		const auto paid = MTPInputMedia(MTP_inputMediaPaidMedia(MTP_flags(0),
			MTP_long(1), MTPVector<MTPInputMedia>(
				MTP_vector<MTPInputMedia>({ story })), MTPstring()));
		check("paid media cannot hide story source", Packet(
			MTP_int(mtpc_messages_sendMedia), MTP_int(0), user, paid,
			MTP_string(""), MTP_long(1)), expected);
	}
	check("bot start", Request::Serialize(MTPmessages_StartBot(
		bot, user, MTP_long(1), MTP_string("test"))), true);
	check("bot start parameter emoji denied", Request::Serialize(MTPmessages_StartBot(
		bot, user, MTP_long(1), MTP_string(u"start_\U0001F600"_q))), false);
	check("bot start parameter malformed UTF8 denied", Request::Serialize(MTPmessages_StartBot(
		bot, user, MTP_long(1), MTP_string(QByteArray::fromHex("c0af").toStdString()))), false);
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
		MTP_int(2 | (1)), user, MTPInputReplyTo(MTP_inputReplyToMonoForum(blocked)),
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
		MTP_int(mtpc_messages_sendMessage), MTP_int(2 | (0)), blocked,
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
	checkWith("rich message allowed channel", Request::Serialize(
		MTPmessages_GetRichMessage(channel, MTP_int(1))), true, channelAllows);
	checkWith("rich message denied channel", Request::Serialize(
		MTPmessages_GetRichMessage(MTP_inputPeerChannel(MTP_long(43), MTP_long(1)),
			MTP_int(1))), false, channelAllows);
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

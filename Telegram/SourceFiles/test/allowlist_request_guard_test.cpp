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
		const MTPMessageMedia &media = MTP_messageMediaEmpty()) {
	return MTP_message(
		MTP_flags(MTPDmessage::Flag::f_from_id | MTPDmessage::Flag::f_media),
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
		MTPReplyMarkup(),
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
		MTPlong(),
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

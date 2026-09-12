/*
This file is part of Telegram Desktop.
For license and copyright information see:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "scheme.h"
#include "data/data_peer_id.h"
#include "mtproto/details/mtproto_serialized_request.h"
#include <map>
#include <set>

namespace MTP {

[[nodiscard]] bool AllowlistDocumentContentAllowed(
	const QString &mime,
	const QVector<MTPDocumentAttribute> &attributes);
[[nodiscard]] bool AllowlistUploadPrefixAllowed(const QByteArray &bytes);

class AllowlistContentContext final {
public:
	explicit AllowlistContentContext(Fn<QByteArray(uint64)> resolveDocument = nullptr);
	void recordDocument(uint64 id, const QString &mime,
		const QVector<MTPDocumentAttribute> &attributes);
	void recordMessage(const MTPDmessage &message, bool scheduled = false);
	void forgetMessage(PeerId peer, int id, bool scheduled = false);
	[[nodiscard]] bool documentAllowed(const MTPInputDocument &document) const;
	[[nodiscard]] bool messageAllowed(PeerId peer, int id, bool scheduled = false) const;
	[[nodiscard]] bool uploadAllowed(const MTPInputFile &file) const;
	[[nodiscard]] bool recordUploadPart(uint64 id, int part, const QByteArray &bytes);

private:
	[[nodiscard]] bool documentContentAllowed(uint64 id) const;
	Fn<QByteArray(uint64)> _resolveDocument;
	struct UploadProof {
		bool allowed = false;
		int firstPartSize = 0;
		std::set<int> parts;
	};
	std::map<uint64, bool> _documents;
	std::map<std::pair<PeerId, int>, bool> _messages;
	std::map<std::pair<PeerId, int>, uint64> _messageDocuments;
	std::map<uint64, UploadProof> _uploads;

};

[[nodiscard]] bool AllowlistRequestAllowed(
	const details::SerializedRequest &request,
	UserId selfId,
	const Fn<bool(PeerId)> &allows,
	const Fn<bool(UserId)> &knownBot = nullptr,
	AllowlistContentContext *content = nullptr);

} // namespace MTP

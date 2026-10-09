/* Hub enrollment; included inside namespace Main. Upstream license: see LEGAL. */
namespace {
QByteArray HubEncoded(const QByteArray &bytes) {
	return bytes.toBase64(QByteArray::Base64UrlEncoding | QByteArray::OmitTrailingEquals);
}
QByteArray HubDecoded(const QJsonValue &value) {
	const auto text = value.toString().toLatin1();
	const auto raw = QByteArray::fromBase64(text, QByteArray::Base64UrlEncoding);
	return (raw.size() == 32 && HubEncoded(raw) == text) ? raw : QByteArray();
}
QByteArray HubDigest(const QByteArray &bytes) {
	return QCryptographicHash::hash(bytes, QCryptographicHash::Sha256).toHex();
}
QByteArray HubPublic(const QByteArray &seed) {
	if (seed.size() != 32) return {};
	const auto key = std::unique_ptr<EVP_PKEY, decltype(&EVP_PKEY_free)>(
		EVP_PKEY_new_raw_private_key(EVP_PKEY_ED25519, nullptr,
			reinterpret_cast<const unsigned char*>(seed.data()), seed.size()), EVP_PKEY_free);
	QByteArray result(32, Qt::Uninitialized); size_t size = result.size();
	return (key && EVP_PKEY_get_raw_public_key(key.get(), reinterpret_cast<unsigned char*>(result.data()), &size) == 1 && size == 32) ? result : QByteArray();
}
QByteArray HubSign(const QByteArray &seed, const QByteArray &message) {
	const auto key = std::unique_ptr<EVP_PKEY, decltype(&EVP_PKEY_free)>(
		EVP_PKEY_new_raw_private_key(EVP_PKEY_ED25519, nullptr,
			reinterpret_cast<const unsigned char*>(seed.data()), seed.size()), EVP_PKEY_free);
	const auto ctx = std::unique_ptr<EVP_MD_CTX, decltype(&EVP_MD_CTX_free)>(EVP_MD_CTX_new(), EVP_MD_CTX_free);
	QByteArray result(64, Qt::Uninitialized); size_t size = result.size();
	return (key && ctx && EVP_DigestSignInit(ctx.get(), nullptr, nullptr, nullptr, key.get()) == 1
		&& EVP_DigestSign(ctx.get(), reinterpret_cast<unsigned char*>(result.data()), &size,
			reinterpret_cast<const unsigned char*>(message.data()), message.size()) == 1 && size == 64) ? HubEncoded(result) : QByteArray();
}
// The Hub's canonical peer list (canonicalPeers): {"kind","id"} in that key
// order, sorted by kind then id, no labels, no whitespace.
QByteArray HubCanonicalPeers(const QJsonArray &peers) {
	auto list = std::vector<std::pair<QString, QString>>();
	for (const auto &value : peers) {
		const auto peer = value.toObject();
		list.emplace_back(peer["kind"].toString(), peer["id"].toString());
	}
	std::sort(list.begin(), list.end());
	auto result = QByteArray("[");
	for (const auto &[kind, id] : list) {
		if (result.size() > 1) result += ',';
		result += "{\"kind\":" + QJsonDocument(QJsonArray{kind}).toJson(QJsonDocument::Compact).mid(1).chopped(1)
			+ ",\"id\":" + QJsonDocument(QJsonArray{id}).toJson(QJsonDocument::Compact).mid(1).chopped(1) + '}';
	}
	return result + ']';
}
bool HubSafeOrigin(const QUrl &url) {
	return url.isValid() && url.scheme() == QStringLiteral("https") && !url.host().isEmpty()
		&& url.userInfo().isEmpty() && !url.hasQuery() && !url.hasFragment()
		&& (url.path().isEmpty() || url.path() == QStringLiteral("/"));
}
} // namespace

bool ManagedClient::hubReady() const {
	return _connection["v"].toInt() == 2 && _connection["decision"] == "hub-enrolled";
}
void ManagedClient::beginHubEnrollment() {
	if (_stopped || _busy || !automaticManagement() || (paired() && !_state)) return;
	if (!_host.configured()) {
		changeStatus(QStringLiteral("Choose your initial allowed chats. Class A management connects automatically afterward."));
		later(); // A local re-check only: nothing is sent until the first pick is saved.
		return;
	}
	if (!HubSafeOrigin(_hubOrigin) || !_host.connectionStored || !_host.connectionCommit) {
		if (_state) { _hubSuppressed = true; refresh(); return; }
		changeStatus(QStringLiteral("Hub connection is not configured in this build. Your existing chat restrictions remain active."));
		later();
		return;
	}
	// A migrated account may never replace a previously pinned, different head.
	if (_state && (_state->origin != _connectionOrigin || _state->publicKey != _connectionKey)) {
		_hubSuppressed = true; refresh();
		return;
	}
	const auto saved = _host.connectionStored();
	const auto value = saved.isEmpty() ? std::optional<QJsonObject>() : Managed::ReadObject(saved);
	if (!saved.isEmpty() && !value) {
		changeStatus(QStringLiteral("Connection settings are invalid. Your existing restrictions remain active."));
		return;
	}
	if (value && (*value)["v"].toInt() == 2) {
		if ((*value)["subject"] != _host.subject || (*value)["origin"] != _connectionOrigin.toString(QUrl::FullyEncoded)
			|| (*value)["public_key"] != QString::fromLatin1(HubEncoded(_connectionKey))
			|| (*value)["hub_origin"] != _hubOrigin.toString(QUrl::FullyEncoded)
			|| HubDecoded((*value)["device_seed"]).isEmpty()
			|| !QRegularExpression(QStringLiteral("^[A-Za-z0-9_-]{43}$")).match((*value)["token"].toString()).hasMatch()
			|| !(*value)["initial_peers"].isArray()
			|| ((*value)["decision"] != "hub-pending" && (*value)["decision"] != "hub-enrolled")) {
			changeStatus(QStringLiteral("Hub connection settings are invalid. No identity proof was requested."));
			return;
		}
		_connection = *value;
		if (hubReady()) {
			if (_state) refresh();
			else changeStatus(QStringLiteral("Managed policy is missing. Contact management; no new identity was created."));
			return;
		}
	} else {
		if (value && !readConnection() && _connection.isEmpty()) return;
		QByteArray seed(32, Qt::Uninitialized), random(32, Qt::Uninitialized);
		if (RAND_bytes(reinterpret_cast<unsigned char*>(seed.data()), seed.size()) != 1
			|| RAND_bytes(reinterpret_cast<unsigned char*>(random.data()), random.size()) != 1) return;
		const auto token = _state ? _state->token : !_connection["token"].toString().isEmpty()
			? _connection["token"].toString().toLatin1() : HubEncoded(random);
		QJsonArray peers;
		for (const auto &peer : _host.initial()) peers.append(QJsonObject{{"kind",peer.kind},{"id",QString::number(peer.id)}});
		if (peers.size() > 10000 || QJsonDocument(peers).toJson(QJsonDocument::Compact).size() > Managed::MaxPolicyBytes) return;
		if (!persistConnection(QJsonObject{{"v",2},{"decision","hub-pending"},{"subject",_host.subject},
			{"origin",_connectionOrigin.toString(QUrl::FullyEncoded)},{"public_key",QString::fromLatin1(HubEncoded(_connectionKey))},
			{"hub_origin",_hubOrigin.toString(QUrl::FullyEncoded)},{"token",QString::fromLatin1(token)},
			{"device_seed",QString::fromLatin1(HubEncoded(seed))},{"initial_peers",peers}})) return;
	}
	const auto expires = _connection["expires_at"].toInteger();
	if (_connection["enroll_body"].isObject() && expires > QDateTime::currentSecsSinceEpoch()) {
		submitHubEnrollment();
		return;
	}
	auto clean = _connection; clean.remove("enroll_body"); clean.remove("expires_at");
	if (!persistConnection(clean)) return;
	const auto body = QJsonDocument(QJsonObject{{"v",1},
		{"device_public_key",QString::fromLatin1(HubEncoded(HubPublic(HubDecoded(_connection["device_seed"]))))},
		{"client_version","hub-integration-dev"}}).toJson(QJsonDocument::Compact);
	changeStatus(QStringLiteral("Connecting this account to Class A management..."));
	request(Stage::HubChallenge,_hubOrigin,body,{},[this](const QByteArray &response) { receiveHubChallenge(response); });
}
void ManagedClient::receiveHubChallenge(const QByteArray &bytes) {
	const auto value = Managed::ReadObject(bytes);
	const auto now = QDateTime::currentSecsSinceEpoch();
	if (!value || (*value)["v"].toInt() != 1 || (*value)["bot_id"] != QStringLiteral("8558994389")
		|| (*value)["bot_username"] != QStringLiteral("ClassAAssistant_bot")
		|| !QRegularExpression(QStringLiteral("^[a-f0-9]{32}$")).match((*value)["challenge_id"].toString()).hasMatch()
		|| !QRegularExpression(QStringLiteral("^agc_[A-Za-z0-9_-]{22}$")).match((*value)["start_param"].toString()).hasMatch()
		|| (*value)["expires_at"].toInteger() <= now || (*value)["expires_at"].toInteger() > now + 360) {
		changeStatus(QStringLiteral("Hub identity challenge rejected. No Telegram proof was requested.")); later(); return;
	}
	_busy = true;
	const auto generation = ++_proofGeneration;
	_proofDeadline.start(30000);
	const auto guard = QPointer<ManagedClient>(this);
	_host.hubProof((*value)["start_param"].toString(),[guard,generation,challenge = *value](QByteArray proof) {
		if (!guard || guard->_stopped || generation != guard->_proofGeneration) return;
		guard->_proofDeadline.stop(); guard->_busy = false;
		if (proof.isEmpty() || proof.size() > 32768) {
			guard->changeStatus(QStringLiteral("Telegram identity proof unavailable. Your existing restrictions remain active.")); guard->later(); return;
		}
		const auto seed = HubDecoded(guard->_connection["device_seed"]);
		const auto peers = guard->_connection["initial_peers"].toArray();
		const auto credential = HubDigest(guard->_connection["token"].toString().toLatin1());
		const auto id = challenge["challenge_id"].toString();
		const auto message = QByteArray("ALLOWGRAM_HUB_ENROLL_V1\n") + id.toLatin1() + '\n'
			+ HubDigest(proof) + '\n' + HubDigest(HubCanonicalPeers(peers)) + '\n' + credential;
		const auto signature = HubSign(seed, message);
		if (signature.isEmpty()) { guard->later(); return; }
		const auto body = QJsonObject{{"v",1},{"challenge_id",id},{"init_data",QString::fromUtf8(proof)},
			{"device",QJsonObject{{"public_key",QString::fromLatin1(HubEncoded(HubPublic(seed)))},
				{"name","Allowgram Desktop"},{"client_version","hub-integration-dev"},{"credential_hash",QString::fromLatin1(credential)}}},
			{"initial_peers",peers},{"device_sig",QString::fromLatin1(signature)}};
		auto next = guard->_connection; next["enroll_body"] = body; next["expires_at"] = challenge["expires_at"];
		if (guard->persistConnection(next)) guard->submitHubEnrollment();
	});
}
void ManagedClient::submitHubEnrollment() {
	const auto body = QJsonDocument(_connection["enroll_body"].toObject()).toJson(QJsonDocument::Compact);
	request(Stage::HubEnroll,_hubOrigin,body,{},[this](const QByteArray &bytes) {
		const auto value = Managed::ReadObject(bytes);
		if (!value || (*value)["status"] != "enrolled"
			|| QUrl((*value)["head_url"].toString()) != _connectionOrigin
			|| !QRegularExpression(QStringLiteral("^[a-f0-9]{32}$")).match((*value)["device_id"].toString()).hasMatch()
			|| !(*value)["policy"].isObject()) {
			changeStatus(QStringLiteral("Hub enrollment response rejected. The pinned head and chat restrictions are unchanged.")); later(); return;
		}
		auto state = Managed::State{_connectionOrigin,_host.subject,(*value)["device_id"].toString(),
			_connectionKey,_connection["token"].toString().toLatin1(),
			QJsonDocument((*value)["policy"].toObject()).toJson(QJsonDocument::Compact)};
		if (!apply(std::move(state))) { later(); return; }
		auto next = _connection; next["decision"] = "hub-enrolled"; next.remove("enroll_body"); next.remove("expires_at");
		if (!persistConnection(next)) { later(); return; }
		acknowledge();
	});
}
namespace {
constexpr auto kChatsReportInterval = qint64(300);
// The head's Label rule: 1..128 code points, no Unicode category C (control, format, unassigned, private use).
QString HubChatLabel(const QString &title, const QString &fallback) {
	auto result = QString();
	auto count = 0;
	for (const auto codepoint : title.trimmed().toUcs4()) {
		const auto category = QChar::category(char32_t(codepoint));
		if (category == QChar::Other_Control || category == QChar::Other_Format
			|| category == QChar::Other_Surrogate || category == QChar::Other_PrivateUse
			|| category == QChar::Other_NotAssigned) {
			continue;
		}
		if (++count > 128) break;
		result += QString::fromUcs4(&codepoint, 1);
	}
	result = result.trimmed();
	return result.isEmpty() ? fallback : result;
}
} // namespace
void ManagedClient::reportChats() {
	if (_stopped || !_host.chats || !_state || _chatsLoading) return;
	const auto now = QDateTime::currentSecsSinceEpoch();
	if (_chatsReportedAt && now - _chatsReportedAt < kChatsReportInterval) return;
	_chatsLoading = true;
	_chatsReportedAt = now; // A failed walk or upload waits a full interval too.
	const auto guard = QPointer<ManagedClient>(this);
	const auto origin = _state->origin;
	const auto token = _state->token;
	_host.chats([guard, origin, token](QJsonArray chats) {
		if (!guard || guard->_stopped) return;
		guard->_chatsLoading = false;
		if (chats.isEmpty() || !guard->_state || guard->_state->origin != origin || guard->_state->token != token) return;
		auto clean = QJsonArray();
		for (const auto &value : chats) {
			const auto chat = value.toObject();
			const auto id = chat["id"].toString();
			clean.append(QJsonObject{{"kind",chat["kind"]},{"id",id},
				{"label",HubChatLabel(chat["label"].toString(), id)}});
		}
		const auto body = QJsonDocument(QJsonObject{{"v",1},{"chats",clean}}).toJson(QJsonDocument::Compact);
		QNetworkRequest request(origin.resolved(QUrl(QStringLiteral("/api/client/chats"))));
		request.setAttribute(QNetworkRequest::RedirectPolicyAttribute,QNetworkRequest::ManualRedirectPolicy);
		request.setAttribute(QNetworkRequest::CookieLoadControlAttribute,QNetworkRequest::Manual);
		request.setAttribute(QNetworkRequest::CookieSaveControlAttribute,QNetworkRequest::Manual);
		request.setTransferTimeout(15000);
		request.setRawHeader("Accept","application/json");
		request.setRawHeader("Authorization",QByteArray("Bearer ")+token);
		request.setHeader(QNetworkRequest::ContentTypeHeader,QStringLiteral("application/json"));
		if (guard->_chatsReply) guard->_chatsReply->abort();
		const auto reply = guard->_network->put(request, body);
		guard->_chatsReply = reply;
		QObject::connect(reply,&QNetworkReply::finished,reply,&QObject::deleteLater);
	});
}

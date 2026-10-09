/* Consent transport implementation, included inside namespace Main. See LEGAL. */
bool ManagedClient::needsConsent() const {
	return !automaticManagement() && !_stopped && !paired() && _host.configured()
		&& _host.connectionStored && _host.connectionCommit
		&& _host.connectionStored().isEmpty();
}
bool ManagedClient::claimConsentPrompt() {
	if (_promptShown || !needsConsent()) return false;
	_promptShown = true;
	return true;
}
bool ManagedClient::persistConnection(const QJsonObject &value) {
	const auto bytes = QJsonDocument(value).toJson(QJsonDocument::Compact);
	if (!_host.connectionCommit || bytes.size() > Managed::MaxWireBytes
		|| !_host.connectionCommit(bytes)) {
		changeStatus(QStringLiteral("Could not save connection settings. Your existing chat restrictions remain active."));
		return false;
	}
	_connection = value;
	return true;
}
QString ManagedClient::connectionFingerprint() const {
	const auto token = _connection["token"].toString().toLatin1();
	if (token.isEmpty()) return {};
	const auto code = QCryptographicHash::hash(QByteArray("ALLOWGRAM_CONNECTION_V1\n") + token,
		QCryptographicHash::Sha256).toHex().left(16).toUpper();
	return QString::fromLatin1(code.mid(0,4) + '-' + code.mid(4,4) + '-' + code.mid(8,4) + '-' + code.mid(12,4));
}
bool ManagedClient::declineDashboard() {
	if (automaticManagement()) return false;
	if (_stopped || _busy || paired() || _connection["decision"] == "pending") return false;
	if (!persistConnection(QJsonObject{{"v",1},{"decision","declined"},{"subject",_host.subject},
		{"origin",_connectionOrigin.toString(QUrl::FullyEncoded)},
		{"public_key",QString::fromLatin1(_connectionKey.toBase64(QByteArray::Base64UrlEncoding | QByteArray::OmitTrailingEquals))}})) return false;
	changeStatus(QStringLiteral("Not connected. Your chat selections are unchanged. You can connect from Settings later."));
	return true;
}
bool ManagedClient::connectDashboard() {
	if (automaticManagement()) { refresh(); return !_stopped; }
	if (_stopped || _busy || paired() || !_host.configured()
		|| !_host.connectionStored || !_host.connectionCommit) return false;
	if (_connection["decision"] == "pending") { refreshConnection(); return true; }
	QByteArray random(32, Qt::Uninitialized);
	if (RAND_bytes(reinterpret_cast<unsigned char*>(random.data()), int(random.size())) != 1) {
		changeStatus(QStringLiteral("Unable to create a secure connection. No request was sent."));
		return false;
	}
	QJsonArray peers;
	for (const auto &peer : _host.initial()) peers.append(QJsonObject{{"kind",peer.kind},{"id",QString::number(peer.id)}});
	const auto request = QJsonObject{{"telegram_user_id",_host.subject},{"initial_peers",peers},
		{"device_name","Allowgram Desktop"},{"client_version","7.2.8.12"},{"consent_version",1}};
	if (peers.size() > 10000 || QJsonDocument(request).toJson(QJsonDocument::Compact).size() > Managed::MaxPolicyBytes) return false;
	const auto value = QJsonObject{{"v",1},{"decision","pending"},{"subject",_host.subject},
		{"origin",_connectionOrigin.toString(QUrl::FullyEncoded)},
		{"public_key",QString::fromLatin1(_connectionKey.toBase64(QByteArray::Base64UrlEncoding | QByteArray::OmitTrailingEquals))},
		{"token",QString::fromLatin1(random.toBase64(QByteArray::Base64UrlEncoding | QByteArray::OmitTrailingEquals))},
		{"request",request}};
	if (!persistConnection(value)) return false; // Durable before any network activity.
	_connectionSubmitted = false;
	refreshConnection();
	return true;
}
bool ManagedClient::readConnection() {
	_connection = {};
	_connectionSubmitted = false;
	const auto bytes = _host.connectionStored ? _host.connectionStored() : QByteArray();
	if (bytes.isEmpty()) {
		changeStatus(QStringLiteral("Not connected to the dashboard. Your initial chat selection remains in use."));
		return false;
	}
	const auto value = Managed::ReadObject(bytes);
	const auto bad = [this] {
		changeStatus(QStringLiteral("Connection settings are invalid. No request sent; your selected chats are unchanged."));
		return false;
	};
	if (!value || (*value)["v"].toInt(-1) != 1 || (*value)["subject"] != _host.subject
		|| (*value)["origin"] != _connectionOrigin.toString(QUrl::FullyEncoded)
		|| (*value)["public_key"] != QString::fromLatin1(_connectionKey.toBase64(QByteArray::Base64UrlEncoding | QByteArray::OmitTrailingEquals))) return bad();
	const auto decision = (*value)["decision"].toString();
	if (decision == "declined" || decision == "rejected") {
		_connection = *value;
		changeStatus(decision == "declined" ? QStringLiteral("Not connected. You can connect from Settings later.")
			: QStringLiteral("Connection rejected or expired. Your selected chats are unchanged; contact your owner."));
		return false;
	}
	if (decision != "pending" || value->size() != 7 || !(*value)["request"].isObject()) return bad();
	const auto token = (*value)["token"].toString().toLatin1();
	const auto decoded = QByteArray::fromBase64(token, QByteArray::Base64UrlEncoding);
	if (token.size() != 43 || decoded.size() != 32
		|| decoded.toBase64(QByteArray::Base64UrlEncoding | QByteArray::OmitTrailingEquals) != token) return bad();
	const auto request = (*value)["request"].toObject();
	if (request.size() != 5 || request["telegram_user_id"] != _host.subject
		|| request["consent_version"].toInt(-1) != 1 || !request["initial_peers"].isArray()
		|| request["device_name"].toString().isEmpty() || request["device_name"].toString().size() > 128
		|| request["client_version"].toString().isEmpty() || request["client_version"].toString().size() > 40
		|| QJsonDocument(request).toJson(QJsonDocument::Compact).size() > Managed::MaxPolicyBytes) return bad();
	const auto peers = request["initial_peers"].toArray();
	if (peers.size() > 10000) return bad();
	QSet<QString> seen;
	const auto idPattern = QRegularExpression(QStringLiteral("^[1-9][0-9]{0,14}$"));
	for (const auto &peer : peers) {
		const auto p = peer.toObject();
		const auto id = p["id"].toString();
		const auto kind = p["kind"].toString();
		const auto key = kind + ':' + id;
		if (p.size() != 2 || !idPattern.match(id).hasMatch() || id.toULongLong() > 281474976710655ULL
			|| (kind != "user" && kind != "chat" && kind != "channel") || seen.contains(key)) return bad();
		seen.insert(key);
	}
	_connection = *value;
	return true;
}
void ManagedClient::refreshConnection() {
	if (_stopped || _busy || paired() || _connection["decision"] != "pending") return;
	_timer.stop();
	const auto body = QJsonDocument(_connection["request"].toObject()).toJson(QJsonDocument::Compact);
	changeStatus(QStringLiteral("Connecting to dashboard. Verification code: %1").arg(connectionFingerprint()));
	request(_connectionSubmitted ? Stage::Connection : Stage::Register, _connectionOrigin,
		_connectionSubmitted ? QByteArray() : body, _connection["token"].toString().toLatin1(),
		[this](const QByteArray &response) { receiveConnection(response); });
}
void ManagedClient::receiveConnection(const QByteArray &bytes) {
	const auto value = Managed::ReadObject(bytes);
	const auto reject = [this] {
		changeStatus(QStringLiteral("Invalid dashboard response. Your chat selection remains unchanged."));
		later();
	};
	if (!value || (*value)["v"].toInt(-1) != 1 || (*value)["telegram_user_id"] != _host.subject
		|| (*value)["fingerprint"] != connectionFingerprint()
		|| !QRegularExpression(QStringLiteral("^[a-f0-9]{32}$")).match((*value)["id"].toString()).hasMatch()) { reject(); return; }
	const auto status = (*value)["status"].toString();
	if (status == "pending" && value->size() == 5) {
		_connectionSubmitted = true;
		changeStatus(QStringLiteral("Listed in dashboard, awaiting owner approval. Verification code: %1. Your chats are unchanged.").arg(connectionFingerprint()));
		later();
	} else if (status == "rejected" && value->size() == 5) {
		auto next = _connection; next["decision"] = "rejected";
		if (!persistConnection(next)) { later(); return; }
		changeStatus(QStringLiteral("Connection rejected or expired. Your chats are unchanged; contact your owner."));
	} else if (status == "approved" && value->size() == 7 && (*value)["policy"].isObject()
		&& QRegularExpression(QStringLiteral("^[a-f0-9]{32}$")).match((*value)["device_id"].toString()).hasMatch()) {
		auto state = Managed::State{_connectionOrigin,_host.subject,(*value)["device_id"].toString(),
			_connectionKey,_connection["token"].toString().toLatin1(),
			QJsonDocument((*value)["policy"].toObject()).toJson(QJsonDocument::Compact)};
		if (apply(std::move(state))) acknowledge();
		else later();
	} else reject();
}

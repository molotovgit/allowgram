/* Allowgram: account-bound managed allowlist transport. See LEGAL. */
#include "main/allowlist_managed_client.h"
#include <QtCore/QDateTime>
#include <QtCore/QCryptographicHash>
#include <QtCore/QRegularExpression>
#include <QtCore/QSet>
#include <openssl/rand.h>
#include <QtCore/QJsonArray>
#include <QtCore/QJsonDocument>
#include <QtNetwork/QNetworkRequest>
#include <algorithm>
#include <memory>

namespace Main {
ManagedClient::ManagedClient(Host host, QObject *parent, QNetworkAccessManager *testTransport,
		std::optional<Managed::Invitation> testConnectionTrust)
: QObject(parent)
, _host(std::move(host))
, _connectionOrigin(QStringLiteral("https://allowgram-head-production.up.railway.app"))
, _connectionKey(QByteArray::fromBase64("emcLYc3W7m8Gv1y9r_cLBug_WZuzjFl1iMaNiV1ZYKw", QByteArray::Base64UrlEncoding))
, _network(testTransport ? testTransport : new QNetworkAccessManager(this)) {
	if (testTransport && testConnectionTrust) {
		_connectionOrigin = testConnectionTrust->origin;
		_connectionKey = testConnectionTrust->publicKey;
	}
	_timer.setSingleShot(true);
	QObject::connect(&_timer,&QTimer::timeout,this,[this] { refresh(); });
}
ManagedClient::~ManagedClient() { stop(); }
void ManagedClient::stop() {
	_stopped = true;
	_timer.stop();
	if (_reply) {
		_reply->disconnect(this);
		_reply->abort();
		_reply->deleteLater();
		_reply.clear();
	}
	_busy = false;
}
void ManagedClient::observe(QObject *context, std::function<void()> changed) {
	_observers.push_back({context,std::move(changed)});
	QObject::connect(context,&QObject::destroyed,this,[this] {
		_observers.erase(std::remove_if(_observers.begin(),_observers.end(),
			[](const Observer &observer) { return observer.context.isNull(); }),_observers.end());
	});
}
void ManagedClient::changeStatus(QString status) {
	_status = std::move(status);
	const auto observers = _observers;
	for (const auto &observer : observers) if (observer.context) observer.changed();
}
QString ManagedClient::manager() const {
	return _state ? _state->origin.toString(QUrl::FullyEncoded) : QString();
}
void ManagedClient::start() {
	if (_stopped || _busy) return;
	const auto saved = _host.stored();
	if (saved.isEmpty()) {
		if (readConnection()) refreshConnection();
		return;
	}
	_state = Managed::ReadState(saved,_host.subject,QDateTime::currentSecsSinceEpoch());
	if (!_state) {
		changeStatus(QStringLiteral("Managed settings are invalid. Access stays locked; contact your head."));
		return;
	}
	_policy = Managed::VerifyPolicy(_state->envelope,_state->publicKey,_host.subject,
		_state->device,0,{},QDateTime::currentSecsSinceEpoch());
	changeStatus(QStringLiteral("Using verified revision %1. Connecting to your head...").arg(_policy->revision));
	refresh();
}
void ManagedClient::later() {
	if (!_stopped && (_state || _connection["decision"] == "pending")) _timer.start(30000);
}
void ManagedClient::refresh() {
	if (_stopped || _busy) return;
	if (!_state) { refreshConnection(); return; }
	_timer.stop();
	request(Stage::Policy,_state->origin,{},_state->token,[this](const QByteArray &body) {
		auto next = *_state;
		next.envelope = body;
		if (apply(std::move(next))) acknowledge();
		else later();
	});
}
bool ManagedClient::pair(const QString &invitation) {
	if (_stopped || _busy) return false;
	if (_connection["decision"] == "pending") {
		changeStatus(QStringLiteral("A dashboard connection is pending. Verify its code with your owner."));
		return false;
	}
	if (paired()) {
		changeStatus(QStringLiteral("Already managed. This account cannot replace its pinned head locally."));
		return false;
	}
	if (!_host.configured()) {
		changeStatus(QStringLiteral("Choose your initial allowed chats before connecting a head."));
		return false;
	}
	const auto parsed = Managed::ReadInvitation(invitation.trimmed(),_host.subject);
	if (!parsed) {
		changeStatus(QStringLiteral("Invalid invitation, insecure address, or different Telegram account."));
		return false;
	}
	QJsonArray peers;
	for (const auto &peer : _host.initial()) peers.append(QJsonObject{
		{"kind",peer.kind},{"id",QString::number(peer.id)}});
	const auto body = QJsonDocument(QJsonObject{
		{"code",QString::fromLatin1(parsed->code)},
		{"telegram_user_id",_host.subject},
		{"device_name",QStringLiteral("Allowgram Desktop")},
		{"client_version",QStringLiteral("managed-policy-v1")},
		{"initial_peers",peers}}).toJson(QJsonDocument::Compact);
	changeStatus(QStringLiteral("Connecting to the selected head..."));
	request(Stage::Enroll,parsed->origin,body,{},[this,invitation = *parsed](const QByteArray &response) {
		const auto state = Managed::ReadEnrollment(response,invitation,QDateTime::currentSecsSinceEpoch());
		if (!state) {
			changeStatus(QStringLiteral("Enrollment response rejected. Your current choices remain unchanged."));
			return;
		}
		if (apply(*state)) acknowledge();
	});
	return true;
}
bool ManagedClient::apply(Managed::State state) {
	if (_state && (state.origin != _state->origin || state.publicKey != _state->publicKey
		|| state.device != _state->device || state.subject != _host.subject)) return false;
	const auto policy = Managed::VerifyPolicy(state.envelope,state.publicKey,_host.subject,state.device,
		_policy ? _policy->revision : 0,_policy ? _policy->bytes : QByteArray(),QDateTime::currentSecsSinceEpoch());
	if (!policy) {
		changeStatus(QStringLiteral("Policy rejected. Your last verified list remains active."));
		return false;
	}
	if (!_host.commit(Managed::SaveState(state))) {
		changeStatus(QStringLiteral("Could not save the policy. Nothing was applied or acknowledged."));
		return false;
	}
	_state = std::move(state);
	_policy = policy;
	changeStatus(QStringLiteral("Revision %1 applied locally. Acknowledging...").arg(_policy->revision));
	return true;
}
void ManagedClient::acknowledge() {
	if (_stopped || !_state || !_policy) return;
	const auto body = QJsonDocument(QJsonObject{{"revision",_policy->revision},
		{"policy_sha256",QString::fromLatin1(_policy->digest)}}).toJson(QJsonDocument::Compact);
	request(Stage::Ack,_state->origin,body,_state->token,[this](const QByteArray &) {
		changeStatus(QStringLiteral("Revision %1 applied and acknowledged. Connected to your head.").arg(_policy->revision));
		later();
	});
}
void ManagedClient::request(Stage stage, const QUrl &origin, const QByteArray &body,
		const QByteArray &token, std::function<void(const QByteArray &)> done) {
	if (_stopped || _busy) return;
	const auto path = stage == Stage::Enroll ? QStringLiteral("/api/client/enroll")
		: stage == Stage::Ack ? QStringLiteral("/api/client/ack")
		: stage == Stage::Register ? QStringLiteral("/api/client/register")
		: stage == Stage::Connection ? QStringLiteral("/api/client/registration") : QStringLiteral("/api/client/policy");
	QNetworkRequest request(origin.resolved(QUrl(path)));
	request.setAttribute(QNetworkRequest::RedirectPolicyAttribute,QNetworkRequest::ManualRedirectPolicy);
	request.setAttribute(QNetworkRequest::CookieLoadControlAttribute,QNetworkRequest::Manual);
	request.setAttribute(QNetworkRequest::CookieSaveControlAttribute,QNetworkRequest::Manual);
	request.setTransferTimeout(15000);
	request.setRawHeader("Accept","application/json");
	if (!token.isEmpty()) request.setRawHeader("Authorization",QByteArray("Bearer ")+token);
	const auto get = stage == Stage::Policy || stage == Stage::Connection;
	if (!get) request.setHeader(QNetworkRequest::ContentTypeHeader,QStringLiteral("application/json"));
	_busy = true;
	changeStatus(_status);
	const auto reply = get ? _network->get(request) : _network->post(request,body);
	_reply = reply;
	reply->setReadBufferSize(Managed::MaxWireBytes);
	const auto deadline = new QTimer(reply);
	deadline->setSingleShot(true);
	QObject::connect(deadline,&QTimer::timeout,reply,&QNetworkReply::abort);
	deadline->start(15000); // Absolute deadline also stops a slow drip-feed.
	struct Received { QByteArray bytes; bool oversized = false; bool finished = false; };
	const auto received = std::make_shared<Received>();
	const auto consume = [this,reply,received] {
		if (_stopped || _reply != reply || received->oversized) return;
		if (reply->bytesAvailable() > Managed::MaxWireBytes - received->bytes.size()) {
			received->oversized = true;
			reply->abort();
			return;
		}
		received->bytes += reply->readAll();
	};
	QObject::connect(reply,&QNetworkReply::readyRead,this,consume);
	QObject::connect(reply,&QNetworkReply::finished,this,[this,reply,deadline,received,consume,done = std::move(done)] {
		if (_stopped || _reply != reply || received->finished) return;
		received->finished = true;
		deadline->stop();
		consume();
		const auto status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
		const auto success = !received->oversized && reply->error() == QNetworkReply::NoError
			&& status >= 200 && status < 300;
		_reply.clear();
		_busy = false;
		reply->deleteLater();
		if (!success) {
			changeStatus(status == 401 || status == 403
				? QStringLiteral("Sync denied. Your last verified list is still active.")
				: QStringLiteral("Head unreachable or response rejected. Your existing restrictions remain active."));
			later();
			return;
		}
		done(received->bytes);
	});
}
#include "main/allowlist_managed_connection.inc"
} // namespace Main

/* Allowgram: account-bound managed allowlist transport. See LEGAL. */
#pragma once
#include "main/allowlist_managed_policy.h"
#include <QtCore/QObject>
#include <QtCore/QPointer>
#include <QtCore/QTimer>
#include <QtNetwork/QNetworkAccessManager>
#include <QtNetwork/QNetworkReply>
#include <functional>

namespace Main {
class ManagedClient final : public QObject {
public:
	struct Host {
		QString subject;
		std::function<QByteArray()> stored;
		std::function<bool()> configured;
		std::function<std::vector<Managed::Peer>()> initial;
		// Must validate, durably persist and apply before returning true.
		std::function<bool(const QByteArray &)> commit;
		std::function<QByteArray()> connectionStored;
		std::function<bool(const QByteArray &)> connectionCommit;
	};
	ManagedClient(Host host, QObject *parent = nullptr,
		QNetworkAccessManager *testTransport = nullptr,
		std::optional<Managed::Invitation> testConnectionTrust = std::nullopt);
	~ManagedClient();
	void start();
	void refresh();
	bool pair(const QString &invitation);
	bool connectDashboard();
	bool declineDashboard();
	[[nodiscard]] bool needsConsent() const;
	bool claimConsentPrompt();
	[[nodiscard]] QString connectionFingerprint() const;
	[[nodiscard]] QString dashboardAddress() const { return _connectionOrigin.toString(QUrl::FullyEncoded); }
	void stop();
	[[nodiscard]] bool busy() const { return _busy; }
	[[nodiscard]] bool paired() const { return !_host.stored().isEmpty(); }
	[[nodiscard]] QString status() const { return _status; }
	[[nodiscard]] QString manager() const;
	void observe(QObject *context, std::function<void()> changed);
private:
	enum class Stage { Enroll, Policy, Ack, Register, Connection };
	bool readConnection();
	void refreshConnection();
	void receiveConnection(const QByteArray &body);
	bool persistConnection(const QJsonObject &value);
	void request(Stage stage, const QUrl &origin, const QByteArray &body,
		const QByteArray &token, std::function<void(const QByteArray &)> done);
	bool apply(Managed::State state);
	void acknowledge();
	void changeStatus(QString status);
	void later();
	Host _host;
	QJsonObject _connection;
	QUrl _connectionOrigin;
	QByteArray _connectionKey;
	bool _connectionSubmitted = false;
	bool _promptShown = false;
	QNetworkAccessManager *_network = nullptr;
	QPointer<QNetworkReply> _reply;
	QTimer _timer;
	std::optional<Managed::State> _state;
	std::optional<Managed::Policy> _policy;
	QString _status;
	bool _busy = false;
	bool _stopped = false;
	struct Observer { QPointer<QObject> context; std::function<void()> changed; };
	std::vector<Observer> _observers;
};
} // namespace Main

#include "main/allowlist_sheet_resolver.h"

#include <QtCore/QCryptographicHash>
#include <QtCore/QDateTime>
#include <QtCore/QJsonDocument>
#include <QtCore/QJsonObject>
#include <QtCore/QMap>
#include <QtNetwork/QNetworkProxy>
#include <QtNetwork/QSslCertificate>
#include <QtNetwork/QSslConfiguration>
#include <QtNetwork/QSslSocket>
#include <algorithm>
#include <utility>

namespace Main::Allowlist::Sheet {
namespace {

constexpr auto kMaximumHeaders = 8192;

[[nodiscard]] bool ExpectedTrustError(const QSslError &error) {
	switch (error.error()) {
	case QSslError::SelfSignedCertificate:
	case QSslError::SelfSignedCertificateInChain:
	case QSslError::UnableToGetLocalIssuerCertificate:
	case QSslError::CertificateUntrusted:
	case QSslError::UnableToVerifyFirstCertificate:
		return true;
	default:
		return false;
	}
}

} // namespace

Resolver::Resolver(QObject *parent) : QObject(parent) {
	_timer.setSingleShot(true);
	connect(&_timer, &QTimer::timeout, this, [=] { finish(); });
}

Resolver::~Resolver() {
	cancel();
}

void Resolver::cancel() {
	_done = {};
	_timer.stop();
	if (const auto socket = std::exchange(_socket, nullptr)) {
		socket->disconnect(this);
		socket->abort();
		socket->deleteLater();
	}
	_profile = {};
	_buffer.clear();
	_contentLength = -1;
	_sent = false;
}

void Resolver::start(
		const QString &profilePath,
		const QString &phone,
		Callback done) {
	cancel();
	_done = std::move(done);
	const auto profile = ReadProfile(profilePath, phone);
	if (!profile) {
		_timer.start(0);
		return;
	}
	_profile = *profile;
	_socket = new QSslSocket(this);
	_socket->setProxy(QNetworkProxy::NoProxy);
	_socket->setPeerVerifyMode(QSslSocket::VerifyPeer);
	_socket->setPeerVerifyName(_profile.endpoint.host());
	_socket->setProtocol(QSsl::TlsV1_2OrLater);
	_socket->setReadBufferSize(kMaximumResponseBytes + kMaximumHeaders + 1);
	auto configuration = _socket->sslConfiguration();
	configuration.setAllowedNextProtocols({ QByteArray("http/1.1") });
	_socket->setSslConfiguration(configuration);
	connect(_socket, &QSslSocket::sslErrors, this,
		[=](const QList<QSslError> &errors) {
			if (!certificateAllowed()
				|| !std::all_of(errors.begin(), errors.end(), ExpectedTrustError)) {
				finish();
				return;
			}
			_socket->ignoreSslErrors(errors);
		});
	connect(_socket, &QSslSocket::encrypted, this, [=] { sendRequest(); });
	connect(_socket, &QSslSocket::readyRead, this, [=] { readResponse(); });
	connect(_socket, &QSslSocket::disconnected, this, [=] { finish(); });
	connect(_socket, &QSslSocket::errorOccurred, this, [=] { finish(); });
	_timer.start(kTimeoutMs);
	_socket->connectToHostEncrypted(
		_profile.endpoint.host(),
		quint16(_profile.endpoint.port(443)));
}

bool Resolver::certificateAllowed() const {
	const auto certificate = _socket->peerCertificate();
	const auto now = QDateTime::currentDateTimeUtc();
	if (certificate.isNull()
		|| certificate.digest(QCryptographicHash::Sha256).toHex() != _profile.certificateSha256
		|| !certificate.effectiveDate().isValid()
		|| !certificate.expiryDate().isValid()
		|| now < certificate.effectiveDate() || now > certificate.expiryDate()) {
		return false;
	}
	const auto errors = QSslCertificate::verify(
		_socket->peerCertificateChain(),
		_profile.endpoint.host());
	return std::all_of(errors.begin(), errors.end(), ExpectedTrustError);
}

void Resolver::sendRequest() {
	if (_sent || !_socket->isEncrypted() || !certificateAllowed()) {
		finish();
		return;
	}
	_sent = true;
	const auto body = QJsonDocument(QJsonObject{
		{ QLatin1String("version"), 1 },
		{ QLatin1String("phone"), _profile.phone },
	}).toJson(QJsonDocument::Compact);
	auto request = QByteArray("POST /v1/allowlist/resolve HTTP/1.1\r\nHost: ")
		+ _profile.endpoint.authority().toLatin1()
		+ "\r\nAuthorization: Bearer " + _profile.token
		+ "\r\nContent-Type: application/json\r\nConnection: close\r\nContent-Length: "
		+ QByteArray::number(body.size()) + "\r\n\r\n" + body;
	const auto written = _socket->write(request);
	request.fill('\0');
	_profile.token.clear();
	if (written != request.size()) {
		finish();
	}
}

void Resolver::readResponse() {
	if (!_sent) {
		finish();
		return;
	}
	_buffer += _socket->read(kMaximumResponseBytes + kMaximumHeaders + 1 - _buffer.size());
	if (_contentLength < 0) {
		const auto end = _buffer.indexOf("\r\n\r\n");
		if (end < 0) {
			if (_buffer.size() > kMaximumHeaders) {
				finish();
			}
			return;
		}
		if (end > kMaximumHeaders) {
			finish();
			return;
		}
		const auto lines = _buffer.left(end).split('\n');
		if (!lines[0].startsWith("HTTP/1.1 200 ")
			&& !lines[0].startsWith("HTTP/1.0 200 ")) {
			finish();
			return;
		}
		auto headers = QMap<QByteArray, QByteArray>();
		for (auto i = 1; i < lines.size(); ++i) {
			const auto line = lines[i].trimmed();
			const auto colon = line.indexOf(':');
			const auto key = line.left(colon).toLower();
			if (colon <= 0 || headers.contains(key)) {
				finish();
				return;
			}
			headers.insert(key, line.mid(colon + 1).trimmed());
		}
		const auto length = headers.value("content-length");
		auto valid = false;
		const auto number = length.toInt(&valid);
		if (!valid || QByteArray::number(number) != length
			|| number <= 0 || number > kMaximumResponseBytes
			|| headers.contains("transfer-encoding")
			|| headers.contains("content-encoding")
			|| headers.value("content-type").toLower() != "application/json") {
			finish();
			return;
		}
		_contentLength = number;
		_buffer.remove(0, end + 4);
	}
	if (_buffer.size() > _contentLength) {
		finish();
	} else if (_buffer.size() == _contentLength) {
		finish(ParseResponse(_buffer, _profile.phone));
	}
}

void Resolver::finish(Result result) {
	const auto done = std::move(_done);
	cancel();
	if (done) {
		done(std::move(result));
	}
}

} // namespace Main::Allowlist::Sheet

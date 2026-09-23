#pragma once

#include "main/allowlist_sheet.h"

#include <QtCore/QObject>
#include <QtCore/QTimer>
#include <functional>

class QSslSocket;

namespace Main::Allowlist::Sheet {

class Resolver final : public QObject {
public:
	using Callback = std::function<void(Result)>;

	explicit Resolver(QObject *parent = nullptr);
	~Resolver();
	void start(const QString &profilePath, const QString &phone, Callback done);
	void cancel();

private:
	[[nodiscard]] bool certificateAllowed() const;
	void sendRequest();
	void readResponse();
	void finish(Result result = {});

	QSslSocket *_socket = nullptr;
	QTimer _timer;
	Profile _profile;
	Callback _done;
	QByteArray _buffer;
	int _contentLength = -1;
	bool _sent = false;

};

} // namespace Main::Allowlist::Sheet

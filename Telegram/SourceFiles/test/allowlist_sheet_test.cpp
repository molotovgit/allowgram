#include "main/allowlist_sheet.h"
#include "main/allowlist_sheet_resolver.h"

#include <QtCore/QCoreApplication>
#include <QtCore/QElapsedTimer>
#include <QtCore/QFile>
#include <QtCore/QJsonArray>
#include <QtCore/QJsonDocument>
#include <QtCore/QJsonObject>
#include <QtCore/QTemporaryDir>
#include <QtNetwork/QSslSocket>
#include <iostream>
#include <memory>
#include <string>

namespace {

using namespace Main::Allowlist::Sheet;

auto failures = 0;
auto checks = 0;

void Check(bool passed, const char *name) {
	++checks;
	if (!passed) {
		++failures;
		std::cerr << "FAIL: " << name << '\n';
	}
}

QByteArray ProfileBytes() {
	return R"({"version":1,"endpoint":"https://localhost:4443/v1/allowlist/resolve","phone":"+998000000001","token":"AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA","certificate_sha256":"aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa"})";
}

QByteArray Matched() {
	return R"({"version":1,"status":"matched","phone":"+998000000001","user_ids":["201","202","203"],"group_ids":["-1000000000101","-1000000000102"]})";
}

int ProtocolTests() {
	const auto phone = QString::fromLatin1("+998000000001");
	Check(AuthenticatedPhone(QString::fromLatin1("998000000001")) == phone, "authenticated international digits");
	Check(AuthenticatedPhone(QString::fromLatin1("000000001")).isEmpty(), "no local inference for authenticated phone");
	Check(AuthenticatedPhone(QString::fromLatin1("+997000000001")) != phone, "country is part of identity");
	Check(AuthenticatedPhone(QString::fromLatin1("+998 00 000 00 01")) == phone, "spaced international digits");
	Check(AuthenticatedPhone(QString::fromLatin1("00 000 00 01")).isEmpty(), "leading-zero local stays empty");
	Check(AuthenticatedPhone(QString::fromLatin1("920436145")) == QString::fromLatin1("+998920436145"), "uzbekistan local nine digits");
	Check(AuthenticatedPhone(QString::fromLatin1("+998 92 043 61 45")) == QString::fromLatin1("+998920436145"), "spaced reported number");
	Check(bool(ParseProfile(ProfileBytes(), phone)), "valid deployment profile");
	Check(!ParseProfile(ProfileBytes(), QString()), "unsigned session rejected");
	Check(bool(ParseProfile(ProfileBytes(), QString::fromLatin1("+998000000002"))), "install token accepted for other session");
	auto profileCase = 0;
	for (const auto input : { QByteArray(), QByteArray("{"), QByteArray(16385, ' '),
		ProfileBytes().replace("\"version\":1", "\"version\":1,\"version\":1"),
		ProfileBytes().replace("\"version\":1", "\"version\":true"),
		ProfileBytes().replace("\"version\":1", "\"version\":1.0"),
		ProfileBytes().replace("\"version\":1", "\"version\":2"),
		ProfileBytes().replace("https:", "http:"),
		ProfileBytes().replace("localhost:4443", "user@localhost:4443"),
		ProfileBytes().replace("localhost:4443", "@localhost:4443"),
		ProfileBytes().replace("localhost:4443", "localhost:0"),
		ProfileBytes().replace("localhost:4443", "localhost:65536"),
		ProfileBytes().replace("localhost:4443", "localhost:"),
		ProfileBytes().replace("localhost:4443", "-bad:4443"),
		ProfileBytes().replace("resolve", "resolve?"),
		ProfileBytes().replace("resolve", "resolve#"),
		ProfileBytes().replace("resolve", "other"),
		ProfileBytes().replace("AAAAAAAAAAA", "BBBBBBBBBB="),
		ProfileBytes().replace("aaaaaaaaaaa", "AAAAAAAAAAA") }) {
		Check(!ParseProfile(input, phone),
			("invalid profile case " + std::to_string(++profileCase)).c_str());
	}
	auto directory = QTemporaryDir();
	const auto path = directory.filePath(QString::fromLatin1("access.json"));
	Check(!ReadProfile(path, phone), "missing access file fails closed");
	auto file = QFile(path);
	Check(file.open(QIODevice::WriteOnly), "temporary profile opens");
	Check(file.write(ProfileBytes()) == ProfileBytes().size(), "temporary profile writes");
	file.close();
	Check(bool(ReadProfile(path, phone)), "access file read");
	Check(file.open(QIODevice::WriteOnly | QIODevice::Truncate), "oversized file opens");
	Check(file.write(QByteArray(16385, ' ')) == 16385, "oversized file writes");
	file.close();
	Check(!ReadProfile(path, phone), "oversized file rejected");
	const auto result = ParseResponse(Matched(), phone);
	Check(result.status == Status::Matched && result.users.size() == 3 && result.groups.size() == 2, "known phone policy");
	Check(ParseResponse(Matched().replace("\"202\"", "\"201\""), phone).users.size() == 2, "duplicate typed IDs deduplicate");
	const auto absent = QByteArray(R"({"version":1,"status":"not_found","phone":"+998000000001"})");
	Check(ParseResponse(absent, phone).status == Status::NotFound, "only authoritative absent permits manual");
	for (const auto input : { QByteArray(), QByteArray("{}"), QByteArray(65537, ' '),
		Matched().chopped(1),
		Matched().replace("matched", "unknown"),
		Matched().replace("matched", "not_found"),
		Matched().replace("+998000000001", "+998000000002"),
		Matched().replace("\"version\":1", "\"version\":2"),
		Matched().replace("\"version\":1", "\"version\":true"),
		Matched().replace("\"version\":1", "\"version\":1.0"),
		Matched().replace("\"version\":1", "\"version\":1,\"ver\\u0073ion\":1"),
		Matched().replace("\"201\",\"202\",\"203\"", ""),
		Matched().replace("\"201\"", "201"),
		Matched().replace("\"201\"", "\"0201\""),
		Matched().replace("\"201\"", "\"-201\""),
		Matched().replace("\"201\"", "\"1e3\""),
		Matched().replace("\"201\"", "\"281474976710656\""),
		Matched().replace("\"201\"", "\"201 204\""),
		Matched().replace("\"201\"", "{}"),
		Matched().replace("\"201\"", "[\"201\"]"),
		Matched().replace("-1000000000101", "1000000000101"),
		Matched().replace("-1000000000101", "-1000000000000"),
		Matched().replace("-1000000000101", "channel:101"),
		absent.left(absent.size() - 1) + ",\"user_ids\":[]}" }) {
		Check(ParseResponse(input, phone).status == Status::Failed, "malformed response never permits manual");
	}
	std::cout << checks << " sheet protocol checks; " << failures << " failures\n";
	return failures ? 1 : 0;
}

} // namespace

int main(int argc, char *argv[]) {
	auto app = QCoreApplication(argc, argv);
	if (argc == 1) {
		return ProtocolTests();
	}
	if (argc < 4 || !QSslSocket::supportsSsl()) {
		std::cerr << "TLS backend or test arguments unavailable\n";
		return 2;
	}
	const auto path = QString::fromLocal8Bit(argv[2]);
	const auto phone = QString::fromLatin1(argv[3]);
	const auto mode = QByteArray(argv[1]);
	auto resolver = std::make_unique<Resolver>();
	auto elapsed = QElapsedTimer();
	elapsed.start();
	auto callbacks = 0;
	resolver->start(path, phone, [&](Result result) {
		++callbacks;
		const auto status = (result.status == Status::Matched) ? "matched"
			: (result.status == Status::NotFound) ? "not_found" : "failed";
		std::cout << QJsonDocument(QJsonObject{
			{ QLatin1String("status"), QLatin1String(status) },
			{ QLatin1String("users"), QJsonArray::fromStringList(result.users) },
			{ QLatin1String("groups"), QJsonArray::fromStringList(result.groups) },
			{ QLatin1String("elapsed_ms"), double(elapsed.elapsed()) },
		}).toJson(QJsonDocument::Compact).constData() << '\n';
		app.quit();
	});
	if (const auto socket = resolver->findChild<QSslSocket*>()) {
		QObject::connect(socket, &QSslSocket::sslErrors, &app,
			[&](const QList<QSslError> &errors) {
				std::cerr << "TLS verification codes:";
				for (const auto &error : errors) {
					std::cerr << ' ' << int(error.error());
				}
				std::cerr << '\n';
			});
		QObject::connect(socket, &QSslSocket::errorOccurred, &app,
			[&](QAbstractSocket::SocketError error) {
				std::cerr << "TLS socket code: " << int(error) << '\n';
			});
	}
	if (mode == "cancel" || mode == "destroy") {
		QTimer::singleShot(20, &app, [&] {
			if (mode == "cancel") {
				resolver->cancel();
			} else {
				resolver.reset();
			}
		});
		QTimer::singleShot(250, &app, [&] {
			std::cout << "{\"status\":\"cancelled\",\"callbacks\":" << callbacks << "}\n";
			app.exit(callbacks ? 1 : 0);
		});
	}
	QTimer::singleShot(12000, &app, [&] { app.exit(3); });
	return app.exec();
}

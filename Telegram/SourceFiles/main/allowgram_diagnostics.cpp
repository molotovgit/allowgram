/* Allowgram: logout diagnostics, kept on the computer and sent to the head. See LEGAL. */
#include "main/allowgram_diagnostics.h"

#include "core/version.h"
#include "settings.h"

#include <QtCore/QCoreApplication>
#include <QtCore/QDateTime>
#include <QtCore/QDir>
#include <QtCore/QFile>
#include <QtCore/QFileInfo>
#include <QtCore/QJsonDocument>
#include <QtCore/QJsonObject>
#include <QtCore/QPointer>
#include <QtCore/QRegularExpression>
#include <QtCore/QSaveFile>
#include <QtCore/QStandardPaths>
#include <QtCore/QTimer>
#include <QtNetwork/QNetworkAccessManager>
#include <QtNetwork/QNetworkReply>
#include <QtNetwork/QNetworkRequest>

#include <memory>

namespace Allowgram::Diagnostics {
namespace {

constexpr auto kEventsMax = qint64(600 * 1024);     // events.log is cut back to its newest half beyond this
constexpr auto kEventsKeep = qint64(300 * 1024);
constexpr auto kReportEvents = 400;                  // newest event lines in a report
constexpr auto kReportLogBytes = qint64(160 * 1024); // newest bytes of the app's own log.txt in a report
constexpr auto kPendingMax = 30;                     // reports waiting to be sent; the oldest go first
constexpr auto kRetryMs = 5 * 60 * 1000;
constexpr auto kSendTimeoutMs = 20000;
constexpr auto kReasonMaxAgeMs = qint64(2 * 60 * 1000); // a reason left by a cancelled logout must not stick

struct State {
	bool started = false;
	QString folder;
	QString logoutReason;
	qint64 logoutReasonAt = 0;
	QNetworkAccessManager *network = nullptr;
	QTimer *retry = nullptr;
	bool flushing = false;
};

State &S() {
	static auto state = State();
	return state;
}

QString Now() {
	return QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs);
}

QString ExePath() {
	return QDir::toNativeSeparators(cExeDir() + cExeName());
}

QString WorkingDir() {
	return QDir::toNativeSeparators(QDir(cWorkingDir()).absolutePath());
}

bool Portable() {
	return QDir(cExeDir() + u"AllowgramForcePortable"_q).exists();
}

bool SameCopy(const QString &a, const QString &b) {
	return QDir::cleanPath(QDir::fromNativeSeparators(a)).compare(
		QDir::cleanPath(QDir::fromNativeSeparators(b)),
		Qt::CaseInsensitive) == 0;
}

// A short name for the running copy in event lines: its folder, as recruiters see it.
QString CopyName() {
	return QDir::toNativeSeparators(QDir(cExeDir()).absolutePath());
}

QString StatePath() {
	return S().folder + u"state.json"_q;
}

QString EventsPath() {
	return S().folder + u"events.log"_q;
}

QString PendingDir() {
	return S().folder + u"pending/"_q;
}

QJsonObject ReadState() {
	if (S().folder.isEmpty()) {
		return {};
	}
	auto file = QFile(StatePath());
	if (!file.open(QIODevice::ReadOnly)) {
		return {};
	}
	const auto document = QJsonDocument::fromJson(file.read(64 * 1024));
	return document.isObject() ? document.object() : QJsonObject();
}

void WriteState(const QJsonObject &state) {
	if (S().folder.isEmpty()) {
		return;
	}
	auto file = QSaveFile(StatePath());
	if (file.open(QIODevice::WriteOnly)) {
		file.write(QJsonDocument(state).toJson(QJsonDocument::Compact));
		file.commit();
	}
}

void AppendEvent(const QString &text) {
	if (S().folder.isEmpty()) {
		return;
	}
	auto file = QFile(EventsPath());
	if (file.size() > kEventsMax && file.open(QIODevice::ReadOnly)) {
		file.seek(file.size() - kEventsKeep);
		auto tail = file.readAll();
		file.close();
		const auto cut = tail.indexOf('\n');
		if (cut >= 0) tail = tail.mid(cut + 1);
		auto rewrite = QSaveFile(EventsPath());
		if (rewrite.open(QIODevice::WriteOnly)) {
			rewrite.write(tail);
			rewrite.commit();
		}
	}
	if (file.open(QIODevice::Append)) {
		const auto line = u"%1 | pid %2 | %3 | v%4 | %5\n"_q.arg(
			Now(),
			QString::number(QCoreApplication::applicationPid()),
			CopyName(),
			QString::fromLatin1(AppVersionStr),
			Mask(text).simplified());
		file.write(line.toUtf8());
	}
}

QString TailLines(const QString &path, int lines, qint64 maxBytes) {
	auto file = QFile(path);
	if (!file.open(QIODevice::ReadOnly)) {
		return {};
	}
	const auto from = std::max(qint64(0), file.size() - maxBytes);
	file.seek(from);
	auto text = QString::fromUtf8(file.readAll());
	if (from > 0) {
		const auto cut = text.indexOf('\n');
		if (cut >= 0) text = text.mid(cut + 1);
	}
	auto list = text.split('\n', Qt::SkipEmptyParts);
	if (list.size() > lines) {
		list = list.mid(list.size() - lines);
	}
	return list.join('\n');
}

QJsonObject Report(const QString &kind, const QString &reason, const QJsonObject &previous) {
	return QJsonObject{
		{ u"v"_q, 1 },
		{ u"kind"_q, kind },
		{ u"reason"_q, Mask(reason).left(500) },
		{ u"at"_q, QDateTime::currentSecsSinceEpoch() },
		{ u"client_version"_q, QString::fromLatin1(AppVersionStr) },
		{ u"exe_path"_q, ExePath() },
		{ u"working_dir"_q, WorkingDir() },
		{ u"portable"_q, Portable() },
		{ u"previous"_q, previous },
		{ u"events"_q, TailLines(EventsPath(), kReportEvents, 256 * 1024) },
		{ u"log_tail"_q, Mask(TailLines(cWorkingDir() + u"log.txt"_q, 100000, kReportLogBytes)) },
	};
}

// What the previous state says about the last signed-in account on this computer, without its token.
QJsonObject Previous(const QJsonObject &state) {
	auto result = QJsonObject();
	for (auto i = state.begin(); i != state.end(); ++i) {
		if (i.key() == u"token"_q) {
			continue;
		}
		result.insert(i.key(), i.value().isString() ? QJsonValue(Mask(i.value().toString())) : i.value());
	}
	return result;
}

void Flush(std::function<void(bool)> done = nullptr);

void Queue(QJsonObject report, std::function<void(bool)> done = nullptr) {
	if (S().folder.isEmpty()) {
		if (done) done(false);
		return;
	}
	QDir().mkpath(PendingDir());
	static auto counter = 0;
	const auto name = u"%1-%2-%3.json"_q.arg(
		QString::number(QDateTime::currentMSecsSinceEpoch()),
		QString::number(QCoreApplication::applicationPid()),
		QString::number(++counter).rightJustified(4, QChar('0')));
	auto file = QSaveFile(PendingDir() + name);
	if (file.open(QIODevice::WriteOnly)) {
		file.write(QJsonDocument(report).toJson(QJsonDocument::Compact));
		file.commit();
	}
	auto names = QDir(PendingDir()).entryList({ u"*.json"_q }, QDir::Files, QDir::Name);
	while (names.size() > kPendingMax) {
		QFile::remove(PendingDir() + names.takeFirst());
	}
	Flush(std::move(done));
}

// Sends the waiting reports one by one, oldest first, with the last known head device token of this computer.
// The head keeps a report for a revoked device too: a revoke is exactly when one may be needed.
void Flush(std::function<void(bool)> done) {
	auto &s = S();
	const auto finish = [=](bool sent) { if (done) done(sent); };
	if (s.flushing || !s.network) {
		finish(false);
		return;
	}
	const auto state = ReadState();
	const auto origin = QUrl(state.value(u"origin"_q).toString());
	const auto token = state.value(u"token"_q).toString().toLatin1();
	const auto names = QDir(PendingDir()).entryList({ u"*.json"_q }, QDir::Files, QDir::Name);
	if (names.isEmpty()) {
		finish(true);
		return;
	}
	if (!origin.isValid() || origin.scheme() != u"https"_q || token.size() < 32) {
		AppendEvent(u"diagnostics: %1 report(s) waiting - no head device known on this computer yet"_q.arg(names.size()));
		finish(false);
		return;
	}
	auto file = QFile(PendingDir() + names.front());
	if (!file.open(QIODevice::ReadOnly)) {
		QFile::remove(PendingDir() + names.front());
		finish(false);
		return;
	}
	const auto body = file.read(900 * 1024);
	file.close();
	auto request = QNetworkRequest(origin.resolved(QUrl(u"/api/client/diagnostics"_q)));
	request.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::ManualRedirectPolicy);
	request.setTransferTimeout(kSendTimeoutMs);
	request.setHeader(QNetworkRequest::ContentTypeHeader, u"application/json"_q);
	request.setRawHeader("Accept", "application/json");
	request.setRawHeader("Authorization", QByteArray("Bearer ") + token);
	s.flushing = true;
	const auto reply = s.network->post(request, body);
	const auto name = names.front();
	QObject::connect(reply, &QNetworkReply::finished, reply, [=] {
		S().flushing = false;
		reply->deleteLater();
		const auto status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
		if (reply->error() == QNetworkReply::NoError && status >= 200 && status < 300) {
			QFile::remove(PendingDir() + name);
			AppendEvent(u"diagnostics: report %1 stored by the head"_q.arg(name));
			Flush(done);
		} else if (status == 400 || status == 413 || status == 422) {
			// The head will never take this one; keep the rest moving.
			QFile::remove(PendingDir() + name);
			AppendEvent(u"diagnostics: report %1 refused by the head (%2), dropped"_q.arg(name).arg(status));
			Flush(done);
		} else {
			AppendEvent(u"diagnostics: sending failed (%1 %2), will retry"_q.arg(status).arg(reply->errorString()));
			finish(false);
		}
	});
}

void EnsureStarted() {
	auto &s = S();
	if (s.started || !QCoreApplication::instance()) {
		return;
	}
	s.started = true;
	const auto base = QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation);
	if (base.isEmpty()) {
		return;
	}
	s.folder = QDir(base).absolutePath() + u"/AllowgramDiagnostics/"_q;
	QDir().mkpath(s.folder);
	s.network = new QNetworkAccessManager(QCoreApplication::instance());
	s.retry = new QTimer(QCoreApplication::instance());
	QObject::connect(s.retry, &QTimer::timeout, s.retry, [] { Flush(); });
	s.retry->start(kRetryMs);
	AppendEvent(u"start: exe %1, data folder %2, %3"_q.arg(
		ExePath(),
		WorkingDir(),
		Portable() ? u"portable copy"_q : u"installed copy"_q));
}

} // namespace

QString Mask(const QString &text) {
	static const auto phone = QRegularExpression(u"\\+\\d[\\d \\-()]{6,}\\d"_q);
	static const auto code = QRegularExpression(u"(?<![\\d.:])\\d{5,6}(?![\\d.:])"_q);
	auto result = text;
	result.replace(phone, u"+[phone]"_q);
	result.replace(code, u"[code]"_q);
	return result;
}

void Start() {
	EnsureStarted();
	Flush();
}

void Event(const QString &text) {
	EnsureStarted();
	AppendEvent(text);
}

void RememberDevice(
		quint64 userId,
		const QUrl &origin,
		const QString &deviceId,
		const QByteArray &token) {
	EnsureStarted();
	// Only the device: which copy ran (exe, folder, signed in) is AccountsLoaded()'s to record, after it has
	// compared the copy with the one used last. A Session (and so this call) is created before that check.
	auto state = ReadState();
	const auto changed = state.value(u"device_id"_q).toString() != deviceId;
	state[u"user_id"_q] = QString::number(userId);
	state[u"origin"_q] = origin.toString(QUrl::FullyEncoded);
	state[u"device_id"_q] = deviceId;
	state[u"token"_q] = QString::fromLatin1(token);
	state[u"device_at"_q] = QDateTime::currentSecsSinceEpoch();
	WriteState(state);
	if (changed) {
		AppendEvent(u"head device %1 for account %2"_q.arg(deviceId, QString::number(userId)));
	}
	Flush(); // reports that were waiting for a device can go now
}

void SetLogoutReason(const QString &reason) {
	EnsureStarted();
	S().logoutReason = reason;
	S().logoutReasonAt = QDateTime::currentMSecsSinceEpoch();
}

void LoggedOut(quint64 userId) {
	EnsureStarted();
	const auto fresh = !S().logoutReason.isEmpty()
		&& QDateTime::currentMSecsSinceEpoch() - S().logoutReasonAt <= kReasonMaxAgeMs;
	const auto reason = Mask(fresh
		? S().logoutReason
		: u"unknown (no reason recorded by the caller)"_q);
	S().logoutReason = QString();
	AppendEvent(u"LOGOUT account %1: %2"_q.arg(QString::number(userId), reason));
	auto state = ReadState();
	const auto previous = Previous(state);
	state[u"signed_in"_q] = false;
	state[u"logged_out_at"_q] = QDateTime::currentSecsSinceEpoch();
	state[u"logout_reason"_q] = reason;
	WriteState(state);
	auto report = Report(u"logout"_q, reason, previous);
	report[u"user_id"_q] = QString::number(userId);
	Queue(std::move(report));
}

void AccountsLoaded(bool anySignedIn, quint64 activeUserId) {
	EnsureStarted();
	auto state = ReadState();
	const auto wasSignedIn = state.value(u"signed_in"_q).toBool();
	const auto previousExe = state.value(u"exe"_q).toString();
	if (anySignedIn) {
		AppendEvent(u"signed in as %1"_q.arg(QString::number(activeUserId)));
		if (!previousExe.isEmpty() && !SameCopy(previousExe, ExePath())) {
			// Another Allowgram copy on this computer was used last: each copy keeps its own login.
			auto report = Report(u"another_copy"_q,
				u"this copy is signed in, but the last copy used on this computer was %1"_q.arg(previousExe),
				Previous(state));
			report[u"user_id"_q] = QString::number(activeUserId);
			Queue(std::move(report));
		}
		state[u"signed_in"_q] = true;
		state[u"exe"_q] = ExePath();
		state[u"working_dir"_q] = WorkingDir();
		state[u"portable"_q] = Portable();
		state[u"version"_q] = QString::fromLatin1(AppVersionStr);
		state[u"updated_at"_q] = QDateTime::currentSecsSinceEpoch();
		WriteState(state);
	} else {
		AppendEvent(u"started with no signed-in account"_q);
		if (wasSignedIn) {
			// The computer had a signed-in account and this start has none: a logout nobody recorded
			// (local data removed, a different or freshly extracted copy, a zip opened without extracting).
			const auto same = SameCopy(previousExe, ExePath());
			auto report = Report(u"login_missing"_q,
				same ? u"same copy, its saved login is gone"_q
					: u"a different copy started: %1 (the signed-in one was %2)"_q.arg(ExePath(), previousExe),
				Previous(state));
			report[u"user_id"_q] = state.value(u"user_id"_q).toString();
			state[u"signed_in"_q] = false;
			WriteState(state);
			Queue(std::move(report));
		}
	}
	Flush();
}

void SendNow(const QString &note, std::function<void(bool sent)> done) {
	EnsureStarted();
	AppendEvent(u"manual diagnostics: %1"_q.arg(note));
	auto state = ReadState();
	auto report = Report(u"manual"_q, note, Previous(state));
	report[u"user_id"_q] = state.value(u"user_id"_q).toString();
	Queue(std::move(report), std::move(done));
}

} // namespace Allowgram::Diagnostics

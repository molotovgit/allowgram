/* Allowgram: logout diagnostics, kept on the computer and sent to the head. See LEGAL. */
#pragma once

#include <QtCore/QByteArray>
#include <QtCore/QString>
#include <QtCore/QUrl>

#include <functional>

// Why a recruiter gets "logged out" is invisible from the servers: Telegram ends the session, the user presses
// Log out, or a different Allowgram copy (another folder, a portable zip, a zip opened without extracting) starts
// with no login at all. These diagnostics are kept in ONE machine-wide folder that every Allowgram copy on the
// computer shares (%LOCALAPPDATA%\AllowgramDiagnostics), so the next start - of any copy - can say what happened.
//
// A report goes to the head with the head device token of the last signed-in account on this computer; reports
// that cannot be sent yet wait in the folder and are retried. Reports carry the app version, which copy and
// folder ran, the logout reason and recent app log lines with phone numbers and login codes masked. Never
// message text, never the Telegram login itself.
namespace Allowgram::Diagnostics {

// Once, early at startup, after the working folder is known.
void Start();

// A line in the machine-wide diagnostics log.
void Event(const QString &text);

// The head device of a signed-in account; kept so a report can still be sent after that account is gone.
void RememberDevice(
	quint64 userId,
	const QUrl &origin,
	const QString &deviceId,
	const QByteArray &token);

// The reason for the logout that is about to happen; consumed by LoggedOut().
void SetLogoutReason(const QString &reason);

// An account is being logged out, before its local data is removed.
void LoggedOut(quint64 userId);

// After the accounts are read at startup: is any account signed in in THIS copy?
void AccountsLoaded(bool anySignedIn, quint64 activeUserId);

// Settings: send the diagnostics now. done(true) once the head stored it, done(false) if it is queued instead.
void SendNow(const QString &note, std::function<void(bool sent)> done);

// For tests: phone numbers and login codes masked.
[[nodiscard]] QString Mask(const QString &text);

} // namespace Allowgram::Diagnostics

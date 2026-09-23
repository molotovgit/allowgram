#pragma once

#include <QtCore/QByteArray>
#include <QtCore/QString>
#include <QtCore/QStringList>
#include <QtCore/QUrl>
#include <optional>

namespace Main::Allowlist::Sheet {

inline constexpr auto kMaximumProfileBytes = 16 * 1024;
inline constexpr auto kMaximumResponseBytes = 64 * 1024;
inline constexpr auto kTimeoutMs = 10000;

enum class Status { Failed, Matched, NotFound };

struct Profile {
	QUrl endpoint;
	QString phone;
	QByteArray token;
	QByteArray certificateSha256;
};

struct Result {
	Status status = Status::Failed;
	QStringList users;
	QStringList groups;
};

[[nodiscard]] QString AuthenticatedPhone(const QString &phone);
[[nodiscard]] std::optional<Profile> ParseProfile(
	const QByteArray &bytes,
	const QString &phone);
[[nodiscard]] std::optional<Profile> ReadProfile(
	const QString &path,
	const QString &phone);
[[nodiscard]] Result ParseResponse(
	const QByteArray &bytes,
	const QString &phone);

} // namespace Main::Allowlist::Sheet

/* Allowgram: signed head-managed policies. Upstream license: see LEGAL. */
#pragma once

#include <QtCore/QByteArray>
#include <QtCore/QJsonObject>
#include <QtCore/QString>
#include <QtCore/QUrl>
#include <optional>
#include <vector>

namespace Main::Managed {
inline constexpr auto MaxPolicyBytes = 1024 * 1024;
inline constexpr auto MaxWireBytes = 2 * MaxPolicyBytes;

struct Peer {
	QString kind;
	quint64 id = 0;
};
struct Invitation {
	QUrl origin;
	QString subject;
	QByteArray code;
	QByteArray publicKey;
};
struct Policy {
	QString subject;
	QString device;
	qint64 revision = 0;
	QByteArray bytes;
	QByteArray digest;
	std::vector<Peer> peers;
};
struct State {
	QUrl origin;
	QString subject;
	QString device;
	QByteArray publicKey;
	QByteArray token;
	QByteArray envelope;
};

[[nodiscard]] std::optional<QJsonObject> ReadObject(const QByteArray &bytes);
[[nodiscard]] std::optional<Invitation> ReadInvitation(
	const QString &text, const QString &subject);
[[nodiscard]] std::optional<Policy> VerifyPolicy(
	const QByteArray &envelope, const QByteArray &publicKey,
	const QString &subject, const QString &device, qint64 floor,
	const QByteArray &previousBytes, qint64 now);
[[nodiscard]] std::optional<State> ReadEnrollment(
	const QByteArray &response, const Invitation &invitation, qint64 now);
[[nodiscard]] QByteArray SaveState(const State &state);
[[nodiscard]] std::optional<State> ReadState(
	const QByteArray &bytes, const QString &subject, qint64 now);
} // namespace Main::Managed

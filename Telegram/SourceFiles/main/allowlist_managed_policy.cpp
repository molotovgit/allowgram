/* Allowgram: signed head-managed policies. Upstream license: see LEGAL. */
#include "main/allowlist_managed_policy.h"
#include <QtCore/QCryptographicHash>
#include <QtCore/QJsonArray>
#include <QtCore/QJsonDocument>
#include <QtCore/QSet>
#include <openssl/evp.h>
#include <cmath>
#include <memory>

namespace Main::Managed {
namespace {
constexpr auto MaxId = quint64(281474976710655ULL);
constexpr auto MaxInteger = qint64(9007199254740991LL);

// Qt's DOM parser intentionally accepts duplicate keys. Reject them first,
// including escaped spellings and nested objects; never normalize a signed body.
class UniqueKeys final {
public:
	explicit UniqueKeys(const QByteArray &bytes) : _bytes(bytes) {}
	bool valid() { return value(0) && (space(), _at == _bytes.size()); }
private:
	void space() {
		while (_at < _bytes.size() && (_bytes[_at] == ' ' || _bytes[_at] == '\n'
			|| _bytes[_at] == '\r' || _bytes[_at] == '\t')) ++_at;
	}
	bool take(char ch) {
		space();
		if (_at >= _bytes.size() || _bytes[_at] != ch) return false;
		++_at;
		return true;
	}
	std::optional<QString> string() {
		space();
		const auto start = _at;
		if (!take('"')) return std::nullopt;
		while (_at < _bytes.size()) {
			const auto ch = _bytes[_at++];
			if (ch == '\\') {
				if (_at == _bytes.size()) return std::nullopt;
				++_at;
			} else if (ch == '"') {
				const auto doc = QJsonDocument::fromJson(
					QByteArray("[") + _bytes.mid(start, _at - start) + ']');
				if (!doc.isArray() || doc.array().size() != 1
					|| !doc.array()[0].isString()) return std::nullopt;
				return doc.array()[0].toString();
			}
		}
		return std::nullopt;
	}
	bool value(int depth) {
		space();
		if (depth > 16 || _at >= _bytes.size()) return false;
		if (_bytes[_at] == '{') {
			++_at;
			QSet<QString> seen;
			if (take('}')) return true;
			do {
				const auto key = string();
				if (!key || seen.contains(*key) || seen.size() >= 32) return false;
				seen.insert(*key);
				if (!take(':') || !value(depth + 1)) return false;
				if (take('}')) return true;
			} while (take(','));
			return false;
		} else if (_bytes[_at] == '[') {
			++_at;
			if (take(']')) return true;
			do {
				if (!value(depth + 1)) return false;
				if (take(']')) return true;
			} while (take(','));
			return false;
		} else if (_bytes[_at] == '"') {
			return string().has_value();
		}
		const auto start = _at;
		while (_at < _bytes.size() && _bytes[_at] != ',' && _bytes[_at] != '}'
			&& _bytes[_at] != ']' && _bytes[_at] != ' ' && _bytes[_at] != '\n'
			&& _bytes[_at] != '\r' && _bytes[_at] != '\t') ++_at;
		return _at > start; // Full JSON grammar was checked by Qt beforehand.
	}
	const QByteArray &_bytes;
	qsizetype _at = 0;
};

bool Fields(const QJsonObject &object, std::initializer_list<const char*> fields) {
	if (object.size() != qsizetype(fields.size())) return false;
	for (const auto field : fields) if (!object.contains(QLatin1String(field))) return false;
	return true;
}
QByteArray Decode(const QString &text) {
	if (text.isEmpty() || text.size() > MaxWireBytes) return {};
	const auto bytes = text.toLatin1();
	for (const auto ch : bytes) {
		if (!((ch >= 'a' && ch <= 'z') || (ch >= 'A' && ch <= 'Z')
			|| (ch >= '0' && ch <= '9') || ch == '_' || ch == '-')) return {};
	}
	const auto raw = QByteArray::fromBase64(bytes, QByteArray::Base64UrlEncoding
		| QByteArray::AbortOnBase64DecodingErrors);
	if (raw.toBase64(QByteArray::Base64UrlEncoding | QByteArray::OmitTrailingEquals) != bytes) return {};
	return raw;
}
QString Encode(const QByteArray &bytes) {
	return QString::fromLatin1(bytes.toBase64(QByteArray::Base64UrlEncoding | QByteArray::OmitTrailingEquals));
}
std::optional<quint64> Id(const QJsonValue &value) {
	if (!value.isString()) return std::nullopt;
	const auto text = value.toString();
	if (text.isEmpty() || text.size() > 15 || text.front() == u'0') return std::nullopt;
	for (const auto ch : text) if (ch < u'0' || ch > u'9') return std::nullopt;
	bool ok = false;
	const auto id = text.toULongLong(&ok);
	return (ok && id > 0 && id <= MaxId) ? std::optional<quint64>(id) : std::nullopt;
}
std::optional<qint64> Integer(const QJsonValue &value, qint64 minimum = 1) {
	if (!value.isDouble()) return std::nullopt;
	const auto number = value.toDouble();
	if (!std::isfinite(number) || number < double(minimum) || number > double(MaxInteger)
		|| std::floor(number) != number) return std::nullopt;
	return qint64(number);
}
bool Token(const QString &value) {
	if (value.size() < 40 || value.size() > 128) return false;
	for (const auto ch : value) {
		if (!((ch >= u'a' && ch <= u'z') || (ch >= u'A' && ch <= u'Z')
			|| (ch >= u'0' && ch <= u'9') || ch == u'_' || ch == u'-')) return false;
	}
	return true;
}
bool Device(const QString &value) {
	if (value.size() != 32) return false;
	for (const auto ch : value) if (!((ch >= u'0' && ch <= u'9') || (ch >= u'a' && ch <= u'f'))) return false;
	return true;
}
std::optional<QUrl> Origin(const QString &value) {
	const auto url = QUrl(value, QUrl::StrictMode);
	if (value.size() > 255 || !url.isValid() || url.scheme() != QStringLiteral("https")
		|| url.host().isEmpty() || !url.userInfo().isEmpty() || url.hasQuery() || url.hasFragment()
		|| (!url.path().isEmpty() && url.path() != QStringLiteral("/"))
		|| url.toString(QUrl::FullyEncoded) != value || url.port() == 0) return std::nullopt;
	auto result = url;
	result.setPath(QString());
	return result;
}
bool Signature(const QByteArray &keyBytes, const QByteArray &bytes, const QByteArray &signature) {
	if (keyBytes.size() != 32 || signature.size() != 64) return false;
	const auto key = std::unique_ptr<EVP_PKEY, decltype(&EVP_PKEY_free)>(
		EVP_PKEY_new_raw_public_key(EVP_PKEY_ED25519, nullptr,
			reinterpret_cast<const unsigned char*>(keyBytes.constData()), size_t(keyBytes.size())),
		&EVP_PKEY_free);
	const auto context = std::unique_ptr<EVP_MD_CTX, decltype(&EVP_MD_CTX_free)>(EVP_MD_CTX_new(), &EVP_MD_CTX_free);
	if (!key || !context || EVP_DigestVerifyInit(context.get(), nullptr, nullptr, nullptr, key.get()) != 1) return false;
	const auto message = QByteArray("ALLOWGRAM_HEAD_POLICY_V1\n") + bytes;
	return EVP_DigestVerify(context.get(), reinterpret_cast<const unsigned char*>(signature.constData()),
		size_t(signature.size()), reinterpret_cast<const unsigned char*>(message.constData()), size_t(message.size())) == 1;
}
} // namespace

std::optional<QJsonObject> ReadObject(const QByteArray &bytes) {
	if (bytes.isEmpty() || bytes.size() > MaxWireBytes) return std::nullopt;
	QJsonParseError error;
	const auto document = QJsonDocument::fromJson(bytes, &error);
	if (error.error != QJsonParseError::NoError || !document.isObject() || !UniqueKeys(bytes).valid()) return std::nullopt;
	return document.object();
}

std::optional<Invitation> ReadInvitation(const QString &text, const QString &subject) {
	if (text.size() > 4096 || !text.startsWith(QStringLiteral("AGH1."))) return std::nullopt;
	const auto object = ReadObject(Decode(text.mid(5)));
	if (!object || !Fields(*object,{"v","url","telegram_user_id","code","public_key"})
		|| Integer(object->value("v")) != 1 || !Id(object->value("telegram_user_id"))
		|| object->value("telegram_user_id").toString() != subject
		|| !Token(object->value("code").toString())) return std::nullopt;
	const auto origin = Origin(object->value("url").toString());
	const auto key = Decode(object->value("public_key").toString());
	if (!origin || key.size() != 32) return std::nullopt;
	return Invitation{*origin,subject,object->value("code").toString().toLatin1(),key};
}

std::optional<Policy> VerifyPolicy(const QByteArray &envelope, const QByteArray &publicKey,
		const QString &subject, const QString &device, qint64 floor,
		const QByteArray &previousBytes, qint64 now) {
	if (floor < 0 || !Id(QJsonValue(subject)) || !Device(device)) return std::nullopt;
	const auto outer = ReadObject(envelope);
	if (!outer || !Fields(*outer,{"signed","signature"})) return std::nullopt;
	const auto raw = Decode(outer->value("signed").toString());
	const auto signature = Decode(outer->value("signature").toString());
	if (raw.size() > MaxPolicyBytes || !Signature(publicKey,raw,signature)) return std::nullopt;
	const auto object = ReadObject(raw);
	if (!object || !Fields(*object,{"v","telegram_user_id","device_id","revision","issued_at","peers"})
		|| Integer(object->value("v")) != 1 || object->value("telegram_user_id").toString() != subject
		|| object->value("device_id").toString() != device || !object->value("peers").isArray()) return std::nullopt;
	const auto revision = Integer(object->value("revision"));
	const auto issued = Integer(object->value("issued_at"));
	if (!revision || !issued || *issued > now + 300 || *revision < floor
		|| (*revision == floor && raw != previousBytes)) return std::nullopt;
	const auto array = object->value("peers").toArray();
	if (array.size() > 10000) return std::nullopt;
	Policy result{subject,device,*revision,raw,QCryptographicHash::hash(raw,QCryptographicHash::Sha256).toHex(),{}};
	QSet<QString> seen;
	for (const auto &value : array) {
		if (!value.isObject()) return std::nullopt;
		const auto peer = value.toObject();
		if (!Fields(peer,{"kind","id"})) return std::nullopt;
		const auto kind = peer.value("kind").toString();
		const auto id = Id(peer.value("id"));
		if (!id || (kind != QStringLiteral("user") && kind != QStringLiteral("chat")
			&& kind != QStringLiteral("channel"))) return std::nullopt;
		const auto typed = kind + QLatin1Char(':') + peer.value("id").toString();
		if (seen.contains(typed)) return std::nullopt;
		seen.insert(typed);
		result.peers.push_back({kind,*id});
	}
	return result;
}

std::optional<State> ReadEnrollment(const QByteArray &response, const Invitation &invitation, qint64 now) {
	const auto object = ReadObject(response);
	if (!object || !Fields(*object,{"device_id","device_token","policy"})
		|| !Device(object->value("device_id").toString()) || !Token(object->value("device_token").toString())
		|| !object->value("policy").isObject()) return std::nullopt;
	State result{invitation.origin,invitation.subject,object->value("device_id").toString(),
		invitation.publicKey,object->value("device_token").toString().toLatin1(),
		QJsonDocument(object->value("policy").toObject()).toJson(QJsonDocument::Compact)};
	if (!VerifyPolicy(result.envelope,result.publicKey,result.subject,result.device,0,{},now)) return std::nullopt;
	return result;
}

QByteArray SaveState(const State &state) {
	return QJsonDocument(QJsonObject{{"v",1},{"origin",state.origin.toString(QUrl::FullyEncoded)},
		{"subject",state.subject},{"device",state.device},{"public_key",Encode(state.publicKey)},
		{"token",QString::fromLatin1(state.token)},{"policy",QString::fromUtf8(state.envelope)}}).toJson(QJsonDocument::Compact);
}

std::optional<State> ReadState(const QByteArray &bytes, const QString &subject, qint64 now) {
	const auto object = ReadObject(bytes);
	if (!object || !Fields(*object,{"v","origin","subject","device","public_key","token","policy"})
		|| Integer(object->value("v")) != 1 || object->value("subject").toString() != subject
		|| !Device(object->value("device").toString()) || !Token(object->value("token").toString())) return std::nullopt;
	const auto origin = Origin(object->value("origin").toString());
	const auto key = Decode(object->value("public_key").toString());
	if (!origin || key.size() != 32) return std::nullopt;
	State result{*origin,subject,object->value("device").toString(),key,
		object->value("token").toString().toLatin1(),object->value("policy").toString().toUtf8()};
	if (!VerifyPolicy(result.envelope,key,subject,result.device,0,{},now)) return std::nullopt;
	return result;
}
} // namespace Main::Managed

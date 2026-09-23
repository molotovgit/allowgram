#include "main/allowlist_sheet.h"

#include "main/allowlist_policy.h"

#include <QtCore/QFile>
#include <QtCore/QFileInfo>
#include <QtCore/QJsonArray>
#include <QtCore/QJsonDocument>
#include <QtCore/QJsonObject>
#include <QtCore/QRegularExpression>
#include <QtCore/QSet>

namespace Main::Allowlist::Sheet {
namespace {

[[nodiscard]] bool Matches(const QString &value, const char *pattern) {
	return QRegularExpression(QString::fromLatin1(pattern)).match(value).hasMatch();
}

[[nodiscard]] std::optional<QJsonObject> ReadObject(
		const QByteArray &bytes,
		int limit) {
	if (bytes.isEmpty() || bytes.size() > limit
		|| QString::fromUtf8(bytes).toUtf8() != bytes) {
		return {};
	}
	const auto document = QJsonDocument::fromJson(bytes);
	if (!document.isObject()) {
		return {};
	}
	auto keys = QSet<QString>();
	auto offset = qsizetype(0);
	const auto space = [&] {
		while (offset < bytes.size() && QByteArray(" \r\n\t").contains(bytes[offset])) {
			++offset;
		}
	};
	const auto stringEnd = [&] {
		if (bytes[offset++] != '"') {
			return false;
		}
		while (offset < bytes.size()) {
			const auto ch = bytes[offset++];
			if (ch == '"') {
				return true;
			} else if (ch == '\\') {
				++offset;
			}
		}
		return false;
	};
	space();
	++offset;
	space();
	while (offset < bytes.size() && bytes[offset] != '}') {
		const auto start = offset;
		if (!stringEnd()) {
			return {};
		}
		const auto key = QJsonDocument::fromJson(
			'[' + bytes.mid(start, offset - start) + ']').array()[0].toString();
		if (keys.contains(key)) {
			return {};
		}
		keys.insert(key);
		space();
		if (offset >= bytes.size() || bytes[offset++] != ':') {
			return {};
		}
		space();
		const auto valueStart = offset;
		auto depth = 0;
		while (offset < bytes.size()) {
			const auto ch = bytes[offset];
			if (ch == '"') {
				if (!stringEnd()) {
					return {};
				}
				continue;
			} else if (ch == '{') {
				return {};
			} else if (ch == '[') {
				if (++depth > 1) {
					return {};
				}
			} else if (ch == ']') {
				--depth;
			} else if (!depth && (ch == ',' || ch == '}')) {
				break;
			}
			++offset;
		}
		if (key == QLatin1String("version")
			&& bytes.mid(valueStart, offset - valueStart).trimmed() != "1") {
			return {};
		}
		if (offset < bytes.size() && bytes[offset] == ',') {
			++offset;
			space();
		}
	}
	return document.object();
}

[[nodiscard]] bool Keys(
		const QJsonObject &object,
		std::initializer_list<const char*> expected) {
	if (object.size() != qsizetype(expected.size())) {
		return false;
	}
	for (const auto key : expected) {
		if (!object.contains(QLatin1String(key))) {
			return false;
		}
	}
	return true;
}

[[nodiscard]] bool ReadIds(
		const QJsonValue &value,
		bool groups,
		QStringList &result) {
	if (!value.isArray()) {
		return false;
	}
	const auto array = value.toArray();
	if (array.isEmpty() || array.size() > kMaximumEntries) {
		return false;
	}
	auto seen = QSet<QString>();
	for (const auto id : array) {
		const auto text = id.toString();
		if (!id.isString() || text.size() > 17
			|| !Matches(text, groups ? "\\A-[1-9][0-9]*\\z" : "\\A[1-9][0-9]*\\z")) {
			return false;
		}
		if (!seen.contains(text)) {
			seen.insert(text);
			result.push_back(text);
		}
	}
	return true;
}

} // namespace

QString AuthenticatedPhone(const QString &phone) {
	auto digits = QString();
	digits.reserve(phone.size());
	for (const auto ch : phone) {
		if (ch >= QChar('0') && ch <= QChar('9')) {
			digits.append(ch);
		}
	}
	if (Matches(digits, "\\A[1-9][0-9]{8}\\z")) {
		digits = QString::fromLatin1("998") + digits;
	}
	const auto result = '+' + digits;
	return Matches(result, "\\A\\+[1-9][0-9]{7,14}\\z") ? result : QString();
}

std::optional<Profile> ParseProfile(
		const QByteArray &bytes,
		const QString &phone) {
	if (phone.isEmpty() || AuthenticatedPhone(phone) != phone) {
		return {};
	}
	const auto object = ReadObject(bytes, kMaximumProfileBytes);
	if (!object || !Keys(*object, {
		"version", "endpoint", "phone", "token", "certificate_sha256" })) {
		return {};
	}
	const auto field = [&](const char *name) {
		return object->value(QLatin1String(name)).toString();
	};
	if (AuthenticatedPhone(field("phone")) != field("phone")) {
		return {};
	}
	const auto urlText = field("endpoint");
	const auto url = QUrl(urlText, QUrl::StrictMode);
	const auto host = url.host();
	const auto authority = urlText.mid(
		urlText.indexOf(QLatin1String("://")) + 3).section('/', 0, 0);
	if (urlText.size() > 2048 || urlText.toLatin1() != urlText.toUtf8()
		|| Matches(urlText, "[\\x00-\\x20\\x7f]")
		|| !url.isValid() || url.scheme() != QLatin1String("https")
		|| !url.userInfo().isEmpty() || authority.contains('@')
		|| url.hasQuery() || url.hasFragment()
		|| url.path() != QLatin1String("/v1/allowlist/resolve")
		|| urlText.contains('%') || authority.endsWith(':')
		|| url.port(443) < 1 || url.port(443) > 65535 || host.size() > 253
		|| !Matches(host, "\\A[A-Za-z0-9](?:[A-Za-z0-9.-]*[A-Za-z0-9])?\\z")) {
		return {};
	}
	for (const auto &label : host.split('.')) {
		if (!Matches(label, "\\A[A-Za-z0-9](?:[A-Za-z0-9-]{0,61}[A-Za-z0-9])?\\z")) {
			return {};
		}
	}
	const auto token = field("token").toLatin1();
	if (!Matches(field("token"), "\\A[A-Za-z0-9_-]{43}\\z")
		|| QByteArray::fromBase64(token, QByteArray::Base64UrlEncoding)
			.toBase64(QByteArray::Base64UrlEncoding | QByteArray::OmitTrailingEquals) != token
		|| !Matches(field("certificate_sha256"), "\\A[a-f0-9]{64}\\z")) {
		return {};
	}
	return Profile{ url, phone, token, field("certificate_sha256").toLatin1() };
}

std::optional<Profile> ReadProfile(const QString &path, const QString &phone) {
	const auto info = QFileInfo(path);
	if (!info.isFile() || info.isSymLink() || info.size() > kMaximumProfileBytes) {
		return {};
	}
	auto file = QFile(path);
	if (!file.open(QIODevice::ReadOnly)) {
		return {};
	}
	return ParseProfile(file.read(kMaximumProfileBytes + 1), phone);
}

Result ParseResponse(const QByteArray &bytes, const QString &phone) {
	const auto object = ReadObject(bytes, kMaximumResponseBytes);
	if (!object || phone.isEmpty() || AuthenticatedPhone(phone) != phone
		|| object->value(QLatin1String("phone")).toString() != phone) {
		return {};
	}
	const auto status = object->value(QLatin1String("status")).toString();
	if (status == QLatin1String("not_found")) {
		return Keys(*object, { "version", "status", "phone" })
			? Result{ Status::NotFound } : Result();
	} else if (status != QLatin1String("matched")
		|| !Keys(*object, { "version", "status", "phone", "user_ids", "group_ids" })) {
		return {};
	}
	auto result = Result();
	if (!ReadIds(object->value(QLatin1String("user_ids")), false, result.users)
		|| !ReadIds(object->value(QLatin1String("group_ids")), true, result.groups)
		|| result.users.size() + result.groups.size() > kMaximumEntries
		|| Parse(result.users.join('\n').toStdString(),
			result.groups.join('\n').toStdString()).error != Error::None) {
		return {};
	}
	result.status = Status::Matched;
	return result;
}

} // namespace Main::Allowlist::Sheet

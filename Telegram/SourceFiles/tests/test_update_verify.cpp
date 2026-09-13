/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/

// Focused console tests for the v2 update verification: throwaway keys are
// generated in-process with OpenSSL, so no fixture files and no network.

#include "core/update_channel.h"
#include "core/update_feed.h"
#include "core/update_keys.h"
#include "core/update_mandatory.h"
#include "core/update_stage.h"
#include "core/update_verify.h"

#include <QtCore/QDir>
#include <QtCore/QFile>
#include <QtCore/QFileInfo>
#include <QtCore/QJsonArray>
#include <QtCore/QJsonDocument>
#include <QtCore/QJsonObject>
#include <QtCore/QTemporaryDir>

extern "C" {
#include <openssl/bio.h>
#include <openssl/bn.h>
#include <openssl/core_names.h>
#include <openssl/ecdsa.h>
#include <openssl/evp.h>
#include <openssl/pem.h>
} // extern "C"

#include <iostream>
#include <memory>
#include <vector>

namespace {

using namespace Core::Updates;

int FailedChecks = 0;
int TotalChecks = 0;

constexpr auto kTarget = Target{ Os::Linux, Arch::X64 };
constexpr auto kOtherArch = Target{ Os::Linux, Arch::Arm };
constexpr auto kOtherOs = Target{ Os::Mac, Arch::X64 };
constexpr auto kAllowgramSequence = quint32(8);

void Check(bool condition, const char *name) {
	++TotalChecks;
	if (!condition) {
		++FailedChecks;
		std::cout << "FAILED: " << name << std::endl;
	}
}

struct EvpPkeyDeleter {
	void operator()(EVP_PKEY *value) {
		EVP_PKEY_free(value);
	}
};

using EvpPkey = std::unique_ptr<EVP_PKEY, EvpPkeyDeleter>;

struct TestKey {
	QByteArray id;
	EvpPkey pair;
	bool ed25519 = false;
	QByteArray x;
	QByteArray y;
	qint64 expires = 0;
};

[[nodiscard]] EvpPkey GenerateKey(bool ed25519) {
	const auto ctx = EVP_PKEY_CTX_new_id(
		ed25519 ? EVP_PKEY_ED25519 : EVP_PKEY_EC,
		nullptr);
	if (!ctx || EVP_PKEY_keygen_init(ctx) != 1) {
		EVP_PKEY_CTX_free(ctx);
		return nullptr;
	}
	if (!ed25519
		&& EVP_PKEY_CTX_set_ec_paramgen_curve_nid(
			ctx,
			NID_X9_62_prime256v1) != 1) {
		EVP_PKEY_CTX_free(ctx);
		return nullptr;
	}
	EVP_PKEY *raw = nullptr;
	const auto success = (EVP_PKEY_keygen(ctx, &raw) == 1);
	EVP_PKEY_CTX_free(ctx);
	return success ? EvpPkey(raw) : nullptr;
}

[[nodiscard]] QByteArray BnParam(EVP_PKEY *key, const char *name) {
	BIGNUM *bn = nullptr;
	if (EVP_PKEY_get_bn_param(key, name, &bn) != 1) {
		return QByteArray();
	}
	auto result = QByteArray(32, Qt::Uninitialized);
	const auto success = (BN_bn2binpad(
		bn,
		reinterpret_cast<uchar*>(result.data()),
		32) == 32);
	BN_free(bn);
	return success ? result : QByteArray();
}

[[nodiscard]] TestKey MakeTestKey(
		const QByteArray &id,
		bool ed25519,
		qint64 expires = 0) {
	auto result = TestKey();
	result.id = id;
	result.ed25519 = ed25519;
	result.expires = expires;
	result.pair = GenerateKey(ed25519);
	if (!result.pair) {
		return result;
	}
	if (ed25519) {
		auto x = QByteArray(32, Qt::Uninitialized);
		auto length = size_t(x.size());
		if (EVP_PKEY_get_raw_public_key(
				result.pair.get(),
				reinterpret_cast<uchar*>(x.data()),
				&length) == 1
			&& length == 32) {
			result.x = x;
		}
	} else {
		result.x = BnParam(result.pair.get(), OSSL_PKEY_PARAM_EC_PUB_X);
		result.y = BnParam(result.pair.get(), OSSL_PKEY_PARAM_EC_PUB_Y);
	}
	return result;
}

[[nodiscard]] QByteArray PublicPem(EVP_PKEY *key) {
	const auto bio = BIO_new(BIO_s_mem());
	if (!bio || PEM_write_bio_PUBKEY(bio, key) != 1) {
		BIO_free(bio);
		return QByteArray();
	}
	char *data = nullptr;
	const auto length = BIO_get_mem_data(bio, &data);
	const auto result = QByteArray(data, int(length));
	BIO_free(bio);
	return result;
}

[[nodiscard]] QByteArray DerToRawEcdsa(const QByteArray &der) {
	const auto *data = reinterpret_cast<const uchar*>(der.constData());
	const auto sig = d2i_ECDSA_SIG(nullptr, &data, long(der.size()));
	if (!sig) {
		return QByteArray();
	}
	const BIGNUM *r = nullptr;
	const BIGNUM *s = nullptr;
	ECDSA_SIG_get0(sig, &r, &s);
	auto result = QByteArray(64, Qt::Uninitialized);
	const auto success = (BN_bn2binpad(
		r,
		reinterpret_cast<uchar*>(result.data()),
		32) == 32)
		&& (BN_bn2binpad(
			s,
			reinterpret_cast<uchar*>(result.data()) + 32,
			32) == 32);
	ECDSA_SIG_free(sig);
	return success ? result : QByteArray();
}

// Ed25519 keys sign the message directly, ES256 keys produce a DER
// ECDSA-SHA256 signature which is converted to the raw r||s form that
// the envelope stores.
[[nodiscard]] QByteArray SignWith(
		const TestKey &key,
		const QByteArray &message) {
	const auto ctx = EVP_MD_CTX_new();
	if (!ctx
		|| EVP_DigestSignInit(
			ctx,
			nullptr,
			key.ed25519 ? nullptr : EVP_sha256(),
			nullptr,
			key.pair.get()) != 1) {
		EVP_MD_CTX_free(ctx);
		return QByteArray();
	}
	auto length = size_t(0);
	const auto *bytes = reinterpret_cast<const uchar*>(message.constData());
	if (EVP_DigestSign(
			ctx,
			nullptr,
			&length,
			bytes,
			size_t(message.size())) != 1) {
		EVP_MD_CTX_free(ctx);
		return QByteArray();
	}
	auto result = QByteArray(int(length), Qt::Uninitialized);
	const auto success = (EVP_DigestSign(
		ctx,
		reinterpret_cast<uchar*>(result.data()),
		&length,
		bytes,
		size_t(message.size())) == 1);
	EVP_MD_CTX_free(ctx);
	if (!success) {
		return QByteArray();
	}
	result.resize(int(length));
	return key.ed25519 ? result : DerToRawEcdsa(result);
}

[[nodiscard]] QByteArray Base64Url(const QByteArray &data) {
	return data.toBase64(
		QByteArray::Base64UrlEncoding | QByteArray::OmitTrailingEquals);
}

struct ManifestSpec {
	quint32 version = 1;
	qint64 expires = 0;
	std::vector<const TestKey*> keys;
	std::vector<std::pair<QByteArray, std::vector<std::vector<QByteArray>>>> channels;
	std::vector<QByteArray> revoked;
};

[[nodiscard]] QByteArray MakeManifestJson(const ManifestSpec &spec) {
	auto keys = QJsonArray();
	for (const auto *key : spec.keys) {
		auto object = QJsonObject();
		object.insert("id", QString::fromLatin1(key->id));
		if (key->ed25519) {
			object.insert("alg", "Ed25519");
			object.insert("x", QString::fromLatin1(Base64Url(key->x)));
		} else {
			object.insert("alg", "ES256");
			object.insert("crv", "P-256");
			object.insert("x", QString::fromLatin1(Base64Url(key->x)));
			object.insert("y", QString::fromLatin1(Base64Url(key->y)));
		}
		if (key->expires) {
			object.insert("expires", double(key->expires));
		}
		keys.append(object);
	}
	auto channels = QJsonObject();
	for (const auto &[name, groups] : spec.channels) {
		auto groupsArray = QJsonArray();
		for (const auto &group : groups) {
			auto ids = QJsonArray();
			for (const auto &id : group) {
				ids.append(QString::fromLatin1(id));
			}
			groupsArray.append(ids);
		}
		channels.insert(QString::fromLatin1(name), groupsArray);
	}
	auto revoked = QJsonArray();
	for (const auto &id : spec.revoked) {
		revoked.append(QString::fromLatin1(id));
	}
	auto root = QJsonObject();
	root.insert("format", 1);
	root.insert("manifest_version", double(spec.version));
	root.insert("issued", 1700000000);
	root.insert("expires", double(spec.expires ? spec.expires : 2000000000));
	root.insert("keys", keys);
	root.insert("channels", channels);
	root.insert("revoked", revoked);
	return QJsonDocument(root).toJson(QJsonDocument::Compact);
}

void AppendLeU32(QByteArray &to, quint32 value) {
	const char bytes[4] = {
		char(value & 0xFF),
		char((value >> 8) & 0xFF),
		char((value >> 16) & 0xFF),
		char((value >> 24) & 0xFF),
	};
	to.append(bytes, 4);
}

void AppendLeU64(QByteArray &to, quint64 value) {
	AppendLeU32(to, quint32(value & 0xFFFFFFFFULL));
	AppendLeU32(to, quint32(value >> 32));
}

[[nodiscard]] QByteArray BuildEnvelope(
		Channel channel,
		quint64 version,
		const QByteArray &manifest,
		const QByteArray &manifestSig,
		const std::vector<std::pair<QByteArray, QByteArray>> &signatures,
		const QByteArray &payload,
		Target target = kTarget,
		quint32 format = kEnvelopeFormat) {
	auto result = QByteArray();
	result.append(kEnvelopeMagic, 4);
	AppendLeU32(result, format);
	result.append(char(uchar(channel)));
	result.append(char(uchar(target.os)));
	result.append(char(uchar(target.arch)));
	AppendLeU64(result, version);
	AppendLeU64(result, quint64(1700000000));
	AppendLeU32(result, quint32(manifest.size()));
	result.append(manifest);
	AppendLeU32(result, quint32(manifestSig.size()));
	result.append(manifestSig);
	AppendLeU32(result, quint32(signatures.size()));
	for (const auto &[id, signature] : signatures) {
		AppendLeU32(result, quint32(id.size()));
		result.append(id);
		AppendLeU32(result, quint32(signature.size()));
		result.append(signature);
	}
	AppendLeU32(result, quint32(payload.size()));
	result.append(payload);
	return result;
}

[[nodiscard]] QByteArray BuildSignedEnvelope(
		Channel channel,
		quint64 version,
		const QByteArray &manifest,
		const QByteArray &manifestSig,
		const std::vector<const TestKey*> &signers,
		const QByteArray &payload,
		Target target = kTarget) {
	const auto unsignedBytes = BuildEnvelope(
		channel,
		version,
		manifest,
		manifestSig,
		{},
		payload,
		target);
	const auto envelope = ParseEnvelope(unsignedBytes);
	if (!envelope) {
		return QByteArray();
	}
	const auto input = SigningInput(*envelope);
	auto signatures = std::vector<std::pair<QByteArray, QByteArray>>();
	for (const auto *signer : signers) {
		signatures.push_back({ signer->id, SignWith(*signer, input) });
	}
	return BuildEnvelope(
		channel,
		version,
		manifest,
		manifestSig,
		signatures,
		payload,
		target);
}

QByteArray AllowgramFeed(
		QString product = QStringLiteral("Allowgram"),
		QString channel = QStringLiteral("stable"),
		QString platform = QStringLiteral("linux"),
		QString os = QStringLiteral("linux"),
		QString arch = QStringLiteral("x64"),
		QString display = QStringLiteral("7.2.8.8"),
		quint32 base = 7002008,
		quint32 sequence = 8,
		QString fileName = QString(),
		quint64 size = 4096,
		QString sha256 = QString(),
		QString releaseTag = QString(),
		bool draft = false,
		bool prerelease = false) {
	if (fileName.isNull()) {
		fileName = QStringLiteral("allowgram-update-stable-%1-%2-%3.tdup"
			).arg(os
			).arg(arch
			).arg(display);
	}
	if (sha256.isNull()) {
		sha256 = QString(64, QLatin1Char('a'));
	}
	if (releaseTag.isNull()) {
		releaseTag = QStringLiteral("v") + display;
	}
	const auto file = QJsonObject{
		{ "os", os },
		{ "arch", arch },
		{ "file", fileName },
		{ "size", double(size) },
		{ "sha256", sha256 },
	};
	const auto root = QJsonObject{
		{ "format", 1 },
		{ "product", product },
		{ "channel", channel },
		{ "release", QJsonObject{
			{ "tag", releaseTag },
			{ "draft", draft },
			{ "prerelease", prerelease },
		} },
		{ "version", QJsonObject{
			{ "display", display },
			{ "base", int(base) },
			{ "sequence", int(sequence) },
		} },
		{ "files", QJsonObject{ { platform, file } } },
	};
	return QJsonDocument(root).toJson(QJsonDocument::Compact);
}

QJsonArray StableFeedSignatures(
		const QByteArray &signedBytes,
		const std::vector<const TestKey*> &signers) {
	auto signatures = QJsonArray();
	const auto input = StableReleaseFeedSigningInput(signedBytes);
	for (const auto *signer : signers) {
		signatures.append(QJsonObject{
			{ "key_id", QString::fromLatin1(signer->id) },
			{ "signature", QString::fromLatin1(
				Base64Url(SignWith(*signer, input))) },
		});
	}
	return signatures;
}

QByteArray SignedAllowgramFeed(
		const QByteArray &signedBytes,
		const QJsonArray &signatures) {
	const auto root = QJsonObject{
		{ "format", 1 },
		{ "signed", QString::fromLatin1(Base64Url(signedBytes)) },
		{ "signatures", signatures },
	};
	return QJsonDocument(root).toJson(QJsonDocument::Compact);
}

QByteArray SignedAllowgramFeed(
		const QByteArray &signedBytes,
		const std::vector<const TestKey*> &signers) {
	return SignedAllowgramFeed(
		signedBytes,
		StableFeedSignatures(signedBytes, signers));
}

MandatoryUpdateTarget MandatoryTarget(quint32 sequence) {
	const auto display = QStringLiteral("7.2.8.%1").arg(sequence);
	return {
		.tag = QStringLiteral("v") + display,
		.fileName = StableReleaseFileName(kTarget, display),
		.sha256 = QByteArray(64, 'b'),
		.size = 4096,
		.packedVersion = MakeUpdateVersion(7002008, sequence),
		.displayVersion = display,
	};
}
[[nodiscard]] bool WriteStageFile(
		const QString &root,
		const QString &relative,
		const QByteArray &bytes) {
	const auto path = QDir(root).filePath(relative);
	if (!QDir().mkpath(QFileInfo(path).absolutePath())) {
		return false;
	}
	auto file = QFile(path);
	return file.open(QIODevice::WriteOnly)
		&& file.write(bytes) == bytes.size();
}

[[nodiscard]] StagedUpdateFile MakeStageFile(
		const QString &path,
		const QByteArray &bytes) {
	return { path, quint64(bytes.size()), Sha256Bytes(bytes) };
}
} // namespace

int main(int argc, char *argv[]) {
	Check(
		Core::RunningUpdateVersion()
			== MakeUpdateVersion(quint32(AppVersion), kAllowgramSequence),
		"Allowgram update version uses the stable sequence");
	Check(
		DisplayUpdateVersion(MakeUpdateVersion(7002008, 8))
			== QStringLiteral("7.2.8.8"),
		"packed Allowgram version formats with the stable sequence");

	constexpr auto kNow = qint64(1800000000);
	const auto payload = QByteArray("test-payload-not-really-lzma");

	auto root = MakeTestKey("root", true);
	auto rl = MakeTestKey("rl-test", true);
	auto rc = MakeTestKey("rc-test", false);
	auto cp = MakeTestKey("cp-test", false, kNow + 1000);
	auto cx = MakeTestKey("cx-test", false, kNow + 1000);
	auto expired = MakeTestKey("old-test", false, kNow - 1000);
	auto rogue = MakeTestKey("cp-test", false); // same id, different key
	Check(!root.x.isEmpty() && !rc.x.isEmpty() && !rc.y.isEmpty(),
		"test key generation");

	const auto rootPem = PublicPem(root.pair.get());
	Check(!rootPem.isEmpty(), "root public pem");

	auto spec = ManifestSpec();
	spec.version = 2;
	spec.keys = { &rl, &rc, &cp, &cx, &expired };
	spec.channels = {
		{ "stable", { { "rl-test" }, { "rc-test" } } },
		{ "beta", { { "rl-test" }, { "rc-test" } } },
		{ "canary-public", { { "cp-test" } } },
		{ "canary-private", { { "cx-test" } } },
	};
	const auto manifestJson = MakeManifestJson(spec);
	const auto manifestSig = SignWith(root, manifestJson);

	{
		auto error = QString();
		const auto manifest = ParseVerifiedManifest(
			manifestJson,
			manifestSig,
			rootPem,
			&error);
		Check(manifest.has_value(), "manifest parses and verifies");
		Check(manifest && manifest->version == 2, "manifest version");
		Check(manifest && manifest->keys.size() == 5, "manifest keys count");
		Check(!ParseVerifiedManifest(manifestJson + " ", manifestSig, rootPem),
			"modified manifest rejected");
		Check(!ParseVerifiedManifest(manifestJson, SignWith(rl, manifestJson), rootPem),
			"manifest signed by non-root key rejected");
	}
	const auto held = ParseVerifiedManifest(manifestJson, manifestSig, rootPem);

	const auto runningStable = MakeUpdateVersion(5000000, 0);
	const auto runningCanary = MakeUpdateVersion(5000000, 40);

	const auto verify = [&](
			const QByteArray &data,
			Channel build,
			bool betaSet,
			quint64 running,
			QString *error = nullptr) {
		return VerifyUpdate(
			data,
			build,
			betaSet,
			kTarget,
			running,
			held,
			rootPem,
			kNow,
			error);
	};

	{ // The Allowgram release feed is fixed to signed stable GitHub assets.
		auto error = QString();
		const auto feedUrl = QStringLiteral(
			"https://github.com/molotovgit/allowgram/releases/latest/download/"
			"allowgram-update-feed.json");
		const auto releaseTag = QStringLiteral("v7.2.8.8");
		const auto assetName = QStringLiteral(
			"allowgram-update-stable-linux-x64-7.2.8.8.tdup");
		const auto running = MakeUpdateVersion(7002008, 7);
		const auto signedFeed = [&](QByteArray bytes) {
			return SignedAllowgramFeed(
			bytes,
			std::vector<const TestKey*>{ &rl, &rc });
		};
		const auto parseFeed = [&](const QByteArray &bytes, quint64 version) {
			return ParseStableReleaseFeed(
				bytes,
				"linux",
				version,
				held,
				kNow,
				&error);
		};
		const auto parsed = parseFeed(signedFeed(AllowgramFeed()), running);
		Check(parsed && parsed->updateAvailable,
			"Allowgram feed offers a newer stable package");
		Check(parsed && parsed->asset.fileName == assetName,
			"Allowgram feed accepts the exact asset name");
		Check(parsed && parsed->asset.tag == releaseTag,
			"Allowgram feed carries the exact release tag");
		Check(
			parsed && parsed->asset.url == StableReleaseDownloadUrl(
				releaseTag,
				assetName),
			"Allowgram feed builds the immutable GitHub asset URL");
		Check(StableReleaseFeedUrl() == feedUrl,
			"Allowgram feed uses the fixed GitHub release asset URL");
		Check(
			parsed && parsed->asset.packedVersion == MakeUpdateVersion(7002008, 8),
			"Allowgram feed carries the packed stable sequence");
		const auto same = parseFeed(
			signedFeed(AllowgramFeed()),
			MakeUpdateVersion(7002008, 8));
		Check(same && !same->updateAvailable,
			"equal Allowgram sequence is not offered");
		Check(!ParseStableReleaseFeed(
			AllowgramFeed(),
			"linux",
			running,
			held,
			kNow),
			"unsigned Allowgram feed rejected before mandatory lock");
		Check(!ParseStableReleaseFeed(
			signedFeed(AllowgramFeed()),
			"linux",
			running,
			std::nullopt,
			kNow),
			"Allowgram feed without a trusted manifest rejected");
		auto expiredManifest = held;
		if (expiredManifest) {
			expiredManifest->expires = kNow - 1;
		}
		Check(!ParseStableReleaseFeed(
			signedFeed(AllowgramFeed()),
			"linux",
			running,
			expiredManifest,
			kNow),
			"Allowgram feed with expired manifest rejected");
		Check(!ParseStableReleaseFeed(
			SignedAllowgramFeed(
				AllowgramFeed(),
				std::vector<const TestKey*>{ &rl }),
			"linux",
			running,
			held,
			kNow),
			"Allowgram feed missing a stable signature group rejected");
		Check(!ParseStableReleaseFeed(
			SignedAllowgramFeed(
				AllowgramFeed(),
				std::vector<const TestKey*>{ &rl, &rogue }),
			"linux",
			running,
			held,
			kNow),
			"Allowgram feed signed by foreign key rejected");
		const auto originalSignedBytes = AllowgramFeed();
		auto tamperedSignedBytes = originalSignedBytes;
		tamperedSignedBytes[tamperedSignedBytes.size() - 2]
			= tamperedSignedBytes[tamperedSignedBytes.size() - 2] ^ 1;
		Check(!ParseStableReleaseFeed(
			SignedAllowgramFeed(
				tamperedSignedBytes,
				StableFeedSignatures(
					originalSignedBytes,
					std::vector<const TestKey*>{ &rl, &rc })),
			"linux",
			running,
			held,
			kNow),
			"Allowgram feed signature mismatch rejected");
		Check(!parseFeed(
			signedFeed(AllowgramFeed(
				QStringLiteral("Allowgram"),
				QStringLiteral("stable"),
				QStringLiteral("linux"),
				QStringLiteral("linux"),
				QStringLiteral("x64"),
				QStringLiteral("7.2.8.65536"),
				7002008,
				65536)),
			running),
			"out-of-PE-range Allowgram sequence rejected");
		Check(!parseFeed(
			signedFeed(AllowgramFeed(QStringLiteral("Telegram"))),
			running),
			"foreign product feed rejected");
		Check(!parseFeed(
			signedFeed(AllowgramFeed(
				QStringLiteral("Allowgram"),
				QStringLiteral("beta"))),
			running),
			"beta feed rejected");
		Check(!parseFeed(
			signedFeed(AllowgramFeed(
				QStringLiteral("Allowgram"),
				QStringLiteral("stable"),
				QStringLiteral("linux"),
				QStringLiteral("linux"),
				QStringLiteral("x64"),
				QStringLiteral("7.2.8.8"),
				7002008,
				8,
				QString(),
				4096,
				QString(),
				QStringLiteral("v7.2.8.9"))),
			running),
			"mismatched release tag rejected");
		Check(!parseFeed(
			signedFeed(AllowgramFeed(
				QStringLiteral("Allowgram"),
				QStringLiteral("stable"),
				QStringLiteral("linux"),
				QStringLiteral("linux"),
				QStringLiteral("x64"),
				QStringLiteral("7.2.8.8"),
				7002008,
				8,
				QString(),
				4096,
				QString(),
				QString(),
				true)),
			running),
			"draft release feed rejected");
		Check(!ParseStableReleaseFeed(
			signedFeed(AllowgramFeed()),
			"win64",
			running,
			held,
			kNow),
			"missing platform asset rejected");
		Check(!parseFeed(
			signedFeed(AllowgramFeed(
				QStringLiteral("Allowgram"),
				QStringLiteral("stable"),
				QStringLiteral("linux"),
				QStringLiteral("mac"))),
			running),
			"wrong OS asset rejected");
		Check(!parseFeed(
			signedFeed(AllowgramFeed(
				QStringLiteral("Allowgram"),
				QStringLiteral("stable"),
				QStringLiteral("linux"),
				QStringLiteral("linux"),
				QStringLiteral("x64"),
				QStringLiteral("7.2.8.8"),
				7002008,
				8,
				QStringLiteral("td-update-linux-x64-7002008"))),
			running),
			"legacy official asset name rejected");
		Check(!parseFeed(
			signedFeed(AllowgramFeed(
				QStringLiteral("Allowgram"),
				QStringLiteral("stable"),
				QStringLiteral("linux"),
				QStringLiteral("linux"),
				QStringLiteral("x64"),
				QStringLiteral("7.2.8.8"),
				7002008,
				8,
				QString(),
				quint64(kMaxPayloadSize) + 1)),
			running),
			"oversized feed asset rejected");
		Check(!parseFeed(
			signedFeed(AllowgramFeed(
				QStringLiteral("Allowgram"),
				QStringLiteral("stable"),
				QStringLiteral("linux"),
				QStringLiteral("linux"),
				QStringLiteral("x64"),
				QStringLiteral("7.2.8.8"),
				7002008,
				8,
				QString(),
				4096,
				QStringLiteral("abc"))),
			running),
			"bad feed SHA-256 rejected");
	}

	{ // Mandatory update state records an authenticated target without extending grace.
		const auto target8 = MandatoryTarget(8);
		const auto target9 = MandatoryTarget(9);
		const auto running = MakeUpdateVersion(7002008, 7);
		const auto first = RegisterMandatoryUpdate(
			std::nullopt,
			target8,
			1000);
		Check(first.active && first.firstSeen == 1000 && first.deadline == 1300,
			"mandatory update starts one 300-second deadline");
		Check(MandatoryStatus(first, running, 1299) == MandatoryUpdateStatus::Grace,
			"mandatory update is in grace before deadline");
		Check(MandatorySecondsRemaining(first, 1299) == 1,
			"mandatory update reports remaining seconds");
		Check(MandatoryStatus(first, running, 1300) == MandatoryUpdateStatus::Expired,
			"mandatory update expires at exact deadline");
		const auto duplicate = RegisterMandatoryUpdate(first, target8, 1060);
		Check(duplicate.deadline == first.deadline,
			"duplicate mandatory discovery does not extend deadline");
		const auto newer = RegisterMandatoryUpdate(duplicate, target9, 1090);
		Check(newer.deadline == first.deadline
			&& newer.target.packedVersion == target9.packedVersion,
			"newer mandatory target keeps the original deadline");
		const auto older = RegisterMandatoryUpdate(newer, target8, 1100);
		Check(older.target.packedVersion == target9.packedVersion,
			"older mandatory target cannot downgrade pending update");
		const auto dismissed = DismissMandatoryUpdatePopup(newer);
		Check(dismissed.popupDismissed
			&& dismissed.deadline == newer.deadline,
			"closing mandatory popup only records dismissal");
		const auto applying = MarkMandatoryUpdateApplyStarted(dismissed);
		Check(applying.applyStarted && applying.deadline == newer.deadline,
			"starting mandatory apply does not extend deadline");
		Check(MandatoryStatus(newer, target9.packedVersion, 1100)
			== MandatoryUpdateStatus::None,
			"installed mandatory target clears the requirement");
		auto dir = QTemporaryDir();
		auto error = QString();
		Check(WriteMandatoryUpdateState(dir.path(), applying, &error),
			"mandatory update state writes atomically");
		const auto roundtrip = ReadMandatoryUpdateState(dir.path(), &error);
		Check(roundtrip && *roundtrip == applying,
			"mandatory update state round-trips from updater storage");
		ClearMandatoryUpdateState(dir.path());
		Check(!ReadMandatoryUpdateState(dir.path()),
			"mandatory update state clears from updater storage");
	}
	{ // Stage manifests authenticate the exact staged Windows payload tree.
		auto dir = QTemporaryDir();
		const auto app = QByteArray("new app");
		const auto helper = QByteArray("new helper");
		const auto info = QByteArray("{\"version\":\"7.2.8.9\"}");
		const auto readme = QByteArray("readme");
		Check(WriteStageFile(dir.path(), QStringLiteral("Allowgram.exe"), app),
			"stage test writes app payload");
		Check(WriteStageFile(dir.path(), QStringLiteral("AllowgramUpdater.exe"), helper),
			"stage test writes helper payload");
		Check(WriteStageFile(dir.path(), QStringLiteral("build-info.json"), info),
			"stage test writes build info payload");
		Check(WriteStageFile(dir.path(), QStringLiteral("README.txt"), readme),
			"stage test writes optional payload file");
		const auto package = QByteArray("signed package bytes");
		const auto manifest = StagedUpdateManifest{
			.packedVersion = MakeUpdateVersion(7002008, 9),
			.displayVersion = QStringLiteral("7.2.8.9"),
			.packageSha256 = Sha256Bytes(package),
			.files = {
				MakeStageFile(QStringLiteral("Allowgram.exe"), app),
				MakeStageFile(QStringLiteral("AllowgramUpdater.exe"), helper),
				MakeStageFile(QStringLiteral("build-info.json"), info),
				MakeStageFile(QStringLiteral("README.txt"), readme),
			},
		};
		const auto serialized = SerializeStageManifest(manifest);
		const auto expectedHash = Sha256Bytes(serialized);
		auto error = QString();
		Check(!serialized.isEmpty(),
			"stage manifest serializes valid payload metadata");
		Check(WriteStageManifest(dir.path(), manifest, &error),
			"stage manifest writes beside the ready marker");
		Check(
			WriteStageFile(
				dir.path(),
				QStringLiteral("tdata/package.tdup"),
				payload),
			"retained signed package can be stored in the stage metadata");
		const auto ready = VerifyStagedUpdate(
			dir.path(),
			MakeUpdateVersion(7002008, 8),
			expectedHash,
			&error);
		Check(ready && ready->packedVersion == manifest.packedVersion,
			"stage manifest accepts newer exact staged payload");
		Check(ready && ready->manifestSha256 == expectedHash,
			"stage manifest reports the launcher helper hash");
		Check(
			VerifyStagedUpdate(
				dir.path(),
				MakeUpdateVersion(7002008, 8),
				expectedHash,
				&error).has_value(),
			"retained signed package is allowed but not copied");
		Check(!VerifyStagedUpdate(
				dir.path(),
				manifest.packedVersion,
				expectedHash,
				&error),
			"stage manifest rejects equal-version replay");
		Check(WriteStageFile(dir.path(), QStringLiteral("README.txt"), QByteArray("tamper")),
			"stage test tampers optional payload file");
		Check(!VerifyStagedUpdate(
				dir.path(),
				MakeUpdateVersion(7002008, 8),
				expectedHash,
				&error),
			"stage manifest rejects staged file tampering");
		Check(WriteStageFile(dir.path(), QStringLiteral("README.txt"), readme),
			"stage test restores optional payload file");
		Check(WriteStageFile(dir.path(), QStringLiteral("extra.bin"), QByteArray("x")),
			"stage test writes unexpected payload file");
		Check(!VerifyStagedUpdate(
				dir.path(),
				MakeUpdateVersion(7002008, 8),
				expectedHash,
				&error),
			"stage manifest rejects unexpected staged file");
		QFile::remove(QDir(dir.path()).filePath(QStringLiteral("extra.bin")));
		auto duplicate = manifest;
		duplicate.files.push_back(
			MakeStageFile(QStringLiteral("allowgram.exe"), QByteArray("case")));
		Check(SerializeStageManifest(duplicate).isEmpty(),
			"stage manifest rejects case-colliding payload paths");
		Check(!NormalizeUpdatePayloadPath(QStringLiteral("../Allowgram.exe")),
			"stage manifest rejects traversal payload paths");
		Check(!NormalizeUpdatePayloadPath(QStringLiteral("C:/Allowgram.exe")),
			"stage manifest rejects drive-qualified payload paths");
		Check(!UpdatePayloadFileAllowed(QStringLiteral("Telegram.exe")),
			"stage manifest rejects legacy Telegram payload identity");
	}
	{ // The committed trust files must verify with the pinned root.
		auto error = QString();
		const auto embedded = ParseVerifiedManifest(
			EmbeddedManifest(),
			EmbeddedManifestSignature(),
			RootPublicKeyPem(),
			&error);
		Check(embedded.has_value(), "embedded manifest verifies");
		Check(embedded && embedded->version >= 1, "embedded manifest version");
		Check(embedded && embedded->channels.size() == 1,
			"embedded manifest lists Allowgram stable only");
		Check(embedded && embedded->channels.contains("stable"),
			"embedded manifest has the stable channel");
		Check(
			embedded && embedded->keys.size() == 1
				&& embedded->keys.front().id == "allowgram-release-2026a",
			"embedded manifest has the Allowgram release key");
	}

	{ // A good stable package needs one rl AND one rc signature.
		const auto good = BuildSignedEnvelope(
			Channel::Stable,
			MakeUpdateVersion(5000001, 0),
			manifestJson,
			manifestSig,
			{ &rl, &rc },
			payload);
		auto error = QString();
		const auto verified = verify(
			good,
			Channel::Stable,
			false,
			runningStable,
			&error);
		Check(verified.has_value(), "good stable package accepted");
		Check(verified && verified->envelope.payload == payload,
			"payload survives verification");
		Check(verified && !verified->adoptManifest,
			"same manifest version is not re-adopted");

		const auto onlyRl = BuildSignedEnvelope(
			Channel::Stable,
			MakeUpdateVersion(5000001, 0),
			manifestJson,
			manifestSig,
			{ &rl },
			payload);
		Check(!verify(onlyRl, Channel::Stable, false, runningStable),
			"stable with one of two groups rejected (AND-of-ORs)");

		const auto onlyRc = BuildSignedEnvelope(
			Channel::Stable,
			MakeUpdateVersion(5000001, 0),
			manifestJson,
			manifestSig,
			{ &rc },
			payload);
		Check(!verify(onlyRc, Channel::Stable, false, runningStable),
			"stable with only the second group rejected");
	}

	{ // Canary-public needs just cp, and only on canary builds.
		const auto good = BuildSignedEnvelope(
			Channel::CanaryPublic,
			MakeUpdateVersion(5000000, 41),
			manifestJson,
			manifestSig,
			{ &cp },
			payload);
		Check(verify(good, Channel::CanaryPublic, false, runningCanary)
				.has_value(),
			"good canary-public package accepted");
		Check(!verify(good, Channel::Stable, false, runningStable),
			"canary package on stable build rejected");
		Check(!verify(good, Channel::Stable, true, runningStable),
			"canary package on stable+beta build rejected");
		Check(!verify(good, Channel::CanaryPrivate, false, runningCanary),
			"canary-public package on canary-private build rejected");

		const auto wrongKey = BuildSignedEnvelope(
			Channel::CanaryPublic,
			MakeUpdateVersion(5000000, 41),
			manifestJson,
			manifestSig,
			{ &cx },
			payload);
		Check(!verify(wrongKey, Channel::CanaryPublic, false, runningCanary),
			"canary-public signed by canary-private key rejected");

		const auto rogueKey = BuildSignedEnvelope(
			Channel::CanaryPublic,
			MakeUpdateVersion(5000000, 41),
			manifestJson,
			manifestSig,
			{ &rogue },
			payload);
		Check(!verify(rogueKey, Channel::CanaryPublic, false, runningCanary),
			"signature by an impostor key with a known id rejected");
	}

	{ // Canary-private is an island.
		const auto good = BuildSignedEnvelope(
			Channel::CanaryPrivate,
			MakeUpdateVersion(5000000, 41),
			manifestJson,
			manifestSig,
			{ &cx },
			payload);
		Check(verify(good, Channel::CanaryPrivate, false, runningCanary)
				.has_value(),
			"good canary-private package accepted");
		Check(!verify(good, Channel::CanaryPublic, false, runningCanary),
			"canary-private package on canary-public build rejected");

		const auto stable = BuildSignedEnvelope(
			Channel::Stable,
			MakeUpdateVersion(6000000, 0),
			manifestJson,
			manifestSig,
			{ &rl, &rc },
			payload);
		Check(!verify(stable, Channel::CanaryPrivate, false, runningCanary),
			"stable package on canary-private build rejected (island)");
	}

	{ // Dormancy rescue: stable on canary-public iff base strictly greater.
		const auto newerBase = BuildSignedEnvelope(
			Channel::Stable,
			MakeUpdateVersion(5000001, 0),
			manifestJson,
			manifestSig,
			{ &rl, &rc },
			payload);
		Check(verify(newerBase, Channel::CanaryPublic, false, runningCanary)
				.has_value(),
			"stable with greater base accepted on canary-public");

		const auto sameBase = BuildSignedEnvelope(
			Channel::Stable,
			MakeUpdateVersion(5000000, 0),
			manifestJson,
			manifestSig,
			{ &rl, &rc },
			payload);
		Check(!verify(sameBase, Channel::CanaryPublic, false, runningCanary),
			"stable with same base rejected on canary-public");
	}

	{ // Beta policy on stable builds.
		const auto beta = BuildSignedEnvelope(
			Channel::Beta,
			MakeUpdateVersion(5000001, 0),
			manifestJson,
			manifestSig,
			{ &rl, &rc },
			payload);
		Check(!verify(beta, Channel::Stable, false, runningStable),
			"beta package rejected without the beta setting");
		Check(!ChannelPolicyAllows(
				Channel::Stable,
				true,
				Channel::Beta,
				MakeUpdateVersion(5000001, 0),
				runningStable),
			"Allowgram stable package policy rejects imported beta setting");
		Check(!verify(beta, Channel::Stable, true, runningStable),
			"beta package rejected with the imported beta setting");
		Check(verify(beta, Channel::Beta, false, runningStable).has_value(),
			"beta package accepted on a beta build");
	}

	{ // Version monotonicity.
		const auto same = BuildSignedEnvelope(
			Channel::CanaryPublic,
			runningCanary,
			manifestJson,
			manifestSig,
			{ &cp },
			payload);
		Check(!verify(same, Channel::CanaryPublic, false, runningCanary),
			"same version rejected");

		const auto older = BuildSignedEnvelope(
			Channel::CanaryPublic,
			MakeUpdateVersion(5000000, 39),
			manifestJson,
			manifestSig,
			{ &cp },
			payload);
		Check(!verify(older, Channel::CanaryPublic, false, runningCanary),
			"older version rejected");
	}

	{ // Expired keys cannot satisfy a group: sign with the key that
	  // expired before kNow, listed under canary-public as well.
		auto withExpired = spec;
		withExpired.channels[2] = {
			"canary-public",
			{ { "cp-test", "old-test" } },
		};
		const auto json = MakeManifestJson(withExpired);
		const auto sig = SignWith(root, json);
		const auto byExpired = BuildSignedEnvelope(
			Channel::CanaryPublic,
			MakeUpdateVersion(5000000, 41),
			json,
			sig,
			{ &expired },
			payload);
		const auto heldSame = ParseVerifiedManifest(json, sig, rootPem);
		Check(!VerifyUpdate(
				byExpired,
				Channel::CanaryPublic,
				false,
				kTarget,
				runningCanary,
				heldSame,
				rootPem,
				kNow),
			"signature by an expired key rejected");
		Check(VerifyUpdate(
				byExpired,
				Channel::CanaryPublic,
				false,
				kTarget,
				runningCanary,
				heldSame,
				rootPem,
				kNow - 2000).has_value(),
			"same signature accepted before the key expiry");
	}

	{ // Revocation kills a key even while it is still listed.
		auto revokedSpec = spec;
		revokedSpec.version = 3;
		revokedSpec.revoked = { "cp-test" };
		const auto json = MakeManifestJson(revokedSpec);
		const auto sig = SignWith(root, json);
		const auto package = BuildSignedEnvelope(
			Channel::CanaryPublic,
			MakeUpdateVersion(5000000, 41),
			json,
			sig,
			{ &cp },
			payload);
		Check(!verify(package, Channel::CanaryPublic, false, runningCanary),
			"package carrying a manifest that revokes its own key rejected");

		// The newer revoking manifest must win even when the package
		// carries the older one that still authorizes the key.
		const auto heldRevoking = ParseVerifiedManifest(json, sig, rootPem);
		const auto oldManifestPackage = BuildSignedEnvelope(
			Channel::CanaryPublic,
			MakeUpdateVersion(5000000, 41),
			manifestJson,
			manifestSig,
			{ &cp },
			payload);
		Check(!VerifyUpdate(
				oldManifestPackage,
				Channel::CanaryPublic,
				false,
				kTarget,
				runningCanary,
				heldRevoking,
				rootPem,
				kNow),
			"older carried manifest cannot undo a held revocation");
	}

	{ // A newer carried manifest is adopted (stepping-stone bootstrap).
		auto newerSpec = spec;
		newerSpec.version = 7;
		const auto json = MakeManifestJson(newerSpec);
		const auto sig = SignWith(root, json);
		const auto package = BuildSignedEnvelope(
			Channel::CanaryPublic,
			MakeUpdateVersion(5000000, 41),
			json,
			sig,
			{ &cp },
			payload);
		const auto verified = verify(
			package,
			Channel::CanaryPublic,
			false,
			runningCanary);
		Check(verified.has_value(), "package with newer manifest accepted");
		Check(verified && verified->adoptManifest,
			"newer carried manifest is marked for adoption");
		Check(verified && verified->manifest.version == 7,
			"the newest manifest is the one used");

		const auto noHeld = VerifyUpdate(
			package,
			Channel::CanaryPublic,
			false,
			kTarget,
			runningCanary,
			std::nullopt,
			rootPem,
			kNow);
		Check(noHeld.has_value() && noHeld->adoptManifest,
			"verification works with no held manifest at all");
	}

	{ // An expired manifest still verifies packages (revocation is the
	  // kill switch, expiry only drives the build-time watchdog).
		auto staleSpec = spec;
		staleSpec.version = 4;
		staleSpec.expires = kNow - 1000;
		const auto json = MakeManifestJson(staleSpec);
		const auto sig = SignWith(root, json);
		const auto package = BuildSignedEnvelope(
			Channel::CanaryPublic,
			MakeUpdateVersion(5000000, 41),
			json,
			sig,
			{ &cp },
			payload);
		Check(verify(package, Channel::CanaryPublic, false, runningCanary)
				.has_value(),
			"package with expired manifest still accepted");
	}

	{ // Tampering with any signed byte must break verification.
		const auto good = BuildSignedEnvelope(
			Channel::CanaryPublic,
			MakeUpdateVersion(5000000, 41),
			manifestJson,
			manifestSig,
			{ &cp },
			payload);
		auto tamperedPayload = good;
		tamperedPayload[tamperedPayload.size() - 1]
			= tamperedPayload[tamperedPayload.size() - 1] ^ 1;
		Check(!verify(
				tamperedPayload,
				Channel::CanaryPublic,
				false,
				runningCanary),
			"tampered payload rejected");

		auto tamperedChannel = good;
		tamperedChannel[8] = char(uchar(Channel::Stable));
		Check(!verify(
				tamperedChannel,
				Channel::Stable,
				false,
				runningStable),
			"channel byte tampering rejected");

		auto tamperedVersion = good;
		tamperedVersion[13] = tamperedVersion[13] ^ 1;
		Check(!verify(
				tamperedVersion,
				Channel::CanaryPublic,
				false,
				runningCanary),
			"version tampering rejected");

		const auto badFormat = BuildEnvelope(
			Channel::CanaryPublic,
			MakeUpdateVersion(5000000, 41),
			manifestJson,
			manifestSig,
			{},
			payload,
			kTarget,
			3);
		Check(!ParseEnvelope(badFormat), "unknown format rejected");

		auto badOs = good;
		badOs[9] = char(3);
		Check(!ParseEnvelope(badOs), "unknown os rejected");
		auto badArch = good;
		badArch[10] = char(3);
		Check(!ParseEnvelope(badArch), "unknown arch rejected");

		auto badMagic = good;
		badMagic[0] = 'X';
		Check(!IsV2UpdateFile(badMagic), "bad magic not detected as v2");

		// No truncated prefix may parse, let alone verify or crash.
		auto truncationSafe = true;
		for (auto i = 0; i != good.size(); ++i) {
			if (ParseEnvelope(good.left(i))) {
				truncationSafe = false;
				break;
			}
		}
		Check(truncationSafe, "every truncated prefix rejected");
		Check(!verify(
				good + QByteArray("x"),
				Channel::CanaryPublic,
				false,
				runningCanary),
			"trailing garbage rejected");
	}

	{ // Unknown signature entries are ignored, satisfied groups win.
		const auto unsignedBytes = BuildEnvelope(
			Channel::CanaryPublic,
			MakeUpdateVersion(5000000, 41),
			manifestJson,
			manifestSig,
			{},
			payload);
		const auto envelope = ParseEnvelope(unsignedBytes);
		const auto input = SigningInput(*envelope);
		const auto withExtra = BuildEnvelope(
			Channel::CanaryPublic,
			MakeUpdateVersion(5000000, 41),
			manifestJson,
			manifestSig,
			{
				{ "who-is-this", QByteArray(64, 'x') },
				{ cp.id, SignWith(cp, input) },
			},
			payload);
		Check(verify(withExtra, Channel::CanaryPublic, false, runningCanary)
				.has_value(),
			"unknown extra signature entries are ignored");

		// A DER-encoded ECDSA signature is not the raw r||s the envelope
		// stores, the fixed 64-byte check must refuse it.
		auto der = QByteArray();
		der.append(char(0x30)).append(char(0x44));
		der.append(char(0x02)).append(char(0x20)).append(QByteArray(32, 'r'));
		der.append(char(0x02)).append(char(0x20)).append(QByteArray(32, 's'));
		const auto withDer = BuildEnvelope(
			Channel::CanaryPublic,
			MakeUpdateVersion(5000000, 41),
			manifestJson,
			manifestSig,
			{ { cp.id, der } },
			payload);
		Check(!verify(withDer, Channel::CanaryPublic, false, runningCanary),
			"DER-encoded ES256 signature rejected");
	}

	{ // The signed target must be the one this client should receive.
		const auto otherArch = BuildSignedEnvelope(
			Channel::CanaryPublic,
			MakeUpdateVersion(5000000, 41),
			manifestJson,
			manifestSig,
			{ &cp },
			payload,
			kOtherArch);
		Check(!verify(otherArch, Channel::CanaryPublic, false, runningCanary),
			"validly signed package for another arch rejected");
		Check(VerifyUpdate(
				otherArch,
				Channel::CanaryPublic,
				false,
				kOtherArch,
				runningCanary,
				held,
				rootPem,
				kNow).has_value(),
			"the same package accepted by a client expecting that arch");

		const auto otherOs = BuildSignedEnvelope(
			Channel::CanaryPublic,
			MakeUpdateVersion(5000000, 41),
			manifestJson,
			manifestSig,
			{ &cp },
			payload,
			kOtherOs);
		Check(!verify(otherOs, Channel::CanaryPublic, false, runningCanary),
			"validly signed package for another os rejected");

		const auto good = BuildSignedEnvelope(
			Channel::CanaryPublic,
			MakeUpdateVersion(5000000, 41),
			manifestJson,
			manifestSig,
			{ &cp },
			payload);
		auto retargeted = good;
		retargeted[10] = char(uchar(Arch::Arm));
		Check(!VerifyUpdate(
				retargeted,
				Channel::CanaryPublic,
				false,
				kOtherArch,
				runningCanary,
				held,
				rootPem,
				kNow),
			"target byte tampering breaks the signature");

		Check(TargetFromPlatformKey("armac")
				&& *TargetFromPlatformKey("armac") == Target{ Os::Mac, Arch::Arm }
				&& TargetFromPlatformKey("win64")
				&& *TargetFromPlatformKey("win64") == Target{ Os::Windows, Arch::X64 }
				&& !TargetFromPlatformKey("amiga"),
			"platform keys map to targets");
	}

	{ // Dormancy rescue also works for beta packages on canary-public.
		const auto beta = BuildSignedEnvelope(
			Channel::Beta,
			MakeUpdateVersion(5000001, 0),
			manifestJson,
			manifestSig,
			{ &rl, &rc },
			payload);
		Check(verify(beta, Channel::CanaryPublic, false, runningCanary)
				.has_value(),
			"beta with greater base accepted on canary-public");
	}

	std::cout << (TotalChecks - FailedChecks) << "/" << TotalChecks
		<< " checks passed." << std::endl;
	return FailedChecks ? 1 : 0;
}

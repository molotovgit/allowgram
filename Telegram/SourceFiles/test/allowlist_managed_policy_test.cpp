/* Public synthetic interop tests. Never opens an Allowgram profile. */
#include "main/allowlist_managed_policy.h"
#include <QtCore/QCoreApplication>
#include <QtCore/QFile>
#include <QtCore/QJsonArray>
#include <QtCore/QJsonDocument>
#include <cstdio>
using namespace Main::Managed;
int main(int argc, char **argv) {
    QCoreApplication application(argc, argv);
    if (argc != 3) return 2;
    QFile file(QString::fromLocal8Bit(argv[1]));
    if (!file.open(QIODevice::ReadOnly)) return 3;
    const auto data = QJsonDocument::fromJson(file.readAll()).object();
    const auto key = QByteArray::fromBase64(data["public_key"].toString().toLatin1(), QByteArray::Base64UrlEncoding);
    const auto subject = data["subject"].toString();
    const auto device = data["device"].toString();
    const auto now = qint64(data["now"].toDouble());
    int total = 0, failures = 0;
    QJsonArray results;
    const auto check = [&](const QString &name, bool ok) {
        ++total; if (!ok) ++failures;
        printf("%s: %s\n", ok ? "PASS" : "FAIL", name.toUtf8().constData());
        results.append(QJsonObject{{"name",name},{"passed",ok}});
    };
    for (const auto &value : data["vectors"].toArray()) {
        const auto v = value.toObject();
        const auto raw = v.contains("raw_envelope") ? v["raw_envelope"].toString().toUtf8()
            : QJsonDocument(v["envelope"].toObject()).toJson(QJsonDocument::Compact);
        const auto previous = QByteArray::fromBase64(v["previous"].toString().toLatin1(),QByteArray::Base64UrlEncoding);
        const auto result = VerifyPolicy(raw,key,subject,device,qint64(v["floor"].toDouble()),previous,now);
        check(v["name"].toString(),bool(result)==v["expected"].toBool()
            && (!result || int(result->peers.size())==v["count"].toInt()));
    }
    const auto inviteText = data["invitation"].toString();
    const auto invitation = ReadInvitation(inviteText, subject);
    check("valid current-user invitation", bool(invitation));
    check("invitation wrong signed-in user", !ReadInvitation(inviteText, QStringLiteral("90002")));
    check("invitation invalid base64", !ReadInvitation(inviteText+QStringLiteral("="),subject));
    auto invite = QJsonDocument::fromJson(QByteArray::fromBase64(inviteText.mid(5).toLatin1(),QByteArray::Base64UrlEncoding)).object();
    const auto encodedInvite = [&](QJsonObject v) {
        return QStringLiteral("AGH1.")+QString::fromLatin1(QJsonDocument(v).toJson(QJsonDocument::Compact).toBase64(QByteArray::Base64UrlEncoding|QByteArray::OmitTrailingEquals));
    };
    for (const auto &url : {"http://board.example", "https://u:p@board.example", "https://board.example/other", "https://board.example?q=1", "https://board.example#fragment"}) {
        auto altered = invite; altered["url"] = QString::fromLatin1(url);
        check(QStringLiteral("reject origin ")+QString::fromLatin1(url), !ReadInvitation(encodedInvite(altered),subject));
    }
    const auto response = QJsonDocument(data["enrollment"].toObject()).toJson(QJsonDocument::Compact);
    const auto state = invitation ? ReadEnrollment(response,*invitation,now) : std::optional<State>();
    check("verified enrollment", bool(state));
    if (state) {
        const auto saved = SaveState(*state);
        check("cached state roundtrip", bool(ReadState(saved,subject,now)));
        check("cached state wrong account", !ReadState(saved,QStringLiteral("90002"),now));
        check("cached state truncated", !ReadState(saved.left(saved.size()/2),subject,now));
    } else {
        check("cached state roundtrip",false);
    }
    check("reject duplicate keys at every depth", !ReadObject("{\"x\":{\"a\":1,\"a\":2}}"));
    check("reject escaped duplicate keys", !ReadObject("{\"v\":1,\"\\u0076\":2}"));
    check("reject malformed JSON", !ReadObject("{bad"));
    check("reject non-object JSON", !ReadObject("[]"));
    check("reject oversized wire body", !ReadObject(QByteArray(MaxWireBytes+1,'x')));
    QFile receipt(QString::fromLocal8Bit(argv[2]));
    if (!receipt.open(QIODevice::WriteOnly)) return 4;
    receipt.write(QJsonDocument(QJsonObject{{"finished",true},{"synthetic",true},{"tests",total},{"failures",failures},{"results",results}}).toJson());
    printf("Native managed policy: %d checks, %d failures\n",total,failures);
    return failures ? 1 : 0;
}

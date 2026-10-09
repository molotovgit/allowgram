/* Synthetic transport tests: no Telegram profile or external network. */
#include "main/allowlist_managed_client.h"
#include <QtCore/QCoreApplication>
#include <QtCore/QElapsedTimer>
#include <QtCore/QFile>
#include <QtCore/QDateTime>
#include <QtCore/QJsonArray>
#include <QtCore/QJsonDocument>
#include <QtCore/QThread>
#include <QtNetwork/QNetworkRequest>
#include <deque>
#include <cstdio>
#include <cstring>
using namespace Main;
namespace {
struct Response { int status = 200; QByteArray bytes; };
class Reply final : public QNetworkReply {
public:
 Reply(const QNetworkRequest &request, Response response, QObject *parent) : QNetworkReply(parent), _bytes(std::move(response.bytes)) {
  setRequest(request); setUrl(request.url()); setAttribute(QNetworkRequest::HttpStatusCodeAttribute,response.status);
  open(QIODevice::ReadOnly|QIODevice::Unbuffered);
  QTimer::singleShot(0,this,[this]{ if(_sent)return; _sent=true;setFinished(true);emit readyRead();emit finished(); });
 }
 void abort() override { if(_sent)return;_sent=true;setError(OperationCanceledError,QStringLiteral("test canceled"));setFinished(true);emit finished(); }
 qint64 bytesAvailable() const override { return _bytes.size()-_at+QNetworkReply::bytesAvailable(); }
 bool isSequential() const override { return true; }
protected:
 qint64 readData(char *data,qint64 size) override { const auto n=qMin(size,qint64(_bytes.size()-_at));if(!n)return -1;memcpy(data,_bytes.constData()+_at,size_t(n));_at+=n;return n; }
private:
 QByteArray _bytes; qint64 _at=0; bool _sent=false;
};
class Network final : public QNetworkAccessManager {
public:
 struct Call { Operation operation; QNetworkRequest request; QByteArray body; };
 std::deque<Response> responses;
 std::vector<Call> calls;
 std::function<bool()> applied;
 std::vector<bool> appliedFlags;
protected:
 QNetworkReply *createRequest(Operation op,const QNetworkRequest &request,QIODevice *outgoing=nullptr) override {
  calls.push_back({op,request,outgoing?outgoing->readAll():QByteArray()});
  appliedFlags.push_back(applied ? applied() : false);
  auto response=Response{503,{}};
  if(!responses.empty()){response=responses.front();responses.pop_front();}
  return new Reply(request,std::move(response),this);
 }
};
QByteArray json(const QJsonValue &value) {return QJsonDocument(value.toObject()).toJson(QJsonDocument::Compact);}
void settle(ManagedClient &client) {
 QElapsedTimer timer;timer.start();
 while(client.busy()&&timer.elapsed()<2500){QCoreApplication::processEvents(QEventLoop::AllEvents,20);QThread::msleep(1);}
}
}
int main(int argc,char **argv) {
 QCoreApplication app(argc,argv);if(argc!=3)return 2;
 QFile input(QString::fromLocal8Bit(argv[1]));if(!input.open(QIODevice::ReadOnly))return 3;
 const auto data=QJsonDocument::fromJson(input.readAll()).object();
 int total=0,failures=0;QJsonArray results;
 const auto check=[&](const QString &name,bool ok){++total;if(!ok)++failures;results.append(QJsonObject{{"name",name},{"passed",ok}});printf("%s: %s\n",ok?"PASS":"FAIL",name.toUtf8().constData());};
 const auto envelope=[&](const QString &name){for(const auto &v:data["vectors"].toArray())if(v.toObject()["name"].toString()==name)return json(v.toObject()["envelope"]);return QByteArray();};
 QByteArray stored;int commits=0;bool writes=true;
 auto host=ManagedClient::Host{data["subject"].toString(),[&]{return stored;},[]{return true;},[]{return std::vector<Managed::Peer>{{QStringLiteral("user"),23456}};},[&](const QByteArray &bytes){++commits;if(!writes)return false;stored=bytes;return true;}};
 Network network;ManagedClient client(host,nullptr,&network);client.start();
 check(QStringLiteral("unmanaged startup makes no request"),network.calls.empty()&&!client.paired());
 network.responses.push_back({201,json(data["enrollment"])});network.responses.push_back({200,"{\"ok\":true}"});
 check(QStringLiteral("pair begins"),client.pair(data["invitation"].toString()));settle(client);
 check(QStringLiteral("signed enrollment persisted before ACK"),client.paired()&&commits==1&&network.calls.size()==2);
 if(network.calls.size()>=2){
  check(QStringLiteral("enrollment has no bearer"),network.calls[0].request.rawHeader("Authorization").isEmpty());
  check(QStringLiteral("ACK uses bound device bearer"),network.calls[1].request.rawHeader("Authorization")==QByteArray("Bearer ")+QByteArray(43,'B'));
  const auto ack=QJsonDocument::fromJson(network.calls[1].body).object();
  check(QStringLiteral("ACK carries applied revision and digest"),ack["revision"].toInt()==2&&ack["policy_sha256"].toString().size()==64);
  check(QStringLiteral("redirects are never followed"),network.calls[0].request.attribute(QNetworkRequest::RedirectPolicyAttribute).toInt()==QNetworkRequest::ManualRedirectPolicy);
  check(QStringLiteral("browser cookies are disabled"),network.calls[0].request.attribute(QNetworkRequest::CookieLoadControlAttribute).toInt()==QNetworkRequest::Manual);
 }
 const auto prior=stored;
 for(const auto &bad: {QStringLiteral("rollback"),QStringLiteral("same revision different bytes"),QStringLiteral("bad signature"),QStringLiteral("wrong subject"),QStringLiteral("wrong device")}){
  const auto count=network.calls.size();const auto saved=commits;
  network.responses.push_back({200,envelope(bad)});client.refresh();settle(client);
  check(QStringLiteral("rejected without save or ACK: ")+bad,stored==prior&&commits==saved&&network.calls.size()==count+1);
 }
 const auto oldCount=network.calls.size();network.responses.push_back({401,"{}"});client.refresh();settle(client);
 check(QStringLiteral("revocation retains last policy without ACK"),stored==prior&&network.calls.size()==oldCount+1);
 const auto redirectCount=network.calls.size();network.responses.push_back({302,"{}"});client.refresh();settle(client);
 check(QStringLiteral("redirect response retains policy"),stored==prior&&network.calls.size()==redirectCount+1);
 const auto largeCount=network.calls.size();network.responses.push_back({200,QByteArray(Managed::MaxWireBytes+1,'x')});client.refresh();settle(client);
 check(QStringLiteral("oversized response retains policy"),stored==prior&&network.calls.size()==largeCount+1);
 const auto failCount=network.calls.size();writes=false;network.responses.push_back({200,envelope(QStringLiteral("valid deny-all"))});client.refresh();settle(client);
 check(QStringLiteral("failed durable write sends no ACK"),stored==prior&&network.calls.size()==failCount+1);
 writes=true;network.responses.push_back({200,envelope(QStringLiteral("valid deny-all"))});network.responses.push_back({200,"{\"ok\":true}"});client.refresh();settle(client);
 const auto empty=Managed::ReadState(stored,data["subject"].toString(),1700000000);
 const auto policy=empty?Managed::VerifyPolicy(empty->envelope,empty->publicKey,empty->subject,empty->device,0,{},1700000000):std::optional<Managed::Policy>();
 check(QStringLiteral("empty policy remains managed and acknowledged"),policy&&policy->revision==3&&policy->peers.empty()&&client.paired());
 client.stop();const auto stopped=network.calls.size();client.refresh();
 check(QStringLiteral("stopped/logout client makes no further request"),network.calls.size()==stopped);
 {
  Network n;ManagedClient restored(host,nullptr,&n);n.responses.push_back({401,"{}"});restored.start();settle(restored);
  check(QStringLiteral("cached policy survives denied reconnect"),restored.paired()&&stored!=QByteArray()&&n.calls.size()==1);
 }
 stored="corrupt";Network corrupt;ManagedClient locked(host,nullptr,&corrupt);locked.start();
 check(QStringLiteral("corrupt managed state cannot reopen pairing"),locked.paired()&&!locked.pair(data["invitation"].toString())&&corrupt.calls.empty());
 stored.clear();
 QByteArray connection = json(QJsonObject{{"v",1},{"decision","pending"},{"subject",host.subject},
  {"token",QString(43,QChar('A'))},{"origin","https://allowgram-head-production.up.railway.app"},
  {"public_key","emcLYc3W7m8Gv1y9r_cLBug_WZuzjFl1iMaNiV1ZYKw"},{"request",QJsonObject{{"telegram_user_id",host.subject},
   {"initial_peers",QJsonArray{QJsonObject{{"kind","user"},{"id","23456"}}}},
   {"device_name","Allowgram Desktop"},{"client_version","7.2.8.12"},{"consent_version",1}}}});
 host.connectionStored=[&]{return connection;};
 host.connectionCommit=[&](const QByteArray &bytes){if(!writes)return false;connection=bytes;return true;};
 {
  Network n;ManagedClient pending(host,nullptr,&n);pending.start();settle(pending);
  check(QStringLiteral("pending consent resumes registration after restart"),n.calls.size()==1&&!pending.paired());
  if(!n.calls.empty()) {
   check(QStringLiteral("registration endpoint is pinned stable HTTPS"),n.calls[0].request.url().toString()==QStringLiteral("https://allowgram-head-production.up.railway.app/api/client/register"));
   check(QStringLiteral("retry retains durable authorization"),n.calls[0].request.rawHeader("Authorization")==QByteArray("Bearer ")+QByteArray(43,'A'));
  }
 }
 const auto pendingConnection = connection;
 connection.clear();
 {
  Network n;ManagedClient anonymous(host,nullptr,&n);anonymous.start();
  check(QStringLiteral("no management request before explicit consent"),n.calls.empty()&&anonymous.needsConsent());
  check(QStringLiteral("automatic consent prompt is claimed once per session"),anonymous.claimConsentPrompt()&&!anonymous.claimConsentPrompt());
  writes=false;check(QStringLiteral("consent disk failure prevents registration"),!anonymous.connectDashboard()&&n.calls.empty()&&connection.isEmpty());writes=true;
  check(QStringLiteral("decline persists without management traffic"),anonymous.declineDashboard()&&n.calls.empty()&&!connection.isEmpty());
 }
 {
  Network n;ManagedClient declined(host,nullptr,&n);declined.start();
  check(QStringLiteral("decline survives restart and suppresses prompt"),!declined.needsConsent()&&n.calls.empty());
 }
 connection.clear();
 QByteArray durableRequest,durableBearer;
 {
  Network n;ManagedClient consent(host,nullptr,&n);consent.start();
  check(QStringLiteral("explicit consent persists then sends registration"),consent.connectDashboard()&&!connection.isEmpty()&&n.calls.size()==1);
  durableRequest=n.calls.front().body;durableBearer=n.calls.front().request.rawHeader("Authorization");settle(consent);
  consent.stop();consent.refresh();check(QStringLiteral("logout stops pending retries"),n.calls.size()==1);
 }
 {
  Network n;ManagedClient retry(host,nullptr,&n);retry.start();settle(retry);
  check(QStringLiteral("lost registration response reuses exact durable token and request"),n.calls.size()==1&&n.calls.front().body==durableRequest&&n.calls.front().request.rawHeader("Authorization")==durableBearer);
 }
 connection=pendingConnection;
 {
  auto wrong=QJsonDocument::fromJson(connection).object();wrong["origin"]="https://attacker.invalid";connection=json(wrong);
  Network n;ManagedClient corruptConsent(host,nullptr,&n);corruptConsent.start();
  check(QStringLiteral("tampered pending origin never receives credentials"),n.calls.empty()&&!corruptConsent.paired());
 }
 connection=pendingConnection;
 const auto trust=Managed::ReadInvitation(data["invitation"].toString(),host.subject);
 auto injected=QJsonDocument::fromJson(connection).object();
 injected["origin"]=trust->origin.toString(QUrl::FullyEncoded);injected["public_key"]=QString::fromLatin1(trust->publicKey.toBase64(QByteArray::Base64UrlEncoding|QByteArray::OmitTrailingEquals));
 connection=json(injected);
 {
  Network n;n.applied=[&]{return !stored.isEmpty();};ManagedClient pending(host,nullptr,&n,*trust);
  auto status=QJsonObject{{"v",1},{"id",QString(32,QChar('c'))},{"status","pending"},{"telegram_user_id",host.subject},
   {"fingerprint",QStringLiteral("placeholder")}};
  pending.start();settle(pending);status["fingerprint"]=pending.connectionFingerprint();
  n.responses.push_back({200,json(status)});pending.refresh();settle(pending);
  check(QStringLiteral("pending approval preserves existing local permissions"),!pending.paired()&&stored.isEmpty()&&n.calls.size()==2);
  const auto policy=data["enrollment"].toObject()["policy"].toObject();
  status["status"]="approved";status["device_id"]=data["enrollment"].toObject()["device_id"];status["policy"]=policy;
  n.responses.push_back({200,json(status)});n.responses.push_back({200,QByteArray("{\"ok\":true}")});pending.refresh();settle(pending);
  check(QStringLiteral("approved consent verifies and persists policy before ACK"),pending.paired()&&!stored.isEmpty()&&n.calls.size()==4&&n.appliedFlags.back());
  check(QStringLiteral("promoted registration keeps same device bearer for ACK"),n.calls.back().request.rawHeader("Authorization")==QByteArray("Bearer ")+QByteArray(43,'A'));
 }
 stored.clear();connection=json(injected);
 {
  Network n;ManagedClient rejected(host,nullptr,&n,*trust);rejected.start();settle(rejected);
  const auto status=QJsonObject{{"v",1},{"id",QString(32,QChar('c'))},{"status","rejected"},{"telegram_user_id",host.subject},{"fingerprint",rejected.connectionFingerprint()}};
  n.responses.push_back({200,json(status)});rejected.refresh();settle(rejected);rejected.refresh();
  check(QStringLiteral("rejected registration stops retries without clearing local policy"),n.calls.size()==2&&stored.isEmpty()&&!rejected.paired());
 }
 stored=prior;host.liveSync=true;
 {
  Network n;n.responses.push_back({200,"{\"ok\":true}"});
  ManagedClient live(host,nullptr,&n);live.start();settle(live);
  check(QStringLiteral("live sync re-ACKs durable cached policy after restart"),n.calls.size()==1&&n.calls[0].request.url().path()==QStringLiteral("/api/client/ack"));
  const auto saved=commits;
  n.responses.push_back({204,{}});live.refresh();settle(live);
  check(QStringLiteral("unchanged long poll does not save or ACK"),n.calls.size()==2&&commits==saved&&n.calls.back().request.url().query()==QStringLiteral("after_revision=2&timeout=20"));
  n.responses.push_back({200,envelope(QStringLiteral("valid deny-all"))});n.responses.push_back({503,{}});live.refresh();settle(live);
  check(QStringLiteral("changed long poll saves policy before ACK attempt"),commits==saved+1&&n.calls.size()==4&&n.calls.back().request.url().path()==QStringLiteral("/api/client/ack"));
  n.responses.push_back({200,"{\"ok\":true}"});live.refresh();settle(live);
  check(QStringLiteral("lost ACK retries before another long poll"),n.calls.size()==5&&n.calls.back().request.url().path()==QStringLiteral("/api/client/ack"));
  n.responses.push_back({204,{}});live.refresh();settle(live);
  check(QStringLiteral("next wait uses newly applied revision"),n.calls.size()==6&&n.calls.back().request.url().query()==QStringLiteral("after_revision=3&timeout=20"));
  live.stop();
 }
 stored=prior;
 {
  Network n;n.responses.push_back({200,"{\"ok\":true}"});ManagedClient live(host,nullptr,&n);live.start();settle(live);
  n.responses.push_back({404,{}});live.refresh();settle(live);
  n.responses.push_back({200,envelope(QStringLiteral("valid deny-all"))});n.responses.push_back({200,"{\"ok\":true}"});live.refresh();settle(live);
  check(QStringLiteral("legacy head falls back to existing policy endpoint"),n.calls.size()==4&&n.calls[2].request.url().path()==QStringLiteral("/api/client/policy"));
  live.stop();
 }
 stored=prior;
 {
  Network n;n.responses.push_back({409,{}});ManagedClient stale(host,nullptr,&n);stale.start();settle(stale);
  n.responses.push_back({200,envelope(QStringLiteral("valid deny-all"))});n.responses.push_back({200,"{\"ok\":true}"});stale.refresh();settle(stale);
  check(QStringLiteral("stale ACK fetches newer policy instead of retrying forever"),n.calls.size()==3&&n.calls[1].request.url().path()==QStringLiteral("/api/client/policy/wait")&&stale.status().contains(QStringLiteral("acknowledged")));
  stale.stop();
 }
 stored.clear();connection.clear();int proofs=0;
 host.hubProof=[&](const QString &start,std::function<void(QByteArray)> done){++proofs;done("auth_date=1700000000&start_param="+start.toUtf8()+"&user=%7B%22id%22%3A"+host.subject.toUtf8()+"%7D&signature=synthetic");};
 const auto challenge=QJsonObject{{"v",1},{"challenge_id",QString(32,'c')},{"start_param",QStringLiteral("agc_")+QString(22,'N')},{"bot_id",QStringLiteral("8558994389")},{"bot_username",QStringLiteral("ClassAAssistant_bot")},{"expires_at",QDateTime::currentSecsSinceEpoch()+300}};
 auto hubResponse=QJsonObject{{"status","enrolled"},{"device_id",data["enrollment"].toObject()["device_id"]},{"head_url",trust->origin.toString(QUrl::FullyEncoded)},{"policy",data["enrollment"].toObject()["policy"]}};
 {
  Network n;n.applied=[&]{return !connection.isEmpty();};n.responses.push_back({200,json(challenge)});n.responses.push_back({200,json(hubResponse)});n.responses.push_back({200,"{\"ok\":true}"});
  ManagedClient automatic(host,nullptr,&n,*trust,QUrl(QStringLiteral("https://hub.invalid")));automatic.start();settle(automatic);
  check(QStringLiteral("Hub enrollment is automatic without optional consent"),!automatic.needsConsent()&&!automatic.claimConsentPrompt()&&proofs==1&&automatic.paired()&&n.calls.size()==3);
  if(n.calls.size()==3){
   const auto body=QJsonDocument::fromJson(n.calls[1].body).object();
   const auto saved=Managed::ReadState(stored,host.subject,QDateTime::currentSecsSinceEpoch());
   check(QStringLiteral("device identity is durable before challenge"),n.appliedFlags.front());
   check(QStringLiteral("Hub only receives hash, public key and device proof"),saved&&n.calls[0].request.rawHeader("Authorization").isEmpty()&&n.calls[1].request.rawHeader("Authorization").isEmpty()&&!n.calls[1].body.contains(saved->token)&&body["device"].toObject()["credential_hash"].toString().size()==64&&body["device_sig"].toString().size()==86);
   check(QStringLiteral("first picker snapshot is sent to Hub"),body["initial_peers"].toArray().size()==1&&body["initial_peers"].toArray()[0].toObject()["id"]==QStringLiteral("23456"));
   check(QStringLiteral("Hub proof is discarded after signed policy is saved"),!connection.contains("init_data")&&n.calls.back().request.url().path()==QStringLiteral("/api/client/ack"));
  }
  automatic.stop();
 }
 stored.clear();connection.clear();proofs=0;
 {
  auto notReady=host;notReady.configured=[]{return false;};Network n;
  ManagedClient firstPick(notReady,nullptr,&n,*trust,QUrl(QStringLiteral("https://hub.invalid")));firstPick.start();
  check(QStringLiteral("automatic enrollment never bypasses initial picker"),n.calls.empty()&&proofs==0&&connection.isEmpty());
 }
 {
  Network n;ManagedClient noConfig(host,nullptr,&n,*trust);noConfig.start();
  check(QStringLiteral("missing Hub trust is fail-closed without old opt-in"),n.calls.empty()&&proofs==0&&!noConfig.needsConsent());
 }
 QFile receipt(QString::fromLocal8Bit(argv[2]));if(!receipt.open(QIODevice::WriteOnly))return 4;
 receipt.write(QJsonDocument(QJsonObject{{"finished",true},{"synthetic",true},{"tests",total},{"failures",failures},{"results",results}}).toJson());
 printf("Native managed client: %d checks, %d failures\n",total,failures);return failures?1:0;
}

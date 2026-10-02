/* Included ONLY in the disposable offline native fixture's transport overlay. */
#pragma once
#include <QtCore/QCoreApplication>
#include <QtCore/QFile>
#include <QtCore/QJsonArray>
#include <QtCore/QJsonDocument>
#include <QtCore/QJsonObject>
#include <QtCore/QTimer>
#include <QtCore/QVariant>
#include <QtNetwork/QNetworkAccessManager>
#include <QtNetwork/QNetworkReply>
#include <QtNetwork/QNetworkRequest>
#include <cstring>
namespace AllowgramManagedFixture {
inline QJsonObject vectors() {
 QFile file(qEnvironmentVariable("ALLOWGRAM_MANAGED_FIXTURE_VECTORS"));
 return file.open(QIODevice::ReadOnly) ? QJsonDocument::fromJson(file.readAll()).object() : QJsonObject();
}
inline QByteArray json(const QJsonObject &object) { return QJsonDocument(object).toJson(QJsonDocument::Compact); }
class Reply final : public QNetworkReply {
public:
 Reply(const QNetworkRequest &request,int status,QByteArray bytes,QObject *parent)
 : QNetworkReply(parent), _bytes(std::move(bytes)) {
  setRequest(request);setUrl(request.url());setAttribute(QNetworkRequest::HttpStatusCodeAttribute,status);
  open(QIODevice::ReadOnly|QIODevice::Unbuffered);
  QTimer::singleShot(0,this,[this] {if(_done)return;_done=true;setFinished(true);Q_EMIT readyRead();Q_EMIT finished();});
 }
 void abort() override {if(_done)return;_done=true;setError(OperationCanceledError,QStringLiteral("fixture canceled"));setFinished(true);Q_EMIT finished();}
 qint64 bytesAvailable() const override {return _bytes.size()-_at+QNetworkReply::bytesAvailable();}
 bool isSequential() const override {return true;}
protected:
 qint64 readData(char *data,qint64 size) override {const auto n=qMin(size,qint64(_bytes.size()-_at));if(!n)return -1;memcpy(data,_bytes.constData()+_at,size_t(n));_at+=n;return n;}
private:
 QByteArray _bytes;qint64 _at=0;bool _done=false;
};
class Network final : public QNetworkAccessManager {
public:
 explicit Network(QObject *parent):QNetworkAccessManager(parent),_vectors(vectors()){}
protected:
 QNetworkReply *createRequest(Operation operation,const QNetworkRequest &request,QIODevice *outgoing=nullptr) override {
  const auto path=request.url().path();
  auto bytes=QByteArray();auto status=503;
  if (!_vectors.isEmpty() && operation==PostOperation && path==QStringLiteral("/api/client/enroll")) {
   status=200;bytes=json(_vectors[QStringLiteral("enrollment")].toObject());
  } else if (!_vectors.isEmpty() && operation==GetOperation && path==QStringLiteral("/api/client/policy")) {
   ++_gets;status=200;
   if(_gets==2) bytes=json(_vectors[QStringLiteral("restored")].toObject());
   else if(_gets>2) bytes=json(_vectors[QStringLiteral("enrollment")].toObject()[QStringLiteral("policy")].toObject());
   else for(const auto &item:_vectors[QStringLiteral("vectors")].toArray()) {
    const auto row=item.toObject();if(row[QStringLiteral("name")].toString()==QStringLiteral("valid deny-all"))bytes=json(row[QStringLiteral("envelope")].toObject());
   }
  } else if (operation==PostOperation && path==QStringLiteral("/api/client/ack") && outgoing) {
   const auto ack=QJsonDocument::fromJson(outgoing->readAll()).object();
   const auto app=QCoreApplication::instance();
   app->setProperty("managedFixtureAckCount",app->property("managedFixtureAckCount").toInt()+1);
   app->setProperty("managedFixtureAckRevision",ack[QStringLiteral("revision")].toInt());
   status=200;bytes=QByteArray("{\"ok\":true}");
  }
  return new Reply(request,status,std::move(bytes),this);
 }
private:
 QJsonObject _vectors;int _gets=0;
};
inline QNetworkAccessManager *create(QObject *parent) {return new Network(parent);}
}

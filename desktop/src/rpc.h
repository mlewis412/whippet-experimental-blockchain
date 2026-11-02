#pragma once
#include <QtCore>
#include <QtNetwork>
#include <functional>

class Rpc : public QObject {
public:
    using Callback = std::function<void(QJsonValue, QString)>;
    explicit Rpc(QObject* parent = nullptr) : QObject(parent), manager(this) { manager.setProxy(QNetworkProxy::NoProxy); }
    void configure(int port, const QString& cookie) { rpcPort = port; cookiePath = cookie; }
    QNetworkReply* call(const QString& method, const QJsonArray& params, Callback callback, int timeout = 20000);
    int inFlight() const { return pending; }
private:
    QNetworkAccessManager manager;
    QString cookiePath;
    int rpcPort = 33675;
    int pending = 0;
    qint64 sequence = 0;
};

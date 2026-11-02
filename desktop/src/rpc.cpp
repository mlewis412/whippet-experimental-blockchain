#include "rpc.h"

QNetworkReply* Rpc::call(const QString& method, const QJsonArray& params, Callback callback, int timeout) {
    QFile file(cookiePath);
    if (!file.open(QIODevice::ReadOnly)) {
        QTimer::singleShot(0, this, [callback] { callback({}, "Node is starting. Waiting for its local connection."); });
        return nullptr;
    }
    const QByteArray cookie = file.readAll().trimmed();
    if (!cookie.contains(':')) {
        QTimer::singleShot(0, this, [callback] { callback({}, "Local node authentication is unavailable."); });
        return nullptr;
    }
    QNetworkRequest req(QUrl(QString("http://127.0.0.1:%1/").arg(rpcPort)));
    req.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");
    req.setRawHeader("Authorization", "Basic " + cookie.toBase64());
    req.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::ManualRedirectPolicy);
    req.setTransferTimeout(timeout);
    const QJsonObject body{{"jsonrpc", "1.0"}, {"id", ++sequence}, {"method", method}, {"params", params}};
    QNetworkReply* reply = manager.post(req, QJsonDocument(body).toJson(QJsonDocument::Compact));
    ++pending;
    connect(reply, &QNetworkReply::finished, this, [this, reply, callback] {
        --pending;
        const QByteArray bytes = reply->readAll();
        const auto doc = QJsonDocument::fromJson(bytes);
        const auto obj = doc.object();
        QString error;
        if (reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt() == 401)
            error = "The local node could not authenticate. Restart the node from Settings.";
        else if (obj.value("error").isObject())
            error = obj.value("error").toObject().value("message").toString("Node request failed.");
        else if (reply->error() != QNetworkReply::NoError)
            error = "The local node did not respond. It may still be starting.";
        else if (!obj.contains("result"))
            error = "The node returned an unreadable response.";
        callback(obj.value("result"), error);
        reply->deleteLater();
    });
    return reply;
}

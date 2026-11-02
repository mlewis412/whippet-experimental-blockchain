#include <QtTest>
#include <QTcpServer>
#include <QTcpSocket>
#include "model.h"
#include "rpc.h"

class Tests : public QObject {
    Q_OBJECT
private slots:
    void amounts() {
        qint64 n=0;
        QVERIFY(Model::parseAmount("0.00000001",n)); QCOMPARE(n,qint64(1));
        QVERIFY(Model::parseAmount("123.12345678",n)); QCOMPARE(n,qint64(12312345678));
        for (const QString& s : {"0", "-1", "1e5", "1.000000001", "NaN", "1,000", "999999999999999999999"}) QVERIFY2(!Model::parseAmount(s,n),qPrintable(s));
    }
    void consensusBoundary() {
        QVERIFY(!Model::needsAuxpow("main",371336)); QVERIFY(Model::needsAuxpow("main",371337));
        QVERIFY(!Model::needsAuxpow("test",158099)); QVERIFY(Model::needsAuxpow("test",158100));
        QVERIFY(!Model::needsAuxpow("regtest",19)); QVERIFY(Model::needsAuxpow("regtest",20));
        QVERIFY(!Model::canMine("main",true,8,true)); QVERIFY(!Model::canMine("main",false,0,true));
        QVERIFY(Model::canMine("regtest",true,0,true)); QVERIFY(!Model::canMine("regtest",false,0,false));
    }
    void missingCookie() {
        Rpc rpc; rpc.configure(1,"/no/such/cookie"); bool done=false;
        rpc.call("getinfo",{},[&](QJsonValue,QString e){QVERIFY(!e.isEmpty());done=true;});
        QTRY_VERIFY(done); QCOMPARE(rpc.inFlight(),0);
    }
    void rpcErrorResponse() {
        QTemporaryDir dir; QFile cookie(dir.filePath(".cookie")); QVERIFY(cookie.open(QIODevice::WriteOnly));cookie.write("user:secret");cookie.close();
        QTcpServer server; QVERIFY(server.listen(QHostAddress::LocalHost,0));
        connect(&server,&QTcpServer::newConnection,this,[&]{auto* socket=server.nextPendingConnection();connect(socket,&QTcpSocket::readyRead,socket,[socket]{QByteArray bytes=socket->property("request").toByteArray()+socket->readAll();socket->setProperty("request",bytes);if(!bytes.contains("\r\n\r\n"))return;QVERIFY(bytes.toLower().contains("authorization: basic dxnlcjpzzwnyzxq="));QByteArray body=R"({"result":null,"error":{"code":-5,"message":"Invalid address"},"id":1})";socket->write("HTTP/1.1 500 Internal Server Error\r\nContent-Type: application/json\r\nContent-Length: "+QByteArray::number(body.size())+"\r\nConnection: close\r\n\r\n"+body);socket->disconnectFromHost();});});
        Rpc rpc;rpc.configure(server.serverPort(),cookie.fileName());bool done=false;
        rpc.call("validateaddress",{"bad"},[&](QJsonValue,QString e){QCOMPARE(e,QString("Invalid address"));done=true;});QTRY_VERIFY(done);QCOMPARE(rpc.inFlight(),0);
    }
};
QTEST_GUILESS_MAIN(Tests)
#include "tests.moc"

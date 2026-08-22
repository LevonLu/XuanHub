// PyBridgeProtocolTest.cpp — Python Bridge 帧限制与 V1/V2 握手兼容测试
#include "PyBridgeProtocol.h"

#include <QJsonArray>
#include <QtEndian>
#include <QtTest>

using namespace xuanhub::pybridge;

class PyBridgeProtocolTest final : public QObject {
    Q_OBJECT
private Q_SLOTS:
    void decodesFragmentedAndAdjacentFrames() {
        const QByteArray first = FrameCodec::encode({
            {QStringLiteral("type"), QStringLiteral("one")}});
        const QByteArray second = FrameCodec::encode({
            {QStringLiteral("type"), QStringLiteral("two")}});
        QVERIFY(!first.isEmpty());
        QVERIFY(!second.isEmpty());

        FrameCodec codec;
        codec.append(first.left(3));
        QJsonObject message;
        QCOMPARE(codec.takeNext(&message), DecodeResult::NeedMore);
        codec.append(first.mid(3) + second);
        QCOMPARE(codec.takeNext(&message), DecodeResult::Ready);
        QCOMPARE(message.value(QStringLiteral("type")).toString(), QStringLiteral("one"));
        QCOMPARE(codec.takeNext(&message), DecodeResult::Ready);
        QCOMPARE(message.value(QStringLiteral("type")).toString(), QStringLiteral("two"));
        QCOMPARE(codec.takeNext(&message), DecodeResult::NeedMore);
    }

    void rejectsOversizedFrameHeader() {
        QByteArray header(sizeof(quint32), Qt::Uninitialized);
        qToBigEndian<quint32>(MaxMessageBytes + 1,
                              reinterpret_cast<uchar*>(header.data()));
        FrameCodec codec;
        codec.append(header);
        QJsonObject message;
        QString error;
        QCOMPARE(codec.takeNext(&message, &error), DecodeResult::Error);
        QVERIFY(error.contains(QStringLiteral("size")));
    }

    void rejectsDeepJsonBeforeEncoding() {
        QJsonValue nested = QStringLiteral("leaf");
        for (int i = 0; i < MaxJsonDepth + 2; ++i)
            nested = QJsonObject{{QStringLiteral("child"), nested}};
        QString error;
        QVERIFY(FrameCodec::encode(nested.toObject(), &error).isEmpty());
        QVERIFY(error.contains(QStringLiteral("nesting")));
    }

    void acceptsLegacyHello() {
        WorkerHello hello;
        QString error;
        QVERIFY(parseWorkerHello({
            {QStringLiteral("type"), QStringLiteral("hello")},
            {QStringLiteral("token"), QStringLiteral("secret")}},
            QStringLiteral("secret"), QStringLiteral("py_demo"), &hello, &error));
        QCOMPARE(hello.protocolVersion, 1);
        QVERIFY(hello.capabilities.isEmpty());
    }

    void acceptsVersionTwoHello() {
        WorkerHello hello;
        QString error;
        QVERIFY(parseWorkerHello({
            {QStringLiteral("type"), QStringLiteral("hello")},
            {QStringLiteral("token"), QStringLiteral("secret")},
            {QStringLiteral("protocol"), 2},
            {QStringLiteral("pluginId"), QStringLiteral("py_demo")},
            {QStringLiteral("capabilities"), QJsonArray{
                QStringLiteral("requestResponseV1"),
                QStringLiteral("hostServicesV1")}}},
            QStringLiteral("secret"), QStringLiteral("py_demo"), &hello, &error));
        QCOMPARE(hello.protocolVersion, 2);
        QCOMPARE(hello.pluginId, QStringLiteral("py_demo"));
        QCOMPARE(hello.capabilities.size(), 2);
    }

    void rejectsUnsupportedOrMismatchedHello() {
        WorkerHello hello;
        QString error;
        QVERIFY(!parseWorkerHello({
            {QStringLiteral("type"), QStringLiteral("hello")},
            {QStringLiteral("token"), QStringLiteral("secret")},
            {QStringLiteral("protocol"), 3},
            {QStringLiteral("pluginId"), QStringLiteral("py_demo")},
            {QStringLiteral("capabilities"), QJsonArray{}}},
            QStringLiteral("secret"), QStringLiteral("py_demo"), &hello, &error));
        QVERIFY(error.contains(QStringLiteral("unsupported")));

        QVERIFY(!parseWorkerHello({
            {QStringLiteral("type"), QStringLiteral("hello")},
            {QStringLiteral("token"), QStringLiteral("secret")},
            {QStringLiteral("protocol"), 2},
            {QStringLiteral("pluginId"), QStringLiteral("py_other")},
            {QStringLiteral("capabilities"), QJsonArray{}}},
            QStringLiteral("secret"), QStringLiteral("py_demo"), &hello, &error));
        QVERIFY(error.contains(QStringLiteral("identity")));
    }
};

QTEST_GUILESS_MAIN(PyBridgeProtocolTest)
#include "PyBridgeProtocolTest.moc"

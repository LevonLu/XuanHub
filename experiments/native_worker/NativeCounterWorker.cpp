// NativeCounterWorker.cpp — PF-11 独立原生计数器 Worker 技术原型
#include "PyBridgeProtocol.h"

#include <QCoreApplication>
#include <QFile>
#include <QJsonArray>
#include <QJsonObject>
#include <QProcessEnvironment>

#include <cstdio>
#include <cstdlib>
#include <windows.h>

using xuanhub::pybridge::DecodeResult;
using xuanhub::pybridge::FrameCodec;

namespace {
bool writeMessage(QFile* output, const QJsonObject& message) {
    QString error;
    const QByteArray frame = FrameCodec::encode(message, &error);
    return !frame.isEmpty() && output->write(frame) == frame.size() && output->flush();
}

QJsonObject response(qint64 id, bool ok, const QJsonValue& result = {},
                     const QString& errorCode = {}, const QString& errorMessage = {}) {
    QJsonObject message{{QStringLiteral("type"), QStringLiteral("response")},
                        {QStringLiteral("id"), id},
                        {QStringLiteral("ok"), ok}};
    if (ok) message.insert(QStringLiteral("result"), result);
    else message.insert(QStringLiteral("error"), QJsonObject{
        {QStringLiteral("code"), errorCode},
        {QStringLiteral("message"), errorMessage}});
    return message;
}
} // namespace

int main(int argc, char* argv[]) {
    QCoreApplication app(argc, argv);
    if (app.arguments().size() != 2) return 2;
    const QString pluginId = app.arguments().at(1);
    const QString token = qEnvironmentVariable("XUANHUB_NATIVE_TOKEN");
    if (pluginId.isEmpty() || token.isEmpty()) return 3;

    QFile output;
    if (!output.open(stdout, QIODevice::WriteOnly, QFileDevice::DontCloseHandle)) {
        return 4;
    }
    if (!writeMessage(&output, {
            {QStringLiteral("type"), QStringLiteral("hello")},
            {QStringLiteral("token"), token},
            {QStringLiteral("protocol"), 2},
            {QStringLiteral("pluginId"), pluginId},
            {QStringLiteral("capabilities"), QJsonArray{
                QStringLiteral("requestResponseV1"),
                QStringLiteral("nativeWorkerPrototype")}}})) {
        return 5;
    }

    FrameCodec codec;
    bool initialized = false;
    qint64 counter = 0;
    while (true) {
        QByteArray chunk(4096, Qt::Uninitialized);
        DWORD bytesRead = 0;
        if (!ReadFile(GetStdHandle(STD_INPUT_HANDLE), chunk.data(),
                      static_cast<DWORD>(chunk.size()), &bytesRead, nullptr)
            || bytesRead == 0) {
            return 0;
        }
        chunk.resize(static_cast<qsizetype>(bytesRead));
        codec.append(chunk);
        QJsonObject message;
        QString decodeError;
        const DecodeResult decoded = codec.takeNext(&message, &decodeError);
        if (decoded == DecodeResult::NeedMore) continue;
        if (decoded == DecodeResult::Error) return 6;
        if (message.value(QStringLiteral("type")).toString() != QLatin1String("request"))
            return 7;

        const qint64 id = message.value(QStringLiteral("id")).toInteger(-1);
        const QString method = message.value(QStringLiteral("method")).toString();
        const QJsonObject args = message.value(QStringLiteral("args")).toObject();
        if (id < 0 || method.isEmpty()) return 8;
        if (method == QLatin1String("init")) {
            initialized = true;
            counter = args.value(QStringLiteral("initial")).toInteger(0);
            writeMessage(&output, response(id, true, QJsonObject{
                {QStringLiteral("initialized"), true}}));
        } else if (!initialized) {
            writeMessage(&output, response(id, false, {},
                QStringLiteral("not_initialized"), QStringLiteral("init is required")));
        } else if (method == QLatin1String("update")) {
            writeMessage(&output, response(id, true, QJsonObject{
                {QStringLiteral("counter"), counter}}));
        } else if (method == QLatin1String("invoke")) {
            const QString name = args.value(QStringLiteral("name")).toString();
            if (name == QLatin1String("increment")) {
                counter += args.value(QStringLiteral("amount")).toInteger(1);
                writeMessage(&output, response(id, true, counter));
            } else if (name == QLatin1String("crash")) {
                std::abort();
            } else if (name == QLatin1String("hang")) {
                Sleep(INFINITE);
            } else {
                writeMessage(&output, response(id, false, {},
                    QStringLiteral("unknown_action"), QStringLiteral("unknown action")));
            }
        } else if (method == QLatin1String("stop")) {
            writeMessage(&output, response(id, true));
            return 0;
        } else {
            writeMessage(&output, response(id, false, {},
                QStringLiteral("unknown_method"), QStringLiteral("unknown method")));
        }
    }
}

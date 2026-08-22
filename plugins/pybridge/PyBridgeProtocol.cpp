// PyBridgeProtocol.cpp — 有界 JSON 帧与向后兼容的 Worker 握手校验
#include "PyBridgeProtocol.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonParseError>
#include <QtEndian>

namespace xuanhub::pybridge {
namespace {
void setError(QString* error, const QString& text) {
    if (error) *error = text;
}

bool isSafeJson(const QJsonValue& value, int depth) {
    if (depth > MaxJsonDepth) return false;
    if (value.isArray()) {
        for (const QJsonValue& item : value.toArray()) {
            if (!isSafeJson(item, depth + 1)) return false;
        }
    } else if (value.isObject()) {
        const QJsonObject object = value.toObject();
        for (auto it = object.constBegin(); it != object.constEnd(); ++it) {
            if (!isSafeJson(it.value(), depth + 1)) return false;
        }
    }
    return true;
}
} // namespace

QByteArray FrameCodec::encode(const QJsonObject& message, QString* error) {
    if (!isSafeJson(message, 0)) {
        setError(error, QStringLiteral("JSON nesting exceeds protocol limit"));
        return {};
    }
    const QByteArray payload = QJsonDocument(message).toJson(QJsonDocument::Compact);
    if (payload.size() > static_cast<qsizetype>(MaxMessageBytes)) {
        setError(error, QStringLiteral("message exceeds protocol size limit"));
        return {};
    }
    QByteArray frame(sizeof(quint32), Qt::Uninitialized);
    qToBigEndian<quint32>(static_cast<quint32>(payload.size()),
                          reinterpret_cast<uchar*>(frame.data()));
    frame += payload;
    return frame;
}

DecodeResult FrameCodec::takeNext(QJsonObject* message, QString* error) {
    if (!message) {
        setError(error, QStringLiteral("output message is null"));
        return DecodeResult::Error;
    }
    if (m_buffer.size() < static_cast<qsizetype>(sizeof(quint32)))
        return DecodeResult::NeedMore;
    const auto* bytes = reinterpret_cast<const uchar*>(m_buffer.constData());
    const quint32 payloadSize = qFromBigEndian<quint32>(bytes);
    if (payloadSize > MaxMessageBytes) {
        setError(error, QStringLiteral("message exceeds protocol size limit"));
        return DecodeResult::Error;
    }
    const qsizetype frameSize = sizeof(quint32) + static_cast<qsizetype>(payloadSize);
    if (m_buffer.size() < frameSize) return DecodeResult::NeedMore;

    const QByteArray payload = m_buffer.mid(sizeof(quint32), payloadSize);
    m_buffer.remove(0, frameSize);
    QJsonParseError parseError{};
    const QJsonDocument document = QJsonDocument::fromJson(payload, &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isObject()) {
        setError(error, QStringLiteral("frame payload must be a JSON object"));
        return DecodeResult::Error;
    }
    if (!isSafeJson(document.object(), 0)) {
        setError(error, QStringLiteral("JSON nesting exceeds protocol limit"));
        return DecodeResult::Error;
    }
    *message = document.object();
    return DecodeResult::Ready;
}

bool parseWorkerHello(const QJsonObject& message,
                      const QString& expectedToken,
                      const QString& expectedPluginId,
                      WorkerHello* hello,
                      QString* error) {
    if (!hello) {
        setError(error, QStringLiteral("hello output is null"));
        return false;
    }
    if (message.value(QStringLiteral("type")).toString() != QLatin1String("hello")
        || expectedToken.isEmpty()
        || message.value(QStringLiteral("token")).toString() != expectedToken) {
        setError(error, QStringLiteral("worker authentication failed"));
        return false;
    }

    WorkerHello parsed;
    const QJsonValue protocolValue = message.value(QStringLiteral("protocol"));
    if (!protocolValue.isUndefined()) {
        if (!protocolValue.isDouble()
            || protocolValue.toDouble() != protocolValue.toInt()) {
            setError(error, QStringLiteral("worker protocol must be an integer"));
            return false;
        }
        parsed.protocolVersion = protocolValue.toInt();
    }
    if (parsed.protocolVersion < MinimumProtocolVersion
        || parsed.protocolVersion > CurrentProtocolVersion) {
        setError(error, QStringLiteral("unsupported worker protocol version"));
        return false;
    }

    if (parsed.protocolVersion >= 2) {
        parsed.pluginId = message.value(QStringLiteral("pluginId")).toString();
        if (parsed.pluginId.isEmpty() || parsed.pluginId != expectedPluginId) {
            setError(error, QStringLiteral("worker plugin identity mismatch"));
            return false;
        }
        const QJsonValue capabilitiesValue = message.value(QStringLiteral("capabilities"));
        if (!capabilitiesValue.isArray()) {
            setError(error, QStringLiteral("worker capabilities must be an array"));
            return false;
        }
        for (const QJsonValue& value : capabilitiesValue.toArray()) {
            if (!value.isString() || value.toString().isEmpty()) {
                setError(error, QStringLiteral("worker capability must be a non-empty string"));
                return false;
            }
            if (!parsed.capabilities.contains(value.toString()))
                parsed.capabilities.append(value.toString());
        }
    }
    *hello = parsed;
    return true;
}

} // namespace xuanhub::pybridge

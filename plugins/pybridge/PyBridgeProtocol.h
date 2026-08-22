// PyBridgeProtocol.h — Python Bridge 帧编解码与版本化握手契约
#pragma once

#include <QByteArray>
#include <QJsonObject>
#include <QString>
#include <QStringList>

namespace xuanhub::pybridge {

inline constexpr quint32 MaxMessageBytes = 4 * 1024 * 1024;
inline constexpr int MaxJsonDepth = 32;
inline constexpr int CurrentProtocolVersion = 2;
inline constexpr int MinimumProtocolVersion = 1;

enum class DecodeResult {
    NeedMore,
    Ready,
    Error,
};

class FrameCodec final {
public:
    static QByteArray encode(const QJsonObject& message, QString* error = nullptr);

    void append(const QByteArray& data) { m_buffer += data; }
    DecodeResult takeNext(QJsonObject* message, QString* error = nullptr);
    void clear() { m_buffer.clear(); }
    qsizetype bufferedBytes() const { return m_buffer.size(); }

private:
    QByteArray m_buffer;
};

struct WorkerHello {
    int protocolVersion = MinimumProtocolVersion;
    QString pluginId;
    QStringList capabilities;
};

// V1 hello 没有 protocol/pluginId/capabilities；V2 必须声明并绑定预期插件身份。
bool parseWorkerHello(const QJsonObject& message,
                      const QString& expectedToken,
                      const QString& expectedPluginId,
                      WorkerHello* hello,
                      QString* error = nullptr);

} // namespace xuanhub::pybridge

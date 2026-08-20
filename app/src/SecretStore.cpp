// SecretStore.cpp — Windows DPAPI 的小型 JSON 密钥库实现
#include "SecretStore.h"

#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>
#include <QDebug>

#include <windows.h>
#include <wincrypt.h>

namespace {
const QString kReferenceKey = QStringLiteral("$xuanhubSecret");

QJsonObject loadRoot(const QString& path) {
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) return {};
    QJsonParseError error{};
    const QJsonDocument doc = QJsonDocument::fromJson(file.readAll(), &error);
    if (error.error != QJsonParseError::NoError || !doc.isObject()) {
        qWarning() << "[Secrets] invalid secret store; ignoring it:" << path;
        return {};
    }
    return doc.object();
}

QByteArray protect(const QString& text) {
    const QByteArray input = text.toUtf8();
    DATA_BLOB plain{};
    plain.cbData = static_cast<DWORD>(input.size());
    plain.pbData = reinterpret_cast<BYTE*>(const_cast<char*>(input.constData()));
    DATA_BLOB encrypted{};
    if (!CryptProtectData(&plain, L"XuanHub plugin secret", nullptr, nullptr, nullptr,
                          CRYPTPROTECT_UI_FORBIDDEN, &encrypted)) {
        qWarning() << "[Secrets] CryptProtectData failed:" << GetLastError();
        return {};
    }
    const QByteArray result(reinterpret_cast<const char*>(encrypted.pbData),
                            static_cast<qsizetype>(encrypted.cbData));
    LocalFree(encrypted.pbData);
    return result;
}

std::optional<QString> unprotect(const QByteArray& encrypted) {
    if (encrypted.isEmpty()) return std::nullopt;
    DATA_BLOB cipher{};
    cipher.cbData = static_cast<DWORD>(encrypted.size());
    cipher.pbData = reinterpret_cast<BYTE*>(const_cast<char*>(encrypted.constData()));
    DATA_BLOB plain{};
    if (!CryptUnprotectData(&cipher, nullptr, nullptr, nullptr, nullptr,
                            CRYPTPROTECT_UI_FORBIDDEN, &plain)) {
        qWarning() << "[Secrets] CryptUnprotectData failed:" << GetLastError();
        return std::nullopt;
    }
    const QString result = QString::fromUtf8(reinterpret_cast<const char*>(plain.pbData),
                                             static_cast<qsizetype>(plain.cbData));
    LocalFree(plain.pbData);
    return result;
}
} // namespace

SecretStore::SecretStore(QString directory)
    : m_path(QDir(directory).absoluteFilePath(QStringLiteral("secrets.json"))) {}

QString SecretStore::idForPluginField(const QString& pluginId, const QString& fieldKey) {
    return QStringLiteral("plugin/") + pluginId + QLatin1Char('/') + fieldKey;
}

QJsonValue SecretStore::referenceValue(const QString& secretId) {
    return QJsonObject{{kReferenceKey, secretId}};
}

QString SecretStore::referenceId(const QJsonValue& value) {
    if (!value.isObject()) return {};
    const QJsonObject ref = value.toObject();
    if (ref.size() != 1 || !ref.value(kReferenceKey).isString()) return {};
    return ref.value(kReferenceKey).toString();
}

std::optional<QString> SecretStore::read(const QString& secretId) const {
    const QJsonObject root = loadRoot(m_path);
    const QString payload = root.value(QStringLiteral("secrets")).toObject().value(secretId).toString();
    if (payload.isEmpty()) return std::nullopt;
    return unprotect(QByteArray::fromBase64(payload.toLatin1(), QByteArray::Base64UrlEncoding));
}

bool SecretStore::write(const QString& secretId, const QString& plaintext) const {
    QJsonObject root = loadRoot(m_path);
    QJsonObject secrets = root.value(QStringLiteral("secrets")).toObject();
    if (plaintext.isEmpty()) {
        secrets.remove(secretId);
    } else {
        const QByteArray encrypted = protect(plaintext);
        if (encrypted.isEmpty()) return false;
        secrets.insert(secretId, QString::fromLatin1(
            encrypted.toBase64(QByteArray::Base64UrlEncoding | QByteArray::OmitTrailingEquals)));
    }
    root.insert(QStringLiteral("version"), 1);
    root.insert(QStringLiteral("secrets"), secrets);

    QSaveFile file(m_path);
    if (!file.open(QIODevice::WriteOnly)) {
        qWarning() << "[Secrets] cannot open secret store for writing:" << m_path;
        return false;
    }
    const QByteArray json = QJsonDocument(root).toJson(QJsonDocument::Compact);
    if (file.write(json) != json.size() || !file.commit()) {
        qWarning() << "[Secrets] cannot save secret store:" << m_path;
        return false;
    }
    // DPAPI 已绑定当前用户；仍尽量避免把密文文件开放给组/其他用户。
    QFile::setPermissions(m_path, QFileDevice::ReadOwner | QFileDevice::WriteOwner);
    return true;
}

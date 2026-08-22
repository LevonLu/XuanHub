// SecretStore.cpp — Windows DPAPI 的小型 JSON 密钥库实现
#include "SecretStore.h"

#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>
#include <QDebug>

#include <utility>

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

SecretStore::Transaction::Transaction(QString path)
    : m_path(std::move(path)), m_originalExists(QFile::exists(m_path)) {
    QFile file(m_path);
    if (file.open(QIODevice::ReadOnly)) m_originalBytes = file.readAll();
    m_root = loadRoot(m_path);
}

SecretStore::Transaction::~Transaction() {
    if (m_committed && !m_finalized && !rollback())
        qCritical() << "[Secrets] automatic rollback failed:" << m_path;
}

bool SecretStore::Transaction::stageWrite(const QString& secretId,
                                          const QString& plaintext) {
    if (m_committed || secretId.isEmpty()) return false;
    QJsonObject secrets = m_root.value(QStringLiteral("secrets")).toObject();
    if (plaintext.isEmpty()) {
        if (!secrets.contains(secretId)) return true;
        secrets.remove(secretId);
    } else {
        const QByteArray encrypted = protect(plaintext);
        if (encrypted.isEmpty()) return false;
        secrets.insert(secretId, QString::fromLatin1(
            encrypted.toBase64(QByteArray::Base64UrlEncoding
                               | QByteArray::OmitTrailingEquals)));
    }
    m_root.insert(QStringLiteral("version"), 1);
    m_root.insert(QStringLiteral("secrets"), secrets);
    m_changed = true;
    return true;
}

bool SecretStore::Transaction::writeBytes(const QByteArray& bytes) const {
    QSaveFile file(m_path);
    if (!file.open(QIODevice::WriteOnly)) {
        qWarning() << "[Secrets] cannot open secret store for writing:" << m_path;
        return false;
    }
    if (file.write(bytes) != bytes.size() || !file.commit()) {
        qWarning() << "[Secrets] cannot save secret store:" << m_path;
        return false;
    }
    QFile::setPermissions(m_path, QFileDevice::ReadOwner | QFileDevice::WriteOwner);
    return true;
}

bool SecretStore::Transaction::commit() {
    if (m_committed) return true;
    if (m_changed
        && !writeBytes(QJsonDocument(m_root).toJson(QJsonDocument::Compact))) {
        return false;
    }
    m_committed = true;
    return true;
}

bool SecretStore::Transaction::rollback() {
    if (!m_committed) return true;
    bool ok = true;
    if (m_changed) {
        if (m_originalExists) {
            ok = writeBytes(m_originalBytes);
        } else if (QFile::exists(m_path)) {
            ok = QFile::remove(m_path);
            if (!ok) qWarning() << "[Secrets] cannot remove rolled-back store:" << m_path;
        }
    }
    if (ok) {
        m_committed = false;
        m_finalized = true;
    }
    return ok;
}

QString SecretStore::idForPluginField(const QString& pluginId, const QString& fieldKey) {
    QString escaped = fieldKey;
    escaped.replace(QLatin1Char('~'), QStringLiteral("~0"));
    escaped.replace(QLatin1Char('/'), QStringLiteral("~1"));
    return idForPluginPointer(pluginId, QLatin1Char('/') + escaped);
}

QString SecretStore::idForPluginPointer(const QString& pluginId,
                                        const QString& jsonPointer) {
    if (pluginId.isEmpty() || !jsonPointer.startsWith(QLatin1Char('/'))) return {};
    return QStringLiteral("plugin/") + pluginId + jsonPointer;
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
    Transaction transaction(m_path);
    if (!transaction.stageWrite(secretId, plaintext) || !transaction.commit()) return false;
    transaction.finalize();
    return true;
}

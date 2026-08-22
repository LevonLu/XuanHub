// PluginPackageValidator.cpp — 先验证 ZIP 结构、哈希与 Manifest，再展开到临时目录
#include "PluginPackageValidator.h"

#include "PluginContractValidator.h"
#include "SemanticVersion.h"
#include "barplugin/HostServices.h"

#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonParseError>
#include <QMap>
#include <QRegularExpression>
#include <QSaveFile>
#include <QSet>
#include <QtCore/private/qzipreader_p.h>
#include <QtEndian>

#include <cstring>
#include <windows.h>
#include <bcrypt.h>

namespace {
constexpr quint32 kEndOfCentralDirectorySignature = 0x06054b50;
constexpr quint32 kCentralDirectorySignature = 0x02014b50;
constexpr quint32 kLocalFileSignature = 0x04034b50;
constexpr qint64 kMaximumZipComment = 65535;
constexpr qint64 kMaximumControlFileBytes = 4LL * 1024 * 1024;
constexpr auto kSignatureAlgorithm = "ecdsa-p256-sha256";
constexpr auto kSignatureTarget = "manifest.sha256";

struct ZipEntry {
    QString path;
    QByteArray rawPath;
    quint16 flags = 0;
    quint16 method = 0;
    quint32 crc32 = 0;
    quint32 compressedSize = 0;
    quint32 uncompressedSize = 0;
    quint32 localHeaderOffset = 0;
    bool directory = false;
    bool symbolicLink = false;
};

template <typename T>
T littleEndian(const QByteArray& bytes, qsizetype offset) {
    return qFromLittleEndian<T>(
        reinterpret_cast<const uchar*>(bytes.constData() + offset));
}

PluginPackageValidationResult failed(const QString& error) {
    PluginPackageValidationResult result;
    result.error = error;
    return result;
}

bool readExactly(QFile* file, qint64 offset, qint64 size,
                 QByteArray* output, QString* error) {
    if (offset < 0 || size < 0 || offset > file->size() - size
        || !file->seek(offset)) {
        *error = QStringLiteral("ZIP offset is outside the archive");
        return false;
    }
    *output = file->read(size);
    if (output->size() != size) {
        *error = QStringLiteral("cannot read complete ZIP structure");
        return false;
    }
    return true;
}

bool isWindowsDeviceName(const QString& segment) {
    QString base = segment.section(QLatin1Char('.'), 0, 0).toUpper();
    if (base == QLatin1String("CON") || base == QLatin1String("PRN")
        || base == QLatin1String("AUX") || base == QLatin1String("NUL")) {
        return true;
    }
    static const QRegularExpression numberedDevice(
        QStringLiteral(R"(^(COM|LPT)[1-9]$)"),
        QRegularExpression::CaseInsensitiveOption);
    return numberedDevice.match(base).hasMatch();
}

bool validateArchivePath(const QString& original, bool directory,
                         QString* normalized, QString* error) {
    if (original.isEmpty() || original.contains(QLatin1Char('\\'))
        || original.startsWith(QLatin1Char('/'))
        || original.contains(QLatin1Char(':'))
        || original.contains(QChar::Null)
        || original.contains(QStringLiteral("//"))) {
        *error = QStringLiteral("unsafe archive path: %1").arg(original);
        return false;
    }
    QString candidate = original;
    if (directory && candidate.endsWith(QLatin1Char('/'))) candidate.chop(1);
    if (candidate.isEmpty()) {
        *error = QStringLiteral("archive contains an empty root entry");
        return false;
    }
    const QStringList segments = candidate.split(QLatin1Char('/'));
    for (const QString& segment : segments) {
        if (segment.isEmpty() || segment == QLatin1String(".")
            || segment == QLatin1String("..")
            || segment.endsWith(QLatin1Char('.'))
            || segment.endsWith(QLatin1Char(' '))
            || isWindowsDeviceName(segment)) {
            *error = QStringLiteral("unsafe archive path segment in: %1").arg(original);
            return false;
        }
        for (const QChar ch : segment) {
            if (ch.unicode() < 0x20) {
                *error = QStringLiteral("archive path contains a control character");
                return false;
            }
        }
    }
    if (QDir::cleanPath(candidate) != candidate) {
        *error = QStringLiteral("archive path is not canonical: %1").arg(original);
        return false;
    }
    *normalized = candidate;
    return true;
}

bool validatePackageRoot(const ZipEntry& entry, QString* error) {
    const QString& path = entry.path;
    const bool payload = path == QLatin1String("payload")
        || path.startsWith(QStringLiteral("payload/"));
    if (payload) return true;
    if (entry.directory) {
        *error = QStringLiteral("unexpected top-level directory: %1").arg(path);
        return false;
    }
    if (path == QLatin1String("plugin.json")
        || path == QLatin1String("manifest.sha256")
        || path == QLatin1String("signature.json")
        || path == QLatin1String("LICENSE")
        || path == QLatin1String("README.md")) {
        return true;
    }
    *error = QStringLiteral("unexpected top-level file: %1").arg(path);
    return false;
}

bool validateLocalHeader(QFile* file, const ZipEntry& entry,
                         qint64 centralOffset, QString* error) {
    QByteArray header;
    if (!readExactly(file, entry.localHeaderOffset, 30, &header, error)) return false;
    if (littleEndian<quint32>(header, 0) != kLocalFileSignature) {
        *error = QStringLiteral("invalid local ZIP header for %1").arg(entry.path);
        return false;
    }
    const quint16 nameLength = littleEndian<quint16>(header, 26);
    const quint16 extraLength = littleEndian<quint16>(header, 28);
    QByteArray rawName;
    if (!readExactly(file, qint64(entry.localHeaderOffset) + 30,
                     nameLength, &rawName, error)) {
        return false;
    }
    const qint64 dataOffset = qint64(entry.localHeaderOffset) + 30
        + nameLength + extraLength;
    if (littleEndian<quint16>(header, 6) != entry.flags
        || littleEndian<quint16>(header, 8) != entry.method
        || rawName != entry.rawPath || dataOffset > centralOffset
        || qint64(entry.compressedSize) > centralOffset - dataOffset) {
        *error = QStringLiteral("local ZIP header disagrees with central directory: %1")
                     .arg(entry.path);
        return false;
    }
    return true;
}

bool readZipIndex(QFile* file, QVector<ZipEntry>* entries, QString* error) {
    const qint64 tailSize = qMin(file->size(), kMaximumZipComment + 22);
    QByteArray tail;
    if (!readExactly(file, file->size() - tailSize, tailSize, &tail, error)) return false;

    qsizetype eocdInTail = -1;
    for (qsizetype i = tail.size() - 22; i >= 0; --i) {
        if (littleEndian<quint32>(tail, i) != kEndOfCentralDirectorySignature)
            continue;
        const quint16 commentLength = littleEndian<quint16>(tail, i + 20);
        if (i + 22 + commentLength == tail.size()) {
            eocdInTail = i;
            break;
        }
    }
    if (eocdInTail < 0) {
        *error = QStringLiteral("ZIP end-of-central-directory record is missing");
        return false;
    }
    const QByteArray eocd = tail.mid(eocdInTail, 22);
    const quint16 disk = littleEndian<quint16>(eocd, 4);
    const quint16 centralDisk = littleEndian<quint16>(eocd, 6);
    const quint16 diskEntries = littleEndian<quint16>(eocd, 8);
    const quint16 totalEntries = littleEndian<quint16>(eocd, 10);
    const quint32 centralSize = littleEndian<quint32>(eocd, 12);
    const quint32 centralOffset = littleEndian<quint32>(eocd, 16);
    const qint64 eocdOffset = file->size() - tailSize + eocdInTail;
    if (disk != 0 || centralDisk != 0 || diskEntries != totalEntries) {
        *error = QStringLiteral("multi-disk ZIP archives are not supported");
        return false;
    }
    if (totalEntries == 0xffff || centralSize == 0xffffffff
        || centralOffset == 0xffffffff) {
        *error = QStringLiteral("ZIP64 archives are not supported");
        return false;
    }
    if (totalEntries == 0 || totalEntries > PluginPackageValidator::MaximumEntries) {
        *error = QStringLiteral("archive entry count is outside 1-%1")
                     .arg(PluginPackageValidator::MaximumEntries);
        return false;
    }
    if (qint64(centralOffset) + centralSize != eocdOffset) {
        *error = QStringLiteral("ZIP central directory bounds are invalid");
        return false;
    }

    QSet<QString> seen;
    qint64 cursor = centralOffset;
    qint64 expandedTotal = 0;
    entries->reserve(totalEntries);
    for (quint16 i = 0; i < totalEntries; ++i) {
        QByteArray header;
        if (!readExactly(file, cursor, 46, &header, error)) return false;
        if (littleEndian<quint32>(header, 0) != kCentralDirectorySignature) {
            *error = QStringLiteral("invalid ZIP central directory entry");
            return false;
        }
        const quint16 madeBy = littleEndian<quint16>(header, 4);
        const quint16 nameLength = littleEndian<quint16>(header, 28);
        const quint16 extraLength = littleEndian<quint16>(header, 30);
        const quint16 commentLength = littleEndian<quint16>(header, 32);
        const quint16 startDisk = littleEndian<quint16>(header, 34);
        const qint64 recordSize = 46LL + nameLength + extraLength + commentLength;
        if (startDisk != 0 || cursor > eocdOffset - recordSize) {
            *error = QStringLiteral("ZIP central directory entry is truncated");
            return false;
        }
        QByteArray rawPath;
        if (!readExactly(file, cursor + 46, nameLength, &rawPath, error)) return false;
        const QString decoded = QString::fromUtf8(rawPath);
        if (decoded.toUtf8() != rawPath) {
            *error = QStringLiteral("archive paths must be valid UTF-8");
            return false;
        }

        ZipEntry entry;
        entry.rawPath = rawPath;
        entry.flags = littleEndian<quint16>(header, 8);
        entry.method = littleEndian<quint16>(header, 10);
        entry.crc32 = littleEndian<quint32>(header, 16);
        entry.compressedSize = littleEndian<quint32>(header, 20);
        entry.uncompressedSize = littleEndian<quint32>(header, 24);
        entry.localHeaderOffset = littleEndian<quint32>(header, 42);
        entry.directory = decoded.endsWith(QLatin1Char('/'));
        const quint32 externalAttributes = littleEndian<quint32>(header, 38);
        const quint16 unixMode = quint16(externalAttributes >> 16);
        entry.symbolicLink = (madeBy >> 8) == 3 && (unixMode & 0170000) == 0120000;

        if (!validateArchivePath(decoded, entry.directory, &entry.path, error)
            || !validatePackageRoot(entry, error)) {
            return false;
        }
        const QString key = entry.path.toCaseFolded();
        if (seen.contains(key)) {
            *error = QStringLiteral("duplicate archive path: %1").arg(entry.path);
            return false;
        }
        seen.insert(key);
        if (entry.symbolicLink) {
            *error = QStringLiteral("symbolic links are not allowed: %1").arg(entry.path);
            return false;
        }
        if ((entry.flags & 0x0001) != 0) {
            *error = QStringLiteral("encrypted ZIP entries are not supported: %1").arg(entry.path);
            return false;
        }
        if (entry.method != 0 && entry.method != 8) {
            *error = QStringLiteral("unsupported ZIP compression method for %1").arg(entry.path);
            return false;
        }
        if (entry.directory
            && (entry.compressedSize != 0 || entry.uncompressedSize != 0)) {
            *error = QStringLiteral("directory ZIP entry contains data: %1").arg(entry.path);
            return false;
        }
        if (!entry.directory) {
            if (entry.method == 0 && entry.compressedSize != entry.uncompressedSize) {
                *error = QStringLiteral("stored ZIP entry has inconsistent sizes: %1")
                             .arg(entry.path);
                return false;
            }
            if (entry.uncompressedSize > PluginPackageValidator::MaximumFileBytes) {
                *error = QStringLiteral("archive file is too large: %1").arg(entry.path);
                return false;
            }
            expandedTotal += entry.uncompressedSize;
            if (expandedTotal > PluginPackageValidator::MaximumExpandedBytes) {
                *error = QStringLiteral("archive expanded size exceeds the limit");
                return false;
            }
            if (entry.uncompressedSize > 0
                && (entry.compressedSize == 0
                    || quint64(entry.uncompressedSize)
                        > quint64(entry.compressedSize)
                            * PluginPackageValidator::MaximumCompressionRatio)) {
                *error = QStringLiteral("archive compression ratio exceeds the limit: %1")
                             .arg(entry.path);
                return false;
            }
        }
        if (!validateLocalHeader(file, entry, centralOffset, error)) return false;
        entries->append(entry);
        cursor += recordSize;
    }
    if (cursor != eocdOffset) {
        *error = QStringLiteral("ZIP central directory contains trailing data");
        return false;
    }
    return true;
}

QByteArray sha256(const QByteArray& data) {
    return QCryptographicHash::hash(data, QCryptographicHash::Sha256).toHex();
}

QString archiveSha256(const QString& path, QString* error) {
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        *error = QStringLiteral("cannot reopen package for hashing");
        return {};
    }
    QCryptographicHash hash(QCryptographicHash::Sha256);
    while (!file.atEnd()) {
        const QByteArray chunk = file.read(1024 * 1024);
        if (chunk.isEmpty() && file.error() != QFile::NoError) {
            *error = QStringLiteral("cannot read package for hashing");
            return {};
        }
        hash.addData(chunk);
    }
    return QStringLiteral("sha256:") + QString::fromLatin1(hash.result().toHex());
}

bool parseHashManifest(const QByteArray& bytes,
                       const QVector<ZipEntry>& entries,
                       QHash<QString, QByteArray>* hashes,
                       QString* error) {
    if (bytes.size() > kMaximumControlFileBytes || bytes.startsWith("\xEF\xBB\xBF")
        || bytes.contains('\r') || (!bytes.isEmpty() && !bytes.endsWith('\n'))) {
        *error = QStringLiteral("manifest.sha256 must be UTF-8 with LF line endings and a final newline");
        return false;
    }
    static const QRegularExpression linePattern(
        QStringLiteral(R"(^([0-9a-f]{64})  (.+)$)"));
    QString previousPath;
    const QList<QByteArray> lines = bytes.split('\n');
    for (int i = 0; i + 1 < lines.size(); ++i) {
        const QByteArray& rawLine = lines.at(i);
        const QString line = QString::fromUtf8(rawLine);
        if (line.toUtf8() != rawLine) {
            *error = QStringLiteral("manifest.sha256 is not valid UTF-8");
            return false;
        }
        const auto match = linePattern.match(line);
        if (!match.hasMatch()) {
            *error = QStringLiteral("invalid manifest.sha256 line %1").arg(i + 1);
            return false;
        }
        QString path;
        if (!validateArchivePath(match.captured(2), false, &path, error)) return false;
        if (path == QLatin1String("manifest.sha256")
            || path == QLatin1String("signature.json")) {
            *error = QStringLiteral("manifest.sha256 contains a reserved path: %1").arg(path);
            return false;
        }
        if (!previousPath.isEmpty() && path <= previousPath) {
            *error = QStringLiteral("manifest.sha256 paths must be unique and sorted");
            return false;
        }
        previousPath = path;
        hashes->insert(path, match.captured(1).toLatin1());
    }

    QSet<QString> expected;
    for (const ZipEntry& entry : entries) {
        if (!entry.directory && entry.path != QLatin1String("manifest.sha256")
            && entry.path != QLatin1String("signature.json")) {
            expected.insert(entry.path);
        }
    }
    const QSet<QString> declared(hashes->keyBegin(), hashes->keyEnd());
    if (declared != expected) {
        QStringList missing = (expected - declared).values();
        QStringList extra = (declared - expected).values();
        missing.sort();
        extra.sort();
        *error = QStringLiteral("manifest.sha256 file set mismatch; missing=[%1], extra=[%2]")
                     .arg(missing.join(QStringLiteral(", ")),
                          extra.join(QStringLiteral(", ")));
        return false;
    }
    return true;
}

bool requiredString(const QJsonObject& object, const QString& key,
                    QString* output, QString* error) {
    const QJsonValue value = object.value(key);
    if (!value.isString() || value.toString().trimmed().isEmpty()) {
        *error = QStringLiteral("package field '%1' must be a non-empty string").arg(key);
        return false;
    }
    *output = value.toString().trimmed();
    return true;
}

bool decodeCanonicalBase64(const QJsonObject& object, const QString& key,
                           QByteArray* output, QString* error) {
    const QJsonValue value = object.value(key);
    if (!value.isString()) {
        *error = QStringLiteral("signature field '%1' must be a string").arg(key);
        return false;
    }
    const QByteArray encoded = value.toString().toLatin1();
    if (encoded.isEmpty() || encoded.size() % 4 != 0) {
        *error = QStringLiteral("signature field '%1' is not canonical Base64").arg(key);
        return false;
    }
    const QByteArray decoded = QByteArray::fromBase64(encoded);
    if (decoded.toBase64() != encoded) {
        *error = QStringLiteral("signature field '%1' is not canonical Base64").arg(key);
        return false;
    }
    *output = decoded;
    return true;
}

bool verifyP256Signature(const QByteArray& publicKey,
                         const QByteArray& signature,
                         const QByteArray& signedBytes,
                         QString* error) {
    if (publicKey.size() != 65 || uchar(publicKey.front()) != 0x04) {
        *error = QStringLiteral("signature publicKey must be an uncompressed P-256 point");
        return false;
    }
    if (signature.size() != 64) {
        *error = QStringLiteral("ECDSA P-256 signature must contain 64 bytes");
        return false;
    }

    BCRYPT_ALG_HANDLE algorithm = nullptr;
    BCRYPT_KEY_HANDLE key = nullptr;
    NTSTATUS status = BCryptOpenAlgorithmProvider(
        &algorithm, BCRYPT_ECDSA_P256_ALGORITHM, nullptr, 0);
    if (status < 0) {
        *error = QStringLiteral("cannot open Windows ECDSA P-256 provider");
        return false;
    }

    QByteArray blob(int(sizeof(BCRYPT_ECCKEY_BLOB)) + 64, Qt::Uninitialized);
    auto* header = reinterpret_cast<BCRYPT_ECCKEY_BLOB*>(blob.data());
    header->dwMagic = BCRYPT_ECDSA_PUBLIC_P256_MAGIC;
    header->cbKey = 32;
    memcpy(blob.data() + sizeof(BCRYPT_ECCKEY_BLOB), publicKey.constData() + 1, 64);
    status = BCryptImportKeyPair(
        algorithm, nullptr, BCRYPT_ECCPUBLIC_BLOB, &key,
        reinterpret_cast<PUCHAR>(blob.data()), ULONG(blob.size()), 0);
    if (status >= 0) {
        const QByteArray digest = QCryptographicHash::hash(
            signedBytes, QCryptographicHash::Sha256);
        status = BCryptVerifySignature(
            key, nullptr,
            reinterpret_cast<PUCHAR>(const_cast<char*>(digest.constData())),
            ULONG(digest.size()),
            reinterpret_cast<PUCHAR>(const_cast<char*>(signature.constData())),
            ULONG(signature.size()), 0);
    }
    if (key) BCryptDestroyKey(key);
    BCryptCloseAlgorithmProvider(algorithm, 0);
    if (status < 0) {
        *error = QStringLiteral("plugin package signature is invalid");
        return false;
    }
    return true;
}

bool verifyPackageSignature(const QJsonObject& packageObject,
                            const QByteArray& signatureBytes,
                            bool signatureFilePresent,
                            const QByteArray& hashManifestBytes,
                            QString* status,
                            QString* algorithm,
                            QString* keyId,
                            QString* error) {
    const QJsonValue declarationValue = packageObject.value(QStringLiteral("signature"));
    const bool declarationPresent = !declarationValue.isUndefined();
    if (!declarationPresent && !signatureFilePresent) {
        *status = QStringLiteral("unsigned");
        return true;
    }
    if (!declarationPresent || !signatureFilePresent) {
        *error = QStringLiteral("signed package requires both plugin.json signature metadata and signature.json");
        return false;
    }
    if (!declarationValue.isObject()) {
        *error = QStringLiteral("package signature declaration must be an object");
        return false;
    }
    const QJsonObject declaration = declarationValue.toObject();
    QString declaredAlgorithm;
    QString declaredKeyId;
    if (!requiredString(declaration, QStringLiteral("algorithm"),
                        &declaredAlgorithm, error)
        || !requiredString(declaration, QStringLiteral("keyId"),
                           &declaredKeyId, error)) {
        return false;
    }
    if (declaredAlgorithm != QLatin1String(kSignatureAlgorithm)) {
        *error = QStringLiteral("unsupported plugin package signature algorithm");
        return false;
    }

    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(signatureBytes, &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isObject()) {
        *error = QStringLiteral("invalid signature.json: %1").arg(parseError.errorString());
        return false;
    }
    const QJsonObject object = document.object();
    const QJsonValue schemaVersion = object.value(QStringLiteral("schemaVersion"));
    if (!schemaVersion.isDouble() || schemaVersion.toDouble() != 1.0) {
        *error = QStringLiteral("unsupported signature schemaVersion");
        return false;
    }
    QString actualAlgorithm;
    QString actualKeyId;
    QString signedTarget;
    if (!requiredString(object, QStringLiteral("algorithm"), &actualAlgorithm, error)
        || !requiredString(object, QStringLiteral("keyId"), &actualKeyId, error)
        || !requiredString(object, QStringLiteral("signed"), &signedTarget, error)) {
        return false;
    }
    if (actualAlgorithm != declaredAlgorithm || actualKeyId != declaredKeyId
        || signedTarget != QLatin1String(kSignatureTarget)) {
        *error = QStringLiteral("signature.json does not match the signed package declaration");
        return false;
    }
    QByteArray publicKey;
    QByteArray signature;
    if (!decodeCanonicalBase64(object, QStringLiteral("publicKey"), &publicKey, error)
        || !decodeCanonicalBase64(object, QStringLiteral("signature"), &signature, error)) {
        return false;
    }
    const QString calculatedKeyId = QStringLiteral("sha256:")
        + QString::fromLatin1(QCryptographicHash::hash(
            publicKey, QCryptographicHash::Sha256).toHex());
    if (calculatedKeyId != actualKeyId) {
        *error = QStringLiteral("signature keyId does not match publicKey");
        return false;
    }
    if (!verifyP256Signature(publicKey, signature, hashManifestBytes, error))
        return false;
    *status = QStringLiteral("verified");
    *algorithm = actualAlgorithm;
    *keyId = actualKeyId;
    return true;
}

bool extractEntries(QZipReader* reader, const QVector<ZipEntry>& entries,
                    const QString& destination, QString* error) {
    for (const ZipEntry& entry : entries) {
        const QString outputPath = QDir(destination).filePath(entry.path);
        if (entry.directory) {
            if (!QDir().mkpath(outputPath)) {
                *error = QStringLiteral("cannot create temporary directory: %1").arg(entry.path);
                return false;
            }
            continue;
        }
        const QFileInfo outputInfo(outputPath);
        if (!QDir().mkpath(outputInfo.absolutePath())) {
            *error = QStringLiteral("cannot create temporary parent directory: %1")
                         .arg(entry.path);
            return false;
        }
        const QByteArray data = reader->fileData(entry.path);
        if (reader->status() != QZipReader::NoError
            || data.size() != qint64(entry.uncompressedSize)) {
            *error = QStringLiteral("cannot extract verified entry: %1").arg(entry.path);
            return false;
        }
        QSaveFile output(outputPath);
        if (!output.open(QIODevice::WriteOnly)
            || output.write(data) != data.size() || !output.commit()) {
            *error = QStringLiteral("cannot write temporary entry: %1").arg(entry.path);
            return false;
        }
    }
    return true;
}
} // namespace

PluginPackageValidationResult PluginPackageValidator::validateAndExtract(
    const QString& packagePath,
    const PluginValidationEnvironment& environment) {
    const QFileInfo packageInfo(packagePath);
    if (packageInfo.suffix().compare(QStringLiteral("xuanplugin"),
                                     Qt::CaseInsensitive) != 0) {
        return failed(QStringLiteral("package extension must be .xuanplugin"));
    }
    if (!packageInfo.isFile() || packageInfo.size() <= 0
        || packageInfo.size() > MaximumArchiveBytes) {
        return failed(QStringLiteral("package is missing, empty, or exceeds %1 bytes")
                          .arg(MaximumArchiveBytes));
    }

    QFile archive(packageInfo.absoluteFilePath());
    if (!archive.open(QIODevice::ReadOnly))
        return failed(QStringLiteral("cannot open package"));
    QVector<ZipEntry> entries;
    QString error;
    if (!readZipIndex(&archive, &entries, &error)) return failed(error);
    if (!archive.seek(0)) return failed(QStringLiteral("cannot rewind package"));

    QZipReader reader(&archive);
    if (!reader.isReadable() || reader.status() != QZipReader::NoError)
        return failed(QStringLiteral("Qt ZIP reader rejected the package"));
    const QList<QZipReader::FileInfo> qtEntries = reader.fileInfoList();
    if (qtEntries.size() != entries.size())
        return failed(QStringLiteral("ZIP readers disagree about the entry count"));
    for (const QZipReader::FileInfo& info : qtEntries) {
        if (info.isSymLink)
            return failed(QStringLiteral("symbolic links are not allowed: %1")
                              .arg(info.filePath));
    }

    QHash<QString, const ZipEntry*> byPath;
    for (const ZipEntry& entry : entries) byPath.insert(entry.path, &entry);
    const ZipEntry* packageManifestEntry = byPath.value(QStringLiteral("plugin.json"));
    const ZipEntry* hashManifestEntry = byPath.value(QStringLiteral("manifest.sha256"));
    const ZipEntry* signatureEntry = byPath.value(QStringLiteral("signature.json"));
    if (!packageManifestEntry || packageManifestEntry->directory
        || !hashManifestEntry || hashManifestEntry->directory) {
        return failed(QStringLiteral("package requires plugin.json and manifest.sha256 files"));
    }
    if (packageManifestEntry->uncompressedSize > kMaximumControlFileBytes
        || hashManifestEntry->uncompressedSize > kMaximumControlFileBytes
        || (signatureEntry
            && (signatureEntry->directory
                || signatureEntry->uncompressedSize > kMaximumControlFileBytes))) {
        return failed(QStringLiteral("package control file exceeds the size limit"));
    }

    const QByteArray hashManifestBytes = reader.fileData(QStringLiteral("manifest.sha256"));
    if (reader.status() != QZipReader::NoError)
        return failed(QStringLiteral("cannot read manifest.sha256"));
    QHash<QString, QByteArray> declaredHashes;
    if (!parseHashManifest(hashManifestBytes, entries, &declaredHashes, &error))
        return failed(error);

    QVector<PluginPackageFile> verifiedFiles;
    for (const ZipEntry& entry : entries) {
        if (entry.directory || entry.path == QLatin1String("manifest.sha256")
            || entry.path == QLatin1String("signature.json")) {
            continue;
        }
        const QByteArray data = reader.fileData(entry.path);
        if (reader.status() != QZipReader::NoError
            || data.size() != qint64(entry.uncompressedSize)) {
            return failed(QStringLiteral("cannot read archive entry: %1").arg(entry.path));
        }
        const QByteArray actual = sha256(data);
        if (actual != declaredHashes.value(entry.path)) {
            return failed(QStringLiteral("SHA-256 mismatch: %1").arg(entry.path));
        }
        verifiedFiles.append({entry.path, entry.compressedSize,
                              entry.uncompressedSize,
                              QString::fromLatin1(actual)});
    }

    const QByteArray packageManifestBytes = reader.fileData(QStringLiteral("plugin.json"));
    QJsonParseError parseError;
    const QJsonDocument packageDocument =
        QJsonDocument::fromJson(packageManifestBytes, &parseError);
    if (parseError.error != QJsonParseError::NoError || !packageDocument.isObject()) {
        return failed(QStringLiteral("invalid package plugin.json: %1")
                          .arg(parseError.errorString()));
    }
    const QJsonObject packageObject = packageDocument.object();
    const QJsonValue schemaVersion = packageObject.value(QStringLiteral("schemaVersion"));
    if (!schemaVersion.isDouble() || schemaVersion.toDouble() != 1.0)
        return failed(QStringLiteral("unsupported package schemaVersion"));
    QString packageId;
    QString packageVersion;
    QString runtime;
    QString runtimeManifest;
    if (!requiredString(packageObject, QStringLiteral("id"), &packageId, &error)
        || !requiredString(packageObject, QStringLiteral("version"), &packageVersion, &error)
        || !requiredString(packageObject, QStringLiteral("runtime"), &runtime, &error)
        || !requiredString(packageObject, QStringLiteral("manifest"),
                           &runtimeManifest, &error)) {
        return failed(error);
    }
    static const QRegularExpression idPattern(
        QStringLiteral(R"(^[A-Za-z0-9._-]{1,128}$)"));
    if (!idPattern.match(packageId).hasMatch()
        || !SemanticVersion::parse(packageVersion, nullptr, &error)) {
        return failed(QStringLiteral("package id or semantic version is invalid"));
    }
    const QString expectedManifest = runtime == QLatin1String("native")
        ? QStringLiteral("payload/metadata.json")
        : runtime == QLatin1String("python")
            ? QStringLiteral("payload/plugin.json") : QString();
    if (expectedManifest.isEmpty() || runtimeManifest != expectedManifest) {
        return failed(QStringLiteral("package runtime or manifest path is invalid"));
    }
    const QJsonValue publisherValue = packageObject.value(QStringLiteral("publisher"));
    if (!publisherValue.isUndefined() && !publisherValue.isString())
        return failed(QStringLiteral("package publisher must be a string"));

    QString signatureStatus;
    QString signatureAlgorithm;
    QString signerKeyId;
    const QByteArray signatureBytes = signatureEntry
        ? reader.fileData(QStringLiteral("signature.json")) : QByteArray();
    if (signatureEntry && reader.status() != QZipReader::NoError)
        return failed(QStringLiteral("cannot read signature.json"));
    if (!verifyPackageSignature(
            packageObject, signatureBytes, signatureEntry != nullptr,
            hashManifestBytes, &signatureStatus, &signatureAlgorithm,
            &signerKeyId, &error)) {
        return failed(error);
    }

    auto temporaryDirectory = QSharedPointer<QTemporaryDir>::create(
        QDir::tempPath() + QStringLiteral("/XuanHub-package-XXXXXX"));
    if (!temporaryDirectory->isValid())
        return failed(QStringLiteral("cannot create package validation directory"));
    if (!extractEntries(&reader, entries, temporaryDirectory->path(), &error))
        return failed(error);

    const QString payloadDirectory =
        QDir(temporaryDirectory->path()).filePath(QStringLiteral("payload"));
    QString discoveredBackend;
    if (runtime == QLatin1String("native")) {
        QFile manifestFile(QDir(payloadDirectory).filePath(QStringLiteral("metadata.json")));
        if (!manifestFile.open(QIODevice::ReadOnly))
            return failed(QStringLiteral("native payload metadata.json is missing"));
        QJsonParseError runtimeParseError;
        const QJsonDocument runtimeDocument =
            QJsonDocument::fromJson(manifestFile.readAll(), &runtimeParseError);
        discoveredBackend = runtimeDocument.isObject()
            ? runtimeDocument.object().value(QStringLiteral("entry"))
                  .toObject().value(QStringLiteral("backend")).toString()
            : QString();
        if (runtimeParseError.error != QJsonParseError::NoError
            || discoveredBackend.isEmpty())
            return failed(QStringLiteral("native payload manifest has no entry.backend"));
    }
    const PluginContractValidationResult contract = PluginContractValidator::validate(
        payloadDirectory, runtime, QFileInfo(discoveredBackend).fileName(), environment);
    const PluginManifestResult& runtimeResult = contract.manifest;
    if (!runtimeResult.ok) return failed(runtimeResult.error);
    if (!contract.errors.isEmpty())
        return failed(contract.errors.join(QStringLiteral("; ")));
    if (runtimeResult.descriptor.id != packageId
        || runtimeResult.descriptor.version != packageVersion) {
        return failed(QStringLiteral("package identity does not match payload manifest"));
    }

    const auto trustLevel = runtime == QLatin1String("python")
        ? xuanhub::HostPluginTrustLevel::Isolated
        : xuanhub::HostPluginTrustLevel::TrustedInProcess;
    const QStringList granted = xuanhub::grantedHostPermissions(
        trustLevel, runtimeResult.descriptor.permissions);
    QStringList unsupported;
    for (const QString& permission : runtimeResult.descriptor.permissions) {
        if (!granted.contains(permission)) unsupported.append(permission);
    }
    const QString packageHash = archiveSha256(packageInfo.absoluteFilePath(), &error);
    if (packageHash.isEmpty()) return failed(error);

    PluginPackageValidationResult result;
    result.ok = true;
    result.runtime = runtime;
    result.publisher = publisherValue.toString().trimmed();
    if (result.publisher.isEmpty()) result.publisher = runtimeResult.descriptor.author;
    result.packageHash = packageHash;
    result.signatureStatus = signatureStatus;
    result.signatureAlgorithm = signatureAlgorithm;
    result.signerKeyId = signerKeyId;
    result.payloadDirectory = payloadDirectory;
    result.declaredPermissions = runtimeResult.descriptor.permissions;
    result.grantedPermissions = granted;
    result.unsupportedPermissions = unsupported;
    result.descriptor = runtimeResult.descriptor;
    result.files = std::move(verifiedFiles);
    result.temporaryDirectory = std::move(temporaryDirectory);
    return result;
}

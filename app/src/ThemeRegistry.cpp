// ThemeRegistry.cpp
#include "ThemeRegistry.h"

#include <QColor>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSet>
#include <QDebug>

#include <tuple>

namespace {
// 色板 schema 字段超集（11 个语义色）。bg/fg/accent 必填，其余可缺失走 fallback。
// 字段顺序无关，此处仅作文档与 fallback 迭代用。
const QStringList kColorFields = {
    QStringLiteral("bg"), QStringLiteral("panel"), QStringLiteral("fg"),
    QStringLiteral("muted"), QStringLiteral("accent"), QStringLiteral("danger"),
    QStringLiteral("success"), QStringLiteral("warning"), QStringLiteral("hover"),
    QStringLiteral("border"), QStringLiteral("onAccent")
};
const QStringList kRequiredFields = {
    QStringLiteral("bg"), QStringLiteral("fg"), QStringLiteral("accent")
};

QVariantMap builtinAppearance() {
    return {
        {QStringLiteral("window"), QVariantMap{
            {QStringLiteral("outerPaddingX"), 0.0},
            {QStringLiteral("outerPaddingY"), 0.0},
        }},
        {QStringLiteral("bar"), QVariantMap{
            {QStringLiteral("background"), QStringLiteral("bg")},
            {QStringLiteral("opacity"), 1.0},
            {QStringLiteral("radius"), 0.0},
            {QStringLiteral("borderWidth"), 0.0},
            {QStringLiteral("borderColor"), QStringLiteral("border")},
        }},
        {QStringLiteral("regions"), QVariantMap{
            {QStringLiteral("decoration"), QStringLiteral("none")},
            {QStringLiteral("background"), QStringLiteral("panel")},
            {QStringLiteral("opacity"), 1.0},
            {QStringLiteral("radius"), 8.0},
            {QStringLiteral("borderWidth"), 0.0},
            {QStringLiteral("borderColor"), QStringLiteral("border")},
            {QStringLiteral("paddingX"), 0.0},
            {QStringLiteral("paddingY"), 0.0},
            {QStringLiteral("spacing"), 0.0},
        }},
        {QStringLiteral("items"), QVariantMap{
            {QStringLiteral("decoration"), QStringLiteral("none")},
            {QStringLiteral("background"), QStringLiteral("panel")},
            {QStringLiteral("hoverBackground"), QStringLiteral("hover")},
            {QStringLiteral("opacity"), 1.0},
            {QStringLiteral("radius"), 6.0},
            {QStringLiteral("borderWidth"), 0.0},
            {QStringLiteral("borderColor"), QStringLiteral("border")},
            {QStringLiteral("paddingX"), 0.0},
            {QStringLiteral("paddingY"), 0.0},
            {QStringLiteral("spacing"), 0.0},
        }},
        {QStringLiteral("typography"), QVariantMap{
            {QStringLiteral("fontFamily"), QString()},
            {QStringLiteral("fontSize"), 12.0},
            {QStringLiteral("fontWeight"), 400},
        }},
        {QStringLiteral("motion"), QVariantMap{
            {QStringLiteral("duration"), 120},
        }},
    };
}

double boundedNumber(const QJsonValue& value, double minimum, double maximum,
                     double fallback, const QString& path) {
    if (!value.isDouble()) {
        qWarning() << "[Theme]" << path << "must be a number -> ignored";
        return fallback;
    }
    const double number = value.toDouble();
    if (number < minimum || number > maximum) {
        qWarning() << "[Theme]" << path << "outside" << minimum << ".." << maximum
                   << "-> clamped";
    }
    return qBound(minimum, number, maximum);
}

QString enumValue(const QJsonValue& value, const QStringList& allowed,
                  const QString& fallback, const QString& path) {
    const QString candidate = value.toString();
    if (!allowed.contains(candidate)) {
        qWarning() << "[Theme]" << path << "invalid value" << candidate << "-> ignored";
        return fallback;
    }
    return candidate;
}

// 外观令牌使用显式 allowlist，避免任意 JSON 值进入布局表达式导致负尺寸、超大窗口或
// 类型不稳定。返回值只包含主题实际声明的字段，由 ThemeManager 深合并默认值。
QVariantMap parseAppearance(const QJsonObject& root, const QString& label,
                            const QString& jsonPath) {
    QVariantMap result;
    auto parseNumberGroup = [&](const QString& groupName,
                                const QList<std::tuple<QString, double, double>>& specs) {
        const QJsonObject group = root.value(groupName).toObject();
        if (group.isEmpty()) return;
        QVariantMap parsed;
        for (const auto& spec : specs) {
            const QString key = std::get<0>(spec);
            if (!group.contains(key)) continue;
            parsed.insert(key, boundedNumber(group.value(key), std::get<1>(spec),
                                              std::get<2>(spec), std::get<1>(spec),
                                              jsonPath + QStringLiteral(": ") + label
                                                  + QLatin1Char('.') + groupName
                                                  + QLatin1Char('.') + key));
        }
        if (!parsed.isEmpty()) result.insert(groupName, parsed);
    };

    parseNumberGroup(QStringLiteral("window"), {
        {QStringLiteral("outerPaddingX"), 0.0, 64.0},
        {QStringLiteral("outerPaddingY"), 0.0, 32.0},
    });

    for (const QString& groupName : {QStringLiteral("bar"), QStringLiteral("regions"),
                                     QStringLiteral("items")}) {
        const QJsonObject group = root.value(groupName).toObject();
        if (group.isEmpty()) continue;
        QVariantMap parsed = result.value(groupName).toMap();
        if (group.contains(QStringLiteral("background"))) {
            parsed.insert(QStringLiteral("background"), enumValue(
                group.value(QStringLiteral("background")),
                {QStringLiteral("transparent"), QStringLiteral("bg"),
                 QStringLiteral("panel"), QStringLiteral("accent")},
                QStringLiteral("transparent"),
                jsonPath + QStringLiteral(": ") + label + QLatin1Char('.') + groupName
                    + QStringLiteral(".background")));
        }
        if (groupName != QStringLiteral("bar")
            && group.contains(QStringLiteral("decoration"))) {
            parsed.insert(QStringLiteral("decoration"), enumValue(
                group.value(QStringLiteral("decoration")),
                {QStringLiteral("none"), QStringLiteral("panel"), QStringLiteral("pill")},
                QStringLiteral("none"),
                jsonPath + QStringLiteral(": ") + label + QLatin1Char('.') + groupName
                    + QStringLiteral(".decoration")));
        }
        if (group.contains(QStringLiteral("borderColor"))) {
            parsed.insert(QStringLiteral("borderColor"), enumValue(
                group.value(QStringLiteral("borderColor")), kColorFields,
                QStringLiteral("border"),
                jsonPath + QStringLiteral(": ") + label + QLatin1Char('.') + groupName
                    + QStringLiteral(".borderColor")));
        }
        if (groupName == QStringLiteral("items")
            && group.contains(QStringLiteral("hoverBackground"))) {
            parsed.insert(QStringLiteral("hoverBackground"), enumValue(
                group.value(QStringLiteral("hoverBackground")), kColorFields,
                QStringLiteral("hover"),
                jsonPath + QStringLiteral(": ") + label
                    + QStringLiteral(".items.hoverBackground")));
        }
        const QList<std::tuple<QString, double, double>> numeric = {
            {QStringLiteral("opacity"), 0.0, 1.0},
            {QStringLiteral("radius"), 0.0, 64.0},
            {QStringLiteral("borderWidth"), 0.0, 8.0},
            {QStringLiteral("paddingX"), 0.0, 32.0},
            {QStringLiteral("paddingY"), 0.0, 16.0},
            {QStringLiteral("spacing"), 0.0, 32.0},
        };
        for (const auto& spec : numeric) {
            const QString key = std::get<0>(spec);
            if (!group.contains(key)) continue;
            parsed.insert(key, boundedNumber(group.value(key), std::get<1>(spec),
                                              std::get<2>(spec), std::get<1>(spec),
                                              jsonPath + QStringLiteral(": ") + label
                                                  + QLatin1Char('.') + groupName
                                                  + QLatin1Char('.') + key));
        }
        if (!parsed.isEmpty()) result.insert(groupName, parsed);
    }

    const QJsonObject typography = root.value(QStringLiteral("typography")).toObject();
    if (!typography.isEmpty()) {
        QVariantMap parsed;
        if (typography.contains(QStringLiteral("fontFamily"))) {
            const QString family = typography.value(QStringLiteral("fontFamily")).toString().left(128);
            parsed.insert(QStringLiteral("fontFamily"), family);
        }
        if (typography.contains(QStringLiteral("fontSize")))
            parsed.insert(QStringLiteral("fontSize"), boundedNumber(
                typography.value(QStringLiteral("fontSize")), 8.0, 32.0, 12.0,
                jsonPath + QStringLiteral(": ") + label + QStringLiteral(".typography.fontSize")));
        if (typography.contains(QStringLiteral("fontWeight")))
            parsed.insert(QStringLiteral("fontWeight"), qRound(boundedNumber(
                typography.value(QStringLiteral("fontWeight")), 100.0, 900.0, 400.0,
                jsonPath + QStringLiteral(": ") + label + QStringLiteral(".typography.fontWeight"))));
        if (!parsed.isEmpty()) result.insert(QStringLiteral("typography"), parsed);
    }

    const QJsonObject motion = root.value(QStringLiteral("motion")).toObject();
    if (motion.contains(QStringLiteral("duration"))) {
        result.insert(QStringLiteral("motion"), QVariantMap{
            {QStringLiteral("duration"), qRound(boundedNumber(
                motion.value(QStringLiteral("duration")), 0.0, 1000.0, 120.0,
                jsonPath + QStringLiteral(": ") + label + QStringLiteral(".motion.duration")))},
        });
    }
    return result;
}

QUrl parseChromeExtension(const QJsonObject& root, const QString& themeDir,
                          const QString& jsonPath, QStringList* modes) {
    const QJsonObject extension = root.value(QStringLiteral("extension")).toObject();
    if (extension.isEmpty()) return {};
    if (extension.value(QStringLiteral("type")).toString() != QStringLiteral("barChrome")
        || extension.value(QStringLiteral("apiVersion")).toInt() != 1) {
        qWarning() << "[Theme]" << jsonPath
                   << "extension must be barChrome apiVersion 1 -> ignored";
        return {};
    }
    const QString relative = extension.value(QStringLiteral("qml")).toString();
    if (relative.isEmpty() || QDir::isAbsolutePath(relative)
        || relative.contains(QLatin1Char(':'))
        || !relative.endsWith(QStringLiteral(".qml"), Qt::CaseInsensitive)) {
        qWarning() << "[Theme]" << jsonPath << "invalid extension.qml -> ignored";
        return {};
    }
    const QString clean = QDir::cleanPath(relative);
    if (clean == QStringLiteral("..") || clean.startsWith(QStringLiteral("../"))) {
        qWarning() << "[Theme]" << jsonPath << "extension.qml escapes theme directory -> ignored";
        return {};
    }
    const QFileInfo baseInfo(themeDir);
    const QFileInfo qmlInfo(QDir(themeDir).absoluteFilePath(clean));
    const QString baseCanonical = QDir::fromNativeSeparators(baseInfo.canonicalFilePath());
    const QString qmlCanonical = QDir::fromNativeSeparators(qmlInfo.canonicalFilePath());
    const QString prefix = QDir::cleanPath(baseCanonical) + QLatin1Char('/');
    if (baseCanonical.isEmpty() || qmlCanonical.isEmpty()
        || !QDir::cleanPath(qmlCanonical).startsWith(prefix, Qt::CaseInsensitive)) {
        qWarning() << "[Theme]" << jsonPath
                   << "extension.qml missing or resolves outside theme directory -> ignored";
        return {};
    }
    const QJsonArray supports = extension.value(QStringLiteral("supports")).toArray();
    for (const QJsonValue& value : supports) {
        const QString mode = value.toString();
        if ((mode == QStringLiteral("appbar") || mode == QStringLiteral("taskbar"))
            && !modes->contains(mode)) {
            modes->append(mode);
        }
    }
    qWarning() << "[Theme] loading trusted barChrome extension from" << qmlCanonical;
    return QUrl::fromLocalFile(qmlCanonical);
}

// 解析单个模式（"dark"/"light"）的色板对象。
// 色值必须是 QColor 可解析的字符串，否则视为缺失（记告警）；
// 必填字段缺失/非法追加进 missingRequired（调用方据此拒载整包）。
void parseMode(const QJsonObject& modeObj, const QString& modeName,
               const QString& jsonPath,
               QVariantMap* out, QStringList* missingRequired) {
    QVariantMap m;
    for (const QString& f : kColorFields) {
        if (!modeObj.contains(f)) continue;
        const QJsonValue v = modeObj.value(f);
        if (!v.isString() || !QColor::isValidColorName(v.toString())) {
            qWarning() << "[Theme]" << jsonPath << "invalid color value:"
                       << modeName + "." + f << "-> treated as missing";
            continue;
        }
        m.insert(f, v.toVariant());
    }
    *out = m;
    for (const QString& req : kRequiredFields) {
        if (!m.contains(req)) missingRequired->append(modeName + "." + req);
    }
}
} // namespace

ThemeRegistry::ThemeRegistry(QObject* parent) : QObject(parent) {
    // 内置硬编码默认主题（与 themes/xuanhub-default/theme.json 内容一致）。
    // 作用：themes/ 目录被误删 / themeId 指向的包都不存在时，保证还有一套能用。
    m_builtinDefault.id = QStringLiteral("xuanhub-default");
    m_builtinDefault.name = QStringLiteral("XuanHub Default");
    m_builtinDefault.version = QStringLiteral("0.1.0");
    m_builtinDefault.author = QStringLiteral("XuanHub");
    m_builtinDefault.dark = QVariantMap{
        {QStringLiteral("bg"),      QStringLiteral("#1e1e2e")},
        {QStringLiteral("panel"),   QStringLiteral("#313244")},
        {QStringLiteral("fg"),      QStringLiteral("#cdd6f4")},
        {QStringLiteral("muted"),   QStringLiteral("#7f849c")},
        {QStringLiteral("accent"),  QStringLiteral("#89b4fa")},
        {QStringLiteral("danger"),  QStringLiteral("#f38ba8")},
        {QStringLiteral("success"), QStringLiteral("#a6e3a1")},
        {QStringLiteral("warning"), QStringLiteral("#f9e2af")},
        {QStringLiteral("hover"),   QStringLiteral("#45475a")},
        {QStringLiteral("border"),  QStringLiteral("#45475a")},
        {QStringLiteral("onAccent"),QStringLiteral("#1e1e2e")},
    };
    m_builtinDefault.light = QVariantMap{
        {QStringLiteral("bg"),      QStringLiteral("#ececec")},
        {QStringLiteral("panel"),   QStringLiteral("#e6e6e6")},
        {QStringLiteral("fg"),      QStringLiteral("#313244")},
        {QStringLiteral("muted"),   QStringLiteral("#6c7086")},
        {QStringLiteral("accent"),  QStringLiteral("#2456d6")},
        {QStringLiteral("danger"),  QStringLiteral("#c1122f")},
        {QStringLiteral("success"), QStringLiteral("#348a33")},
        {QStringLiteral("warning"), QStringLiteral("#b5710f")},
        {QStringLiteral("hover"),   QStringLiteral("#dce0e8")},
        {QStringLiteral("border"),  QStringLiteral("#ccd0da")},
        {QStringLiteral("onAccent"),QStringLiteral("#ffffff")},
    };
    m_builtinDefault.appearance = builtinAppearance();
}

void ThemeRegistry::reload(const QString& themesDir) {
    m_packs.clear();
    QDir base(themesDir);
    if (!base.exists()) {
        qDebug() << "[Theme] themes dir not found:" << themesDir << "-> only builtin default";
        m_packs.append(m_builtinDefault);
        return;
    }
    const auto subdirs = base.entryList(QDir::Dirs | QDir::NoDotAndDotDot);
    QSet<QString> seenIds;   // 跨包去重（同 id 只保留首个）
    for (const QString& sub : subdirs) {
        const QString jsonPath = QDir(base.filePath(sub)).absoluteFilePath(QStringLiteral("theme.json"));
        QFile f(jsonPath);
        if (!f.open(QIODevice::ReadOnly)) continue;   // 无 theme.json 的目录静默跳过
        QJsonParseError pe{};
        const auto doc = QJsonDocument::fromJson(f.readAll(), &pe);
        if (pe.error != QJsonParseError::NoError) {
            qWarning() << "[Theme] parse error in" << jsonPath << ":" << pe.errorString() << "-> skip";
            continue;
        }
        const auto root = doc.object();
        ThemePack pack;
        pack.id = root.value(QStringLiteral("id")).toString();
        pack.name = root.value(QStringLiteral("name")).toString(pack.id);
        pack.version = root.value(QStringLiteral("version")).toString(QStringLiteral("0.0.0"));
        pack.author = root.value(QStringLiteral("author")).toString();
        if (pack.id.isEmpty()) {
            qWarning() << "[Theme]" << jsonPath << "missing 'id' -> skip";
            continue;
        }
        if (seenIds.contains(pack.id)) {
            qDebug() << "[Theme] duplicate id" << pack.id << "in" << jsonPath << "-> skip";
            continue;
        }
        // 校验 dark/light 必填字段
        QStringList missing;
        parseMode(root.value(QStringLiteral("dark")).toObject(),  QStringLiteral("dark"),
                  jsonPath, &pack.dark,  &missing);
        parseMode(root.value(QStringLiteral("light")).toObject(), QStringLiteral("light"),
                  jsonPath, &pack.light, &missing);
        if (!missing.isEmpty()) {
            qWarning() << "[Theme]" << jsonPath << "missing required fields:" << missing
                       << "-> skip";
            continue;
        }
        pack.appearance = parseAppearance(root.value(QStringLiteral("appearance")).toObject(),
                                          QStringLiteral("appearance"), jsonPath);
        pack.darkAppearance = parseAppearance(
            root.value(QStringLiteral("darkAppearance")).toObject(),
            QStringLiteral("darkAppearance"), jsonPath);
        pack.lightAppearance = parseAppearance(
            root.value(QStringLiteral("lightAppearance")).toObject(),
            QStringLiteral("lightAppearance"), jsonPath);
        pack.chromeUrl = parseChromeExtension(root, QFileInfo(jsonPath).absolutePath(),
                                              jsonPath, &pack.chromeModes);
        seenIds.insert(pack.id);
        m_packs.append(pack);
        qDebug() << "[Theme] loaded pack" << pack.id << "(" << pack.name << ")";
    }
    // 默认主题保证在场：若磁盘版本缺失/失败，补内置一份
    if (!seenIds.contains(m_builtinDefault.id)) {
        qDebug() << "[Theme] builtin default not on disk -> inject";
        m_packs.prepend(m_builtinDefault);
    }
}

const ThemeRegistry::ThemePack* ThemeRegistry::find(const QString& id) const {
    for (const auto& p : m_packs) {
        if (p.id == id) return &p;
    }
    return nullptr;
}

const ThemeRegistry::ThemePack& ThemeRegistry::defaultPack() const {
    return m_builtinDefault;
}

QStringList ThemeRegistry::packNames() const {
    QStringList out;
    out.reserve(m_packs.size());
    for (const auto& p : m_packs) out.append(p.name);
    return out;
}

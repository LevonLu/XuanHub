// ThemeRegistry.cpp
#include "ThemeRegistry.h"

#include <QColor>
#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSet>
#include <QDebug>

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

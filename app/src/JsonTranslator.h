// JsonTranslator.h — 从免编译 JSON 语言包提供 Qt 翻译查询
#pragma once

#include <QHash>
#include <QString>
#include <QTranslator>

class JsonTranslator final : public QTranslator {
    Q_OBJECT
public:
    explicit JsonTranslator(QObject* parent = nullptr);

    // 合并核心 qrc 语言包与 plugins/plugins_py 下各插件语言包。
    // 插件上下文必须位于 plugin.<id>.* 命名空间，防止覆盖宿主或其他插件。
    bool loadLocale(const QString& locale, const QString& exeDir);
    QString locale() const { return m_locale; }

    QString translate(const char* context, const char* sourceText,
                      const char* disambiguation = nullptr,
                      int n = -1) const override;

private:
    bool mergeCatalog(const QString& path, const QString& requiredPrefix,
                      bool required);
    void mergePluginRoot(const QString& rootPath);

    QString m_locale;
    QHash<QString, QHash<QString, QString>> m_contexts;
};

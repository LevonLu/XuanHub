// SemanticVersion.h — 插件 Manifest 使用的 SemVer 与最小版本约束工具
#pragma once

#include <QString>
#include <QStringList>

class SemanticVersion final {
public:
    static bool parse(const QString& text, SemanticVersion* output,
                      QString* error = nullptr);
    static bool isValidConstraint(const QString& constraint,
                                  QString* error = nullptr);
    static bool matches(const QString& version, const QString& constraint,
                        QString* error = nullptr);

    int compare(const SemanticVersion& other) const;
    QString toString() const;

    int major = 0;
    int minor = 0;
    int patch = 0;
    QStringList prerelease;
};

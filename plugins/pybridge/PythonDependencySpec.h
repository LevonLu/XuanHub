// PythonDependencySpec.h — 解析 Python 子插件的运行时与包依赖声明
#pragma once

#include <QJsonObject>
#include <QString>
#include <QVector>

struct PythonPackageRequirement {
    QString name;
    QString importName;
    QString versionConstraint = QStringLiteral("*");
    bool native = false;
};

struct PythonDependencySpec {
    bool declared = false;
    QString versionConstraint = QStringLiteral(">=3.12");
    QVector<PythonPackageRequirement> packages;
};

// 约束只接受逗号连接的 ==/!=/>=/<=/>/< 比较项或单独的 *。
// 版本允许 2 至 4 个十进制段，便于宿主与离线 doctor 使用同一确定语义。
bool isValidPythonVersionConstraint(const QString& constraint,
                                    QString* error = nullptr);

bool readPythonDependencySpec(const QJsonObject& manifest,
                              PythonDependencySpec* output,
                              QString* error = nullptr);

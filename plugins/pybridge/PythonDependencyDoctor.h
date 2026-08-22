// PythonDependencyDoctor.h — 调用隔离脚本并返回稳定的依赖诊断结果
#pragma once

#include "PythonDependencySpec.h"

#include <QVariantList>
#include <QVariantMap>

struct PythonDependencyReport {
    bool ok = false;
    QVariantMap runtime;
    QVariantList issues;
    QString failure;

    QString summary() const;
};

class PythonDependencyDoctor final {
public:
    static PythonDependencyReport run(const QString& pythonExecutable,
                                      const QString& doctorScript,
                                      const PythonDependencySpec& spec,
                                      int timeoutMs = 5000);
};

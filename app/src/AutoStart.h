// AutoStart.h — 开机自启（注册表 Run 键）读写助手
// TrayIcon 菜单与设置窗口（M8）共用，避免两处各写一份 QSettings 逻辑。
#pragma once
#include <QCoreApplication>
#include <QDir>
#include <QSettings>
#include <QString>

namespace autostart {

inline const QString& runKey() {
    static const QString k = QStringLiteral(
        "HKEY_CURRENT_USER\\Software\\Microsoft\\Windows\\CurrentVersion\\Run");
    return k;
}

inline bool isEnabled() {
    QSettings ro(runKey(), QSettings::NativeFormat);
    return ro.contains(QStringLiteral("XuanHub"));
}

inline void setEnabled(bool on) {
    QSettings s(runKey(), QSettings::NativeFormat);
    if (on)
        // 路径加引号：装到含空格目录（如 Program Files）后自启仍能解析
        s.setValue(QStringLiteral("XuanHub"),
                   QStringLiteral("\"%1\"").arg(
                       QDir::toNativeSeparators(QCoreApplication::applicationFilePath())));
    else
        s.remove(QStringLiteral("XuanHub"));
}

} // namespace autostart

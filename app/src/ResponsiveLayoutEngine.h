// ResponsiveLayoutEngine.h — 按布局偏好分配状态栏插件宽度与溢出状态
#pragma once

#include <QString>
#include <QVector>

#include "barplugin/PluginDescriptor.h"

class ResponsiveLayoutEngine final {
public:
    struct Item {
        QString id;
        int naturalWidth = 0;
        int fixedWidth = 0;
        xuanhub::PluginLayoutPreference preference;
    };

    struct Result {
        QString id;
        int width = 0;
        bool visible = true;
        bool overflowed = false;
    };

    static QVector<Result> allocate(int availableWidth,
                                    const QVector<Item>& items);
};

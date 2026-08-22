// ResponsiveLayoutEngine.cpp — 保留旧定宽语义，显式声明者才参与收缩与溢出
#include "ResponsiveLayoutEngine.h"

#include <algorithm>

QVector<ResponsiveLayoutEngine::Result> ResponsiveLayoutEngine::allocate(
    int availableWidth, const QVector<Item>& items) {
    QVector<Result> results;
    results.reserve(items.size());
    int totalWidth = 0;
    for (const Item& item : items) {
        int width = item.fixedWidth > 0 ? item.fixedWidth : qMax(0, item.naturalWidth);
        if (item.fixedWidth <= 0 && item.preference.declared) {
            if (item.preference.preferredWidth > 0)
                width = item.preference.preferredWidth;
            width = qMax(width, item.preference.minWidth);
            if (item.preference.maxWidth > 0)
                width = qMin(width, item.preference.maxWidth);
        }
        results.append({item.id, width, true, false});
        totalWidth += width;
    }
    if (availableWidth <= 0) return results;

    QVector<int> responsive;
    for (int i = 0; i < items.size(); ++i) {
        // 用户 config 的 width 是显式固定宽，优先级高于 Manifest 响应式偏好。
        if (items.at(i).fixedWidth <= 0 && items.at(i).preference.declared)
            responsive.append(i);
    }
    std::stable_sort(responsive.begin(), responsive.end(), [&items](int left, int right) {
        const int lp = items.at(left).preference.priority;
        const int rp = items.at(right).preference.priority;
        return lp != rp ? lp < rp : left > right;
    });

    // 空间不足时低优先级先收缩到 minWidth；未声明布局和 config 定宽绝不改变。
    int deficit = qMax(0, totalWidth - availableWidth);
    for (int index : responsive) {
        if (deficit <= 0) break;
        Result& result = results[index];
        const int capacity = qMax(0, result.width - items.at(index).preference.minWidth);
        const int shrink = qMin(capacity, deficit);
        result.width -= shrink;
        totalWidth -= shrink;
        deficit -= shrink;
    }

    // 收缩后仍放不下时，只有明确选择 hide/menu 的插件才会消失；clip 和旧插件
    // 继续保持宽度，避免升级后静默改变既有布局。
    for (int index : responsive) {
        if (totalWidth <= availableWidth) break;
        const QString mode = items.at(index).preference.overflow;
        if (mode != QStringLiteral("hide") && mode != QStringLiteral("menu")) continue;
        Result& result = results[index];
        totalWidth -= result.width;
        result.width = 0;
        result.visible = false;
        result.overflowed = mode == QStringLiteral("menu");
    }

    // 有余量时只扩展 flex>0 且声明 maxWidth 的插件；没有明确上限时不制造
    // 大片空槽。按权重迭代分配，达到上限的项退出下一轮。
    int extra = qMax(0, availableWidth - totalWidth);
    while (extra > 0) {
        int totalFlex = 0;
        QVector<int> growers;
        for (int index : responsive) {
            const auto& preference = items.at(index).preference;
            if (!results.at(index).visible || preference.flex <= 0
                || preference.maxWidth <= results.at(index).width) {
                continue;
            }
            growers.append(index);
            totalFlex += preference.flex;
        }
        if (growers.isEmpty() || totalFlex <= 0) break;
        int distributed = 0;
        for (int index : growers) {
            const auto& preference = items.at(index).preference;
            const int share = qMax(1, extra * preference.flex / totalFlex);
            const int growth = std::min({share, extra - distributed,
                                         preference.maxWidth - results.at(index).width});
            if (growth <= 0) continue;
            results[index].width += growth;
            distributed += growth;
            if (distributed >= extra) break;
        }
        if (distributed <= 0) break;
        extra -= distributed;
    }
    return results;
}

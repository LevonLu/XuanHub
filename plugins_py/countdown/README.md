# 倒计时插件

状态栏继续显示每日或指定日期倒计时，悬浮提示独立配置。

在「设置 → 插件 → 倒计时 → 悬浮提示」输入文字，留空关闭。支持多行，文字按纯文本
显示，不执行 HTML 或表达式。保存后生效；首次部署新的 Python/QML 文件后须重启宿主。

## 发薪日示例

点击「填入发薪日示例」，默认每月 10 号发薪：

```text
下次发工资：{payday}（还有 {paydayDays} 天）
```

`{payday}` 替换为下次实际发薪日期（`YYYY-MM-DD`），`{paydayDays}` 替换为距离该日期的
自然日数。也可以只用其中一个占位符；其他文字和未知占位符保留原样。

计算采用本机日期及中国大陆放假调休安排。遇休息日逐日向前查找最近工作日，补班周末
算工作日。已提前发薪的月份不会继续选中；发薪当天显示剩余 0 天，次日切到下一次。
每月日期可设为 1–31，短月取该月最后一天。

- 2026 年 1 月 10 日为周六，发薪日为 1 月 9 日。
- 2026 年 5 月 10 日为周日，5 月 9 日为补班日，因此提前到 5 月 9 日。
- 2026 年 10 月 10 日为补班日，保留当天。

「额外休息日」「额外工作日」可填写公司安排的日期，用逗号或换行分隔，覆盖国家日历。
两份列表不能包含同一天，非法日期和超范围的发薪日会被拒绝保存。

## 日历覆盖与维护

`calendar_cn.json` 随插件离线分发，当前包含 2026 年完整放假和补班安排，依据
[国务院办公厅通知](https://www.gov.cn/zhengce/zhengceku/202511/content_7047091.htm)
（[北京市政府转载](https://www.beijing.gov.cn/cs/gncs/zcwj/202603/t20260327_4568275.html)）。
新增年份时按同一结构维护 `years.<年份>.holidays` 的闭区间和 `workdays` 日期列表，记录
来源并重启插件。插件不会联网下载日历，也不需要额外 Python 包。

计算所需年份未收录、或日历文件缺失/损坏时，占位符显示「待确认」并注明原因，不使用
仅排除周末的日期冒充确认结果。公司覆盖日期可以明确指定某一天的状态，但不代表整个
未来年份已经获得完整日历。普通提示文字不依赖日历。

## 验证

```powershell
python -B -m unittest discover -s plugins_py/tests -p test_countdown.py -v
python -B scripts/xuanhub_plugin.py validate plugins_py/countdown
python -B scripts/xuanhub_plugin.py run plugins_py/countdown --json
python -B scripts/xuanhub_plugin.py doctor plugins_py/countdown --json
```

测试覆盖日期规则、配置拒绝与热更新、真实隔离 Worker 的启停和 QML 悬停交互。
未安装 Qt 时仅跳过 QML 交互测试；可用 `XUANHUB_QMLTESTRUNNER` 指定 Qt 测试程序。
真实宿主的多屏与不同 DPI 仍需手动验收。

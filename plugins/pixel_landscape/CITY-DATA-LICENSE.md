# 城市预设数据来源

`CityPresets.js` 中的坐标和城市记录 ID 提取自 [GeoNames cities15000](https://download.geonames.org/export/dump/cities15000.zip)，访问日期为 2026-11-09。坐标采用 WGS84 十进制度，表示城市的近似参考点，用于像素风景的太阳光照，不代表精确住址或用户当前位置。

GeoNames 数据采用 [Creative Commons Attribution 4.0](https://creativecommons.org/licenses/by/4.0/) 许可，版权归 GeoNames 及其贡献者。原始格式与许可说明见 [GeoNames 数据说明](https://download.geonames.org/export/dump/readme.txt)。源码与插件分发保留本归属说明。

本插件从原数据精选 75 个城市，按设置界面的国家／地区分组，补充简体中文和英文显示名；不附带人口、行政边界或时区数据库。城市的 `id` 保留原 GeoNames ID，以便追溯和更新。坐标可在设置页手动修改，运行时无需联网查询城市。

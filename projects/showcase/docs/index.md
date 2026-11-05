# Showcase 项目文档

本目录集中维护公开范例的使用方法、资源来源与对照结果。项目业务与资源位于 `../assets/`，制作工具位于 `../tools/`，项目专属测试位于 `../tests/`；文中命令默认从仓库根执行，`assets/`、`settings/`、`cache/` 路径默认相对本项目。平台实测与失败记录统一见[引擎测试文档](../../../docs/testing.md)，当前菜单用例见[功能覆盖](../../../docs/editor/showcase-coverage.md)。

## 功能范例

- [地形、建筑与火箭综合联动](destruction-integration.md)
- [群体导航与地形变化](navigation.md)
- [物理查询与接触事件](physics-interactions.md)
- [ECS 多对象](ecs-many-objects.md)

## PBR 对照

- [对照设置与范围](pbr-comparison.md) · [参数矩阵历史记录](pbr-parameter-comparison.md)
- [当前结果与限制](pbr-comparison/current-results.md) · [工具与复现命令](pbr-comparison/tools.md)
- 历史阶段：[standard-v2](pbr-comparison/standard-v2-results.md)、[SpecGloss v3](pbr-comparison/specgloss-v3-results.md)、[统一显示变换](pbr-comparison/display-matched-results.md)、[法线诊断](pbr-comparison/normal-findings.md)

历史截图和数值保留原采集条件，不混合为当前版本同一次验收。原始图片、EXR、回执和缓存继续留在忽略目录。

## 资源来源与制作

- [庭院模型与贴图](courtyard-source.md)
- [动画测试模型来源](models-source.md) · [模型 MIT 许可原文](models-license.md)
- [字体来源](sources/fonts.md) · [特效来源](sources/effects.md)
- [原创破坏特效导出](destruction-effects.md)
- [lcals 标识影片来源与制作](sources/lcals-movie.md)

`assets/fonts/ofl.txt`、`assets/effects/effekseer-license.txt` 和 `assets/effects/efkbgfx-license.txt` 保持在资源目录随分发保留；集中说明文档不替代这些版权与许可正文。

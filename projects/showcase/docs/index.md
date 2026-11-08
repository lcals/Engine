# Showcase 项目文档

本目录集中维护公开范例的使用方法、资源来源与对照结果。项目业务与资源位于 `../assets/`，制作工具位于 `../tools/`，项目专属测试位于 `../tests/`；文中命令默认从仓库根执行，`assets/`、`settings/`、`cache/` 路径默认相对本项目。平台实测与失败记录统一见[引擎测试文档](../../../docs/testing.md)，当前菜单用例见[验证入口](#验证)。

## 功能范例

- [地形、建筑与火箭综合联动](destruction-integration.md)
- [群体导航与地形变化](navigation.md)
- [物理查询与接触事件](physics-interactions.md)
- [ECS 多对象](ecs-many-objects.md)

## PBR 对照

- [对照设置与范围](pbr-comparison.md)
- [当前结果与限制](pbr-comparison/current-results.md) · [工具与复现命令](pbr-comparison/tools.md)
- [法线对照方法](pbr-comparison/normal-findings.md)

历史截图和数值保留原采集条件，不混合为当前版本同一次验收。原始图片、EXR、回执和缓存继续留在忽略目录。

## 资源来源与制作

- [庭院模型与贴图](courtyard-source.md)
- [动画测试模型来源](models-source.md) · [模型 MIT 许可原文](models-license.md)
- [字体来源](sources/fonts.md) · [特效来源](sources/effects.md)
- [原创破坏特效导出](destruction-effects.md)
- [lcals 标识影片来源与制作](sources/lcals-movie.md)

`assets/fonts/ofl.txt`、`assets/effects/effekseer-license.txt` 和 `assets/effects/efkbgfx-license.txt` 保持在资源目录随分发保留；集中说明文档不替代这些版权与许可正文。

## 公寓物理与音频

公寓通过普通场景组件组合家具静态碰撞、动态落球、相机 AudioListener 和循环背景音；引擎没有公寓专用分支。家具使用包围盒近似，不代表精确 Mesh 碰撞。

[add_apartment_physics_audio.py](../tools/add_apartment_physics_audio.py) 维护源场景与原创音频，保留节点身份和资源 GUID，重复执行复用现有内容，`--check` 仅检查源配置；它不负责导入缓存或设备播放。重新生成公寓后可用该工具恢复示例配置。

背景 WAV 为仓库脚本原创合成的 8 秒正弦和弦，没有外部采样素材；源为 24 kHz 单声道 PCM16，邻接 meta 配置导入为 48 kHz 双声道浮点 PCM。场景使用 GUID 引用，资源随依赖闭包进入运行包。资源加载、物理及音频运行继续通过引擎各自契约完成。

## 验证

菜单注册以 [showcase_cases.ts](../assets/scripts/models/showcase_cases.ts) 为唯一来源；具体模块操作见上面的项目文档。项目测试按需要选择，不把全部场景加入日常回归。

- [case_contract_test.mjs](../tests/case_contract_test.mjs)：用例入口、项目 Controller 和 Model/View 契约；直接脚本调用不等于真实 SDL/RmlUi 输入。
- [persistent_ui_test.mjs](../tests/persistent_ui_test.mjs)：常驻文档、状态和跨场景生命周期。
- [mcp_interaction_test.py](../tests/mcp_interaction_test.py)：显式连接已启动、完成导入的编辑器，检查会话/Run 身份、菜单点击、滚轮、文字输入、跨场景 UI 与正常关闭。只有需要实际交互验收时运行，它会关闭该测试拥有的编辑器。

```sh
node projects/showcase/tests/case_contract_test.mjs .
node projects/showcase/tests/persistent_ui_test.mjs .
python3 projects/showcase/tests/mcp_interaction_test.py --server build/ninja-engine-release/mcp_app --runtime-dir build/showcase-runtime
```

真实交互前用 MCP status 核对项目、运行目录和有效会话；`--server` 与编辑器使用同一构建。待处理请求只查询原回执，不重复发起。原始操作回执保存在项目 `cache/`，不进入 Git。

脚本替身、原生功能、真实 UI 输入和图像质量分别验证；没有执行的路径不宣称通过。菜单入口、资源登记与类型检查不代表所有效果、声音或设备已验收。平台结果与必要性能摘要统一见[引擎测试文档](../../../docs/testing.md)。

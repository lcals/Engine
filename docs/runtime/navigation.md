# 导航模块

`NavigationSystem` 使用 [Recast Navigation 1.6.0](https://github.com/recastnavigation/recastnavigation/tree/v1.6.0) 生成分块导航网格，使用 Detour 查询完整路径。生成流程参考官方 `RecastDemo/Source/Sample_TileMesh.cpp`，保留 Zlib 许可说明；第三方类型只存在于实现文件。系统由世界系统拥有，架构入口见[导航类图](class-diagrams/navigation-system.md)。

## 调用契约

- `build(geometry, bounds, settings)` 提交当前几何、固定构建范围和代理设置，完整构建网格。`NavigationGeometry` 只借用同步调用期间的 Mesh，使用列主序仿射矩阵；完成后不保留源 Mesh 指针。负缩放会调整三角形绕序。
- `replace(geometry, dirty)` 提交**全部当前几何**和覆盖新旧变更位置的范围，只重新生成与 dirty 及必要 Recast 边界交叠的 tiles。候选 tiles 全部生成后才发布，未受影响 tiles 保留。空几何合法，用于移除范围内的旧导航。
- `find_path(start, end, output)` 将端点投影到配置范围内的最近多边形，再查询路径。无法完整到达目标、查询预算不足或输出不足都返回 `false` 并清空 output，不把 Detour 的部分路径当作成功。
- `revision()` 在网格发布、失效或清空时变化；`rebuilt_tiles()` 返回最近一次构建实际处理的 tile 数，包括生成结果为空的 tile。调用方缓存路径时须同时记录 revision，变化后重新查询。
- `clear()` 幂等。`ready()` 表示已创建有效导航网格容器；全部为空的网格也可以 ready，此时寻路失败。

核心不扫描 Scene，不识别地形、建筑、NPC 或具体插件，不自动监听几何。玩法与宿主负责选择 Mesh、合并一次变更的 dirty 范围，并在场景切换时清空旧导航。同步操作应放在调用方安全点，不允许构建、替换或寻路并发。后续可分离准备计算与发布，但当前没有后台任务或异步承诺。

## 生成和失败处理

流程使用上游高度场栅格化、可行走过滤、紧凑高度场、代理半径侵蚀、距离场、watershed 区域、轮廓、多边形、细节网格和 Detour tile 数据。构建范围外的单元不可行走，避免导航绕出调用方声明的边界。边界几何仍参与间隙和坡度判断。

完整构建失败会清空全部旧导航；局部替换失败会移除所有受影响 tiles、递增 revision，并保留范围外的数据。非法 dirty 无法定位影响范围时清空全部。错误通过返回值和 `last_error()` 报告，不能继续沿变更区域的陈旧路径移动。调用方未提交完整变化范围时，系统无法推断遗漏的改动。

默认 cell size 为 0.25 米，cell height 为 0.1 米，tile 边长 32 个格；默认代理高 1.8 米、半径 0.35 米、最大台阶 0.4 米、坡度 45 度。固定区域过滤和细节参数沿用上游示例的保守值，较小的独立可行走岛可能被过滤。

当前设置和输入有明确上限：4096 个 Mesh、累计 100 万顶点/三角形、1024 个 tiles；每 tile 至多 4096 个多边形，寻路节点预算 8192，路径与输出点预算 1024。tile size 支持 8～128 格，扩展边界后的边长不超过 256 格，并检查垂直高度场的可表达范围。上限用于拒绝失控输入，不代表任意叠层复杂度下都有固定耗时或内存占用；尚未对大场景给出性能保证。

## 当前演示与范围

Showcase 由项目脚本提交 Terrain 产生的静态网格，在一次爆炸处理完地形后合并范围并局部重建。移动碎块不自动加入导航，导航也不代替角色运动、物理碰撞或人群避让。多代理尺寸、导航资源离线导入、成本区域、跳跃连接和 DetourCrowd 尚未实现。

回归测试位于 `tests/navigation_test.cpp`，覆盖跨 tile 寻路、挖洞绕路、完全断路、填补恢复、失败时局部失效、空几何删除、变换及参数边界。[脚本接口](../plugins/runtime/class-diagrams/navigation-script-api.md)由 `tests/navigation_script_test.cpp` 验证真实后端、显式输入及场景生命周期。实际构建与测试结果以本次交付记录为准。依赖方向见[运行时模块](runtime.md)。

# 导航模块

`NavigationSystem` 使用 [Recast Navigation 1.6.0](https://github.com/recastnavigation/recastnavigation/tree/v1.6.0) 生成分块导航网格，使用 Detour 查询完整路径，并使用 DetourCrowd 做多代理路径跟随、局部避让及位置积分。生成流程参考官方 `RecastDemo/Source/Sample_TileMesh.cpp`，保留 Zlib 许可说明；第三方类型只存在于实现文件。宿主负责装配及生命周期，架构入口见[导航类图](class-diagrams/navigation-system.md)。

## 调用契约

- `build(geometry, bounds, settings)` 提交当前几何、固定构建范围和代理设置，完整构建网格。`NavigationGeometry` 只借用同步调用期间的 Mesh，使用列主序仿射矩阵；完成后不保留源 Mesh 指针。负缩放会调整三角形绕序。
- `replace(geometry, dirty)` 提交**全部当前几何**和覆盖新旧变更位置的范围，只重新生成与 dirty 及必要 Recast 边界交叠的 tiles。候选 tiles 全部生成后才发布，未受影响 tiles 保留。空几何合法，用于移除范围内的旧导航。
- `find_path(start, end, output)` 将端点投影到配置范围内的最近多边形，再查询路径。无法完整到达目标、查询预算不足或输出不足都返回 `false` 并清空 output，不把 Detour 的部分路径当作成功。
- `revision()` 在网格发布、失效或清空时变化；`rebuilt_tiles()` 返回最近一次构建实际处理的 tile 数，包括生成结果为空的 tile。调用方缓存路径时须同时记录 revision，变化后重新查询。
- `clear()` 幂等。`ready()` 表示已创建有效导航网格容器；全部为空的网格也可以 ready，此时寻路失败。

核心不扫描 Scene，不识别地形、建筑、NPC 或具体插件，不自动监听几何。玩法与宿主负责选择 Mesh、合并一次变更的 dirty 范围，并在场景切换时清空旧导航。同步操作应放在调用方安全点，不允许构建、替换或寻路并发。

构造时可借用已初始化的 `TaskSystem` 并设置 `parallel_tile_threshold`（默认 4，范围 1..1024）。达到阈值时把各 tile 的 Recast 准备交给共享 worker，工作数据和结果各自独占，所有 tile 准备结束后在调用线程统一更新 Detour 网格。域并发额度限制实际并行数；小批次、未传任务域或域饱和拒绝提交时同步准备。任务域先初始化、后关闭，不能在构建期间关闭或重配。`build` / `replace` 仍在完成发布后返回，不代表跨帧异步加载；共享查询器和 DetourCrowd 的更新继续有序执行。

## 生成和失败处理

流程使用上游高度场栅格化、可行走过滤、紧凑高度场、代理半径侵蚀、距离场、watershed 区域、轮廓、多边形、细节网格和 Detour tile 数据。构建范围外的单元不可行走，避免导航绕出调用方声明的边界。边界几何仍参与间隙和坡度判断。

完整构建失败会清空全部旧导航；局部替换失败会移除所有受影响 tiles、递增 revision，并保留范围外的数据。非法 dirty 无法定位影响范围时清空全部。错误通过返回值和 `last_error()` 报告，不能继续沿变更区域的陈旧路径移动。调用方未提交完整变化范围时，系统无法推断遗漏的改动。

默认 cell size 为 0.25 米，cell height 为 0.1 米，tile 边长 32 个格；默认代理高 1.8 米、半径 0.35 米、最大台阶 0.4 米、坡度 45 度。固定区域过滤和细节参数沿用上游示例的保守值，较小的独立可行走岛可能被过滤。

当前设置和输入有明确上限：4096 个 Mesh、累计 100 万顶点/三角形、1024 个 tiles；每 tile 至多 4096 个多边形，寻路节点预算 8192，路径与输出点预算 1024。tile size 支持 8～128 格，扩展边界后的边长不超过 256 格，并检查垂直高度场的可表达范围。上限用于拒绝失控输入，不代表任意叠层复杂度下都有固定耗时或内存占用；尚未对大场景给出性能保证。

## 当前演示与范围

Showcase 由项目脚本提交 Terrain 产生的静态网格，在一次爆炸处理完地形后合并范围并局部重建。移动碎块不自动加入导航，群体代理由独立运行时导航群体插件连接普通 Node 与 NavigationSystem，显式步进后由 Crowd 唯一写入 Transform。同节点不能同时启用 CharacterMotor；可选 `Collider + kinematic RigidBody` 组合要求刚体线/角速度均为零，Physics 只同步该形体供查询及接触使用。单独启用 Collider 或 RigidBody、非运动学刚体及非零速度组合会被拒绝；该组合不提供重力或击退位移。离线导航资源、成本区域、跳跃连接及 TileCache 尚未实现。

回归测试位于 `tests/runtime/navigation/navigation_test.cpp`，覆盖跨 tile 寻路、挖洞绕路、完全断路、填补恢复、失败时局部失效、空几何删除、变换及参数边界。[脚本接口](../plugins/runtime/class-diagrams/navigation-script-api.md)由 `tests/plugins/runtime/script/navigation_script_test.cpp` 验证真实后端、显式输入及场景生命周期。验证按[测试文档](../testing.md)选择相关用例。依赖方向见[运行时模块](runtime.md)。

## 群体移动

先 `configure_crowd(capacity)`，再用 `create_agent(position, settings, handle)` 建立代理。`set_agent_target` 投影世界目标并交给 DetourCrowd 排队；返回成功仅表示已接受，之后通过 `agent_state` 区分 pending、moving 和 arrived，失败目标报告 unreachable。目标投影遵守构建时的 query_half_extents；原始请求保留在 state.target，到达距离以投影后的导航表面目标衡量。`stop_agent` 清目标与速度，`remove_agent` 释放身份；失效句柄不能操作后来复用槽位的代理。

活动代理更新目标时直接使用上游 `dtCrowd::requestMoveTarget`，保留当前速度和走廊，由 Detour 更新路线；持续追踪移动目标不会每次重新从零加速。目标投影失败仍立即清速并报告 unreachable，不能沿旧目标继续移动。新建、显式传送和网格重建使用新代理初始化走廊，继续遵守既有身份与失效规则。

调用方在主线程显式调用 `step_crowd(delta_time)`；导航系统没有自动 tick。固定步长 1/60 秒，单次最多 8 步，负数或非有限时间失败，0 不推进。停下的代理继续参与分离，已重叠代理可能被推开。默认代理半径 0.3 米、高 1.8 米、速度 3 米/秒、加速度 8 米/秒²、分离权重 2、到达距离 0.15 米；尺寸必须不超过已生成网格的保守净空。

完整 build、clear 和 shutdown 清除全部代理、配置及旧目标；组件恢复是调用方的显式策略。局部 replace 在修改 tile 前清除旧群体队列和路径，随后保留世代句柄和原始目标，从当前位置重新投影并恢复代理。在 `query_half_extents` 内找不到剩余导航面时立即报告 invalid；邻近或下层仍有表面时可能投影到该表面，此过程不是物理下落。目标投影失败立即 unreachable，投影成功但断路由后续队列步进报告 unreachable，不沿旧路径继续。容量支持 1..4096，同容量配置幂等，有活动代理时拒绝更换容量。

群体路径限制为 256 个走廊多边形，不接受部分路径；请求只进行端点投影，不重复同步完整寻路；队列及近邻遵守 DetourCrowd 1.6.0 的固定预算，每次 update 最多推进 100 次路径搜索迭代。独立 find_path 保留同步完整查询契约。中等避让配置参考官方 CrowdTool，未增加自制寻路或通用管理层。此能力支持多人技术范例，不包含项目玩法策略。核心回归测试追加于 `tests/runtime/navigation/navigation_test.cpp`，包括交叉避让、绕障、持续移动目标、停止/移除、句柄复用、目标失败、净空与局部重建生命周期。

`tests/runtime/navigation/navigation_terrain_test.cpp` 在测试宿主组合真实 `TerrainVolume` 与导航：生成 24×8 米、0.5 米采样的窄条地形并让四个代理移动，第五个代理停在预定断沟中心，使用球形笔刷挖出贯穿断沟后提交生成网格与局部 dirty 范围。测试检查原有句柄和目标保留、断路代理停止且不跨越缺口；中心代理因脚下表面超出投影范围而保持 invalid、零速度且不移动。恢复原始采样数据并替换同一区域后，五个原目标无需重发即可重新到达，中心代理沿用原句柄恢复。该测试只在测试目标链接地形插件，不改变导航公开契约，也不向导航核心加入 Terrain 依赖。

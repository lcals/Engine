# ECS 多对象功能范例

从菜单打开 **ECS many objects**。默认创建 10,000 个真实场景实体，可切换到 1,000 或 50,000。每个实体具有独立的 Transform 和 MeshRenderer 数据；几何与材质引用现有公开方块资源。对象分属两个父节点，创建每帧处理 128 个、检查每帧处理 1,024 个，UI 不在这些父节点下面。

这是一项场景容量与正确性用例，不显示跑分、不开展跨模块性能优化。50,000 是可选择的验证数量，不表示任何平台已达到该规模的帧率要求。创建中的对象会逐步显示；等待状态变为 `PASS` 后进行状态操作。

## 功能检查

- 创建时先取 Transform 句柄，再添加 MeshRenderer。添加组件改变 ECS 数据表后，原 Transform 句柄仍可读写。
- 每轮逐项核对所有实体及组件身份、世界坐标、继承可见状态与激活状态。
- `Toggle group visibility` 只隐藏 A 组；`Toggle group active` 单独改变 A 组激活状态，两者独立。
- `Move group` 移动 A 组父节点；逐实体验证世界坐标随父节点更新，另一组保持原位。
- `Reparent one object` 将首个对象移到另一组，立即使用新父级的有效状态。
- `Delete + recreate one` 销毁并重建首个对象；旧 Node、Transform、MeshRenderer 句柄必须失效，旧 Transform 写入必须被拒绝。
- 切换数量会销毁两个旧父节点及全部子实体，再建立新群组；保留少量旧句柄继续核对不能复活。
- `Back to scenes` 清理 Controller 订阅及测试实体，复用原有常驻菜单。

项目业务通过现有 TS `Node`/`Component` 外观操作 ECS，不增加核心项目专用接口。UI 使用现有 `UiComponent + RmlUi + TS MVC`。

## 实际编辑器 MCP 检查

先用当前构建启动 showcase 编辑器，完成导入并停在菜单，再运行：

```sh
python3 projects/showcase/tests/ecs_many_objects_test.py \
  --server build/local-ninja-release/mcp_app \
  --runtime-dir build/showcase-runtime \
  --count 10000
```

脚本通过真实 UI 输入进入场景、隐藏、停用、换父级、删除重建、整组回收并返回菜单，再次进入确认整局状态清零。自动保存一张真实画面截图。`execute_project_script` 只读取当前 Gameplay 的 `Inspect()` 状态，`get_scene` 独立检查真实实体数量；不替换 Flecs、渲染或脚本宿主。每次操作必须完成新一轮逐实体检查，不能以旧的 `PASS` 状态蒙混通过。

回执及 JSON 结果保存在项目 `cache/ecs-many-objects/`，不进入 Git。脚本不关闭已有编辑器，也不自动触发 CI。实际完成的运行及平台结果记录在根 `docs/testing.md`：macOS Metal Release 的 1,000／10,000 完整交互已通过；50,000 档位本轮在等待上限内仍未创建完成，不能视为已验收。

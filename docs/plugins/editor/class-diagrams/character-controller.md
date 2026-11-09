# 角色控制器编辑器插件

[完整 Codec 与命令契约](character-control-design.md) · [运行时角色](../../runtime/class-diagrams/character-control-design.md) · [通用相机编辑](camera-controller.md)

`plugins/editor/character_controller/` 提供 CharacterController、CharacterCamera 和 CharacterVisual 的独立 Codec、命令及可选 MCP 适配。Controller Codec 的公开方法、所有权与登记表借用关系只在完整契约中维护。

`register_controller_codec(const CharacterAbilityRegistry&)` 汇总三个 Codec，借用宿主冻结的能力类型表；表先构造、后释放，覆盖 Codec 与世界生命周期。基础编辑器不包含具体插件。CharacterCameraCodec 只保存角色适配参数，CharacterVisualCodec 保存 body/head/first_person 分类。

设置命令的 `settings` 使用完整生成结构：TOML/MCP 字段为 snake_case、枚举为名称、持久节点引用为场景 local ID 的十进制字符串。设置通过 ComponentEdit 校验并参与撤销；不再接收旧的 cameraNodeId、targetNodeId 或散列相机算法参数。运行目标覆盖仅通过相机的 set_camera_target 命令传当前 Node ID。

运行输入与状态工具不进入撤销或场景保存。未知字段和未登记能力直接失败，失败保留原数据。场景与预制体缓存指纹包括 scene.fbs、camera.fbs、character_controller.fbs；当前二进制版本为 3，旧缓存经正常导入重建。

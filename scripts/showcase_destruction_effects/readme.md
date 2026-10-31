# Showcase 破坏表现制作

本工具只制作 showcase 综合场景的原创参数，不属于引擎运行时或资源导入器。步骤 4 输出灰尘 `dust.efkefc`、含扩散火球的爆破 `burst.efkefc`、火箭尾焰/烟雾 `rocket-trail.efkefc`。

在仓库根使用现有 .NET 8 SDK：

```sh
dotnet run --project scripts/showcase_destruction_effects/showcase_effects.csproj --configuration Release -p:BaseIntermediateOutputPath=../../build/showcase-effects/obj/ -- "$PWD"
python3 scripts/create_showcase_destruction_integration.py
```

工具直接编译固定 `third_party/effekseer` 子项目的官方 Core 源码，启用官方 `SCRIPT_ENABLED` 和 `DOTNET_STARNDARD` 配置；这里的 net8.0 仅用于本机离线参数导出，未更改上游 net9.0 工程。NuGet 包版本与上游工程保持一致。中间产物、工具与还原记录均在忽略的 `build/showcase-effects/`，不触发引擎或 V8 编译。

`program.cs` 使用官方 `Core.SaveTo` 写出含编辑数据的 `.efkefc`，使用预定义精灵材质和原样复制的 MIT 贴图；具体来源见项目 `assets/effects/source.md`。Python 脚本生成本示例拥有的平坦地形、综合场景、256/128/64/72 块塔/墙/柱/梁的切片描述、火箭网格、材质、音效与 meta；场景登记 64 个弹体、8 个灰尘/6 个爆破槽和 48 个路线标记。`--check` 只比较生成结果，不改文件。两个既有实验场只读复用为模板，不把它们的资源改成炮击场。


尾迹节点采用官方 `WhenCreating` 变换继承，持续发射的普通 CPU 精灵跟随移动发射器产生尾焰/烟雾；运行时命中、寿命结束、Reset 与退出由 EffectComponent 停止。两张纹理保持上游字节及许可，邻接 meta 映射实际资源依赖。核心没有识别火箭名称或模型的特判。

火箭速度、射击频率、伤害、土石预算与应力参数在项目 `assets/scripts/models/destruction_range_config.ts`，不是本导出器或引擎的默认玩法。生成/`--check`/TypeScript 通过只表示资源和代码检查通过，不代表窗口效果、音质、完整 Android 场景或手机性能验收；结果以项目及阶段验证文档为准。

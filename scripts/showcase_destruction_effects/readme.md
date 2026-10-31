# Showcase 破坏表现制作

本工具只制作综合场景的原创参数，不属于引擎运行时或资源导入器。

在仓库根使用现有 .NET 8 SDK：

```sh
dotnet run --project scripts/showcase_destruction_effects/showcase_effects.csproj --configuration Release -p:BaseIntermediateOutputPath=../../build/showcase-effects/obj/ -- "$PWD"
python3 scripts/create_showcase_destruction_integration.py
```

工具直接编译固定 `third_party/effekseer` 子项目的官方 Core 源码，启用官方 `SCRIPT_ENABLED` 和 `DOTNET_STARNDARD` 配置；这里的 net8.0 仅用于本机离线参数导出，未更改上游 net9.0 工程。NuGet 包版本与上游工程保持一致。中间产物、工具与还原记录均在忽略的 `build/showcase-effects/`，不触发引擎或 V8 编译。

`program.cs` 使用官方 `Core.SaveTo` 写出含编辑数据的 `.efkefc`，使用预定义精灵材质和原样复制的 MIT 贴图；具体来源见项目 `assets/effects/source.md`。Python 脚本只生成本示例拥有的平坦地形、综合场景、音效和 meta，`--check` 校验生成结果，不修改文件。两个既有实验场只读复用为模板。

# 特效样例来源

[assets/effects/](../../assets/effects/) 只包含可再分发的公开样例，不包含购买资源；本页资源文件名均相对该目录。

- `laser/laser.efkefc` 与三张贴图来自 [Effekseer 官方仓库](https://github.com/effekseer/Effekseer/tree/b5700082b49bc5f919610880dafddc90211a9a4e/Examples/Resources)，版本 `1808`，提交 `b5700082b49bc5f919610880dafddc90211a9a4e`。原文件为 `Laser01.efkefc`、`Texture/LaserMain01.png`、`Texture/Particle01.png`、`Texture/Particle02.png`。
- `arrow/arrow.efkefc`、模型和贴图来自 [efkbgfx 样例](https://github.com/cloudwu/efkbgfx/tree/7d89c3c31a8d8d544217b431f9400b2b50003321/examples/resources)，提交 `7d89c3c31a8d8d544217b431f9400b2b50003321`。原文件为 `Simple_Model_UV.efkefc`、`Model/Arrow.efkmodel`、`Texture/Arrow.png`。

两项目采用 MIT 许可，完整许可保存在资源旁的 [effekseer-license.txt](../../assets/effects/effekseer-license.txt) 和 [efkbgfx-license.txt](../../assets/effects/efkbgfx-license.txt)，资源构建清单随运行包保留。

目录和文件名改为小写；样例二进制内容保持上游原样。内部引用通过特效邻接 `.meta` 的 `[[resources]]` 映射到项目资源，不在引擎中识别样例名称。

展示场景为 `scenes/effects.scene`；可从启动菜单的 **Effects laboratory** 进入并返回。两个独立 Effect 组件循环播放，地面、灯光、相机与 HDR/Bloom 参数均由场景资源声明。样例分别覆盖精灵叠加和模型/UV 动画；不以它们代表音频、自定义材质或全部特效类型已经通过验证。

## 综合破坏场景的原创特效

- `destruction/dust.efkefc`、`destruction/burst.efkefc` 和步骤 4 的 `destruction/rocket-trail.efkefc` 的节点、发射、颜色、速度、大小及渐隐参数为本项目原创，由 `projects/showcase/tools/showcase_destruction_effects/program.cs` 调用上述固定版本 Effekseer 官方 C# 数据模型和 `Core.SaveTo` 导出。文件保留官方编辑数据，可以继续用 Effekseer 打开调整；没有自行实现二进制编码器或粒子渲染器。
- `destruction/textures/smoke.png`、`particle-soft.png` 分别原样取自上述固定版本 efkbgfx 的 `Texture/Smoke.png`、`Texture/Particle_Soft.png`，继续保留资源目录中的 MIT 许可。Smoke 是 2×2 图集，灰尘节点通过 UV 参数选择第一格，没有修改贴图字节。
- 步骤 4 的爆破增加原创扩散火球参数；火箭尾迹由连续发射的加色尾焰和透明烟雾节点组成，粒子在创建时继承位置/旋转，使用同两张公开贴图。运行时普通 EffectComponent 在命中、寿命结束、Reset/退出时显式停止，不增加专用引擎粒子路径。
- 使用普通 CPU sprite、预定义默认材质、透明混合/加色混合，未使用自定义材质、GPU 粒子或特效内嵌音频。对应邻接 meta 映射真实纹理依赖，音频由场景的独立 AudioSource 播放。
- `audio/destruction-blast.wav` 与 `audio/destruction-debris.wav` 为 `projects/showcase/tools/create_showcase_destruction_integration.py` 确定性合成的原创 48 kHz 单声道 PCM16，无外部录音采样；运行时仍通过标准音频导入管线。

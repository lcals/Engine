# 特效样例来源

本目录只包含可再分发的公开样例，不包含购买资源。

- `laser/laser.efkefc` 与三张贴图来自 [Effekseer 官方仓库](https://github.com/effekseer/Effekseer/tree/b5700082b49bc5f919610880dafddc90211a9a4e/Examples/Resources)，版本 `1808`，提交 `b5700082b49bc5f919610880dafddc90211a9a4e`。原文件为 `Laser01.efkefc`、`Texture/LaserMain01.png`、`Texture/Particle01.png`、`Texture/Particle02.png`。
- `arrow/arrow.efkefc`、模型和贴图来自 [efkbgfx 样例](https://github.com/cloudwu/efkbgfx/tree/7d89c3c31a8d8d544217b431f9400b2b50003321/examples/resources)，提交 `7d89c3c31a8d8d544217b431f9400b2b50003321`。原文件为 `Simple_Model_UV.efkefc`、`Model/Arrow.efkmodel`、`Texture/Arrow.png`。

两项目采用 MIT 许可，完整许可保存在 `effekseer-license.txt` 和 `efkbgfx-license.txt`，资源构建清单随运行包保留。

目录和文件名改为小写；样例二进制内容保持上游原样。内部引用通过特效邻接 `.meta` 的 `[[resources]]` 映射到项目资源，不在引擎中识别样例名称。

展示场景为 `project/scenes/effects.scene`；可从启动菜单的 **Effects laboratory** 进入并返回。两个独立 Effect 组件循环播放，地面、灯光、相机与 HDR/Bloom 参数均由场景资源声明。样例分别覆盖精灵叠加和模型/UV 动画；不以它们代表音频、自定义材质或全部特效类型已经通过验证。

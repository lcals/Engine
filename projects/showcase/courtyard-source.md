# 庭院场景与材质来源

庭院用于同一个示例项目内的蒙皮动画与物理控制器测试。地面、台阶、立面、廊架、长椅和绿植由仓库脚本 `scripts/create_animation_courtyard.py` 通过引擎 MCP 建模生成；场景与 Mesh 源资源保存在 `assets/scenes/`，生成缓存不进入版本管理。

## 纹理

2026-10-30 从 [Poly Haven API](https://api.polyhaven.com/) 获取以下 1K 纹理，按返回的 URL 下载并核对 MD5。素材采用 [CC0](https://polyhaven.com/license)，可随项目分发。

| 本项目用途 | 素材 | 作者 | 一次重复的实际尺寸 |
| --- | --- | --- | --- |
| 铺装 | [Concrete Tiles 02](https://polyhaven.com/a/concrete_tiles_02) | Charlotte Baglioni | 1.825 m |
| 墙面与花池 | [Plastered Wall 02](https://polyhaven.com/a/plastered_wall_02) | Charlotte Baglioni | 2.23 m |
| 木廊架与长椅 | [Oak Wood Planks](https://polyhaven.com/a/oak_wood_planks) | Dimitrios Savva | 1.2 m |
| 种植区碎石 | [Gravel Floor 03](https://polyhaven.com/a/gravel_floor_03) | Charlotte Baglioni | 2.5 m |

`assets/textures/` 中每套包括原始 JPG 基础色、转为 RGB8 PNG 的 OpenGL 法线和 ORM。ORM 从上游 ARM 的 AO/粗糙度通道重组：R = 0.8 + 0.2 × AO，G = 原粗糙度，B = 0（非金属）；温和 AO 用于适配当前渲染器。没有使用置换贴图。UV 密度写在 Mesh 中，材质不依赖运行时 UV 缩放参数。

每张纹理的 `.meta` 显式设置 UASTC 压缩、质量、mipmap 与颜色空间；基础色为 sRGB，法线与 ORM 为线性。资源管线生成统一 GUID 缓存，模型几何、材质、贴图与场景之间通过资源目录和依赖图关联。

## 效果范围

使用 PBR 材质、方向光阴影、环境补光和局部点光。深色窗面是实体材质；发亮灯罩使用 Unlit，照明来自独立 Light 组件。没有透明玻璃、IBL、全局光照或点光阴影。绿植为程序化几何，不是扫描植物；动画人物的来源与蒙皮限制见 `models-source.md`。

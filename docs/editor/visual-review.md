# 场景效果巡览

通过既有 MCP 预览镜头完成「基线巡览 → 调整源资源 → 相同路线复拍 → 对照分析」。不启动 Gameplay，不创建场景相机节点，不增加编辑器界面。使用 Python 3.11 及以上的标准库，无额外 Python 依赖。

## 固定条件

- 使用相同引擎构建、图形后端、窗口尺寸、场景活动相机投影和路线。保留构建版本及运行日志。
- 先打开目标预览，等待首次资源加载完成，确认 `modified=false`、`reloadPending=false`。关闭统计面板。
- 路线放项目 `settings/`，结果放 `cache/visual-reviews/`；收费资源、截图和项目特有配置遵守项目忽略规则。
- 入口全景、材质近景、明暗交界、室内及高处远景各选一个代表机位。通过不截图的过渡点避开墙体；巡览镜头没有碰撞检测。
- 巡览期间不手动移动镜头、不编辑资源；每次只调整明确的一组参数，再完整复拍。报告中的亮度变化不是视觉质量评分。

路线为 JSON，例如：

```json
{
  "scene": "project/scenes/example.scene",
  "steps_per_second": 12,
  "waypoints": [
    {
      "name": "entrance",
      "position": [0, 2, 10],
      "rotation": [0, 0, 0],
      "travel_seconds": 0,
      "settle_seconds": 1
    },
    {
      "name": "transition",
      "position": [0, 2, 6],
      "rotation": [0, 0, 0],
      "travel_seconds": 3,
      "settle_seconds": 0,
      "capture": false
    },
    {
      "name": "detail",
      "position": [2, 2, 4],
      "rotation": [-10, 20, 0],
      "travel_seconds": 3,
      "settle_seconds": 1
    }
  ]
}
```

`rotation` 是 `[pitch,yaw,roll]` 度。首机位直接定位，之后平滑插值位置并沿各轴最短角度旋转；`travel_seconds` 是到达该点的目标时长。MCP 每一步等待真实回执，慢帧可能延长路线，实际时长和步数写入记录。这是效果巡览，不是帧率或性能基准。

## 采集与对比

先按根 README 启动编辑器及目标场景预览，再执行：

```sh
python3 scripts/preview_visual_review.py \
  --server build/ninja-engine-release/mcp_app \
  --project projects/showcase --runtime-dir build/editor-runtime \
  --route projects/showcase/settings/visual-review.json \
  --output visual-reviews/baseline
```

以上场景路径和机位只是格式示例；按实际项目创建路线文件。工具不启停编辑器，不替换其他活动预览，也不覆盖既有采集目录。默认结束恢复首机位，`--leave-open` 保留末机位；二者都保持预览开启。

修改并保存场景或材质源文件，等自动导入与预览刷新结束，再运行同一命令，输出目录改为 `visual-reviews/tuned`，增加：

```sh
--compare projects/showcase/cache/visual-reviews/baseline/manifest.json
```

每轮产物：

- 原始 PNG：由 bgfx 真实帧回调写入，只有截图回执为 `complete` 才计入成功。
- `manifest.json`：路线、源场景 SHA-256、磁盘活动相机投影、实际机位、渲染参数、分辨率、截图 SHA-256、会话和完成状态。
- `receipts.jsonl`：逐条 MCP 参数及执行回执；核对项目、运行目录、会话及 pending 请求身份。
- `index.html`：自包含报告；有基线时可拖动分界线比较。图片只对 PNG 数据无损重压缩，原始文件不变。

比较要求路线、磁盘相机投影和截图尺寸相同；采集期间场景文件、镜头或渲染参数变化会使本轮失败。未保存内存编辑会明确记录警告，磁盘哈希不能代表这部分状态。该工具不冻结整个资源目录，也不替代模型、纹理及 Shader 版本管理；正式基线应在完成导入、无其他编辑者时采集。失败仍保留已完成的截图和回执，不重发可能已经执行的修改。

首次打开大场景后 GPU 资源准备可能暂时暂停心跳，出现 `No live editor session` 时先确认原编辑器进程和日志；等心跳恢复再用新的输出目录采集，不重复启动实例或重发 pending 修改。

通过 MCP 调整参数后调用 `save_scene`。已加载网格和未覆盖材质保留原 GUID，只导出动态网格及覆盖材质；内容不变的源文件和 meta 不重写。只调灯光或环境时应返回 `meshCount=0`、`materialCount=0`，仅场景变化；重复保存应返回 `resourceCount=0`、`importRequired=false`。保存命令自身不进入撤销栈，参数编辑仍可 `undo` / `redo`，然后再次增量保存。

预览识别自身保存的资源及源文件摘要，自动导入后只提交新目录，保留场景身份、镜头与撤销历史。外部编辑仍走正常热重载和未保存保护；同一场景代次内已保存文件或 meta 被外部修改时，保存明确拒绝覆盖。修改共享源材质或 Shader 前备份相关文件，等待正常导入完成后再采集；不能把外部修改伪装成自保存。


## 分析顺序

1. 先检查曝光、颜色空间和天空，避免用额外灯光补偿整体错误。
2. 比较受光与背光石材，平衡三色环境补光和 IBL，防止两份漫反射叠加后失去层次。
3. 检查近景、远景和室内，再调雾密度与颜色；保持空间深度，不靠雾掩盖错误。
4. 检查法线、高光、阴影边缘、遮挡接触、半透明与发光，区分参数问题和缺少渲染能力。
5. 同路线复拍，逐点记录改善、退化和未解决项。不同引擎或不同长宽比的参考图只作定性对照，不宣称逐像素匹配。




# 资源目录、读取与高层加载

[类图索引](../class-diagrams/index.md) · [统一生命周期总图](class-diagrams/resource-lifecycle.md) · [高层契约](class-diagrams/resource-loader.md) · [资源插件](../plugins/runtime/class-diagrams/resource-graph.md)

**设计状态：统一加载与释放契约已确认。** 核心负责单项读取与解析，插件实现共享加载与卸载，业务先加载后使用。验证结果集中在[测试文档](../testing.md)。

## 职责与可替换性

- 核心 VirtualFileSystem 提供同步/异步文件范围读取；不认识 GUID、资源类型或引用计数。
- 核心 ResourceSystem 提供目录和单项资源读取/解析/校验；成功转交独占候选，不加载依赖、不缓存业务对象。
- 默认 resource_graph 插件组合包装核心能力，组织依赖、共享、引用计数、加载和卸载。
- 业务及使用模块只依赖高层 ResourceLoader 契约；宿主可以选择默认插件，也可以注入符合相同规范的其他实现。底层读取类不实现这个高层接口，不提供不同语义的回退。
- 业务决定加载、使用和释放时机；使用模块只处理已加载数据，未加载返回错误。资源 ready 与 GPU/首帧/VM/场景 ready 分别报告。

这属于插件的高级封装形态；其他插件也可采用底层接入与注入，或作为独立功能由业务调用。三者都遵循[插件可替换原则](../architecture.md#插件可替换原则)。

## 清单、身份与实体

项目打包入口放 settings/editor-resource-build.toml；无清单或 items 默认全量，显式配置按真实依赖闭包筛选，目标平台由构建参数给出。源文件身份登记不等于已有可用运行产物；模型虚拟分组不自动产生运行依赖。

运行目录保存逻辑路径、完整 GUID、Kind、格式、哈希、必要运行依赖与物理位置。目录、实体头及各类型持久模型沿用 include/schemas/runtime/ 的 FlatBuffers，不复制手写模型。GuidMap 负责普通相对路径、builtin:// 与 GUID 的双向映射；运行目录拒绝 editor://。移动保留 meta 身份，复制生成新 GUID。

物理访问链为“逻辑路径 → GUID → 目录条目 → 挂载及文件范围 → 实体”。提交目录只验证元数据并建立索引，不打开全部实体或读取整包。散文件与未压缩包内范围共享 VFS 契约；平台差异集中在文件层。

转换缓存保持 cache/gen/<完整GUID>，无扩展名、类型前缀或版本子目录；SQLite 与目录索引留在 cache 根。运行包独立包含 catalog.bin 及其引用的 packs/ 或 gen/，不在启动时从可执行文件解压资源。

App::assets_path() 在原生桌面为程序旁 assets/，可选 macOS .app 为 Contents/Resources/assets/，Android 为 apk:/assets；逻辑路径不附带项目名。编辑器提交项目 cache 挂载，不复制第二份导入缓存。

宿主优先选完整已发布持久版本，否则选包体版本，再提交目录；核心按本次目录读取，缺失条目或实体不得回退旧包。引擎基线资源由构建自动加入，Shader 使用目标平台字节码；内置不能反向依赖项目。

## 加载、使用和释放

同步：ResourceLoader::load → 已加载 ResourceHandle → 模块 bind/play/instantiate → 结束模块实例 → ResourceLoader::release。

异步：begin_load → 系统推进、业务纯查询 status → ready 后 take → 使用与释放。取消只影响自己的请求；多个并发请求合并同一在途资源。同步遇到正在异步加载的必要资源返回 busy，不能在主线程泵自己等待的完成委托。

每次获取有独立身份；复制句柄不加计数，retain 才增加一次独立使用权。插件内部维护依赖、请求、任务及使用者引用；最后一次释放后反向卸载，不由业务维护计数或猜测 shared_ptr 的所有权。

模块在受理使用前校验类型和句柄、取得自己的引用；业务可以随后释放原始获取。模块自身的后台任务、运行实例、终态结果按其契约结束并释放。加载、播放/使用和释放都是明确操作，路径 setter、VM import、组件恢复不能暗中执行文件读取。

内存生成资源及无物理位置的内置定义经 load_memory/begin_load_memory 接纳，使用相同类型校验和计数，不写临时文件。内置默认资源由宿主持有至关闭；外置同身份条目必须读当前实体，损坏不回退编入定义。全部 21 种类型及扩展方法见[处理器图](class-diagrams/resource-codecs.md)。

<a id="后台加载与分帧提交"></a>

## 任务、目录换代与关闭

文件、核心单项解析、资源插件组织以及影片/渲染等模块各借用独立 TaskSystem 域，共享 ThreadSystem 的 oneTBB worker。业务不推进 ResourceLoad::step；宿主派发完成，各系统有预算地推进自己的工作。节点实例化、GPU 准备和 VM 编译的状态/耗时不混入资源完成数。

目录成功提交后整体换代；失败保持旧状态。旧 ready 获取及其依赖闭包继续有效，未完成旧请求失败为 generation_changed，新请求不混用旧数据。加载器替换前先清理旧上下文，不承诺活动句柄迁移。

关闭先停止业务与使用模块，再关闭资源插件，随后关闭核心读取及任务域。取消和普通释放不阻塞，独立后台状态保活到收尾；显式关闭边界才等待。详见[业务与任务](class-diagrams/resource-consumers.md)和[总体验收条件](class-diagrams/resource-lifecycle.md#后续实现的验收条件)。

当前存储格式和已有功能验证仍以[测试文档](../testing.md)为准，本组草案尚无实现验收结果。

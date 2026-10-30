# 最新验证

> 2026-10-26 清理：历史验证日志与散落截图已从仓库删除；历史段落保留结论，原 `artifacts-*` / `artifacts/` 文件不再提供。当前截图保存在 `docs/images/`。

## 2026-10-26 交互、GUI 装配与项目管理

- 仅编辑器构建：**46 passed / 0 failed / 20 skipped**；跳过项全部依赖 CUDA / Chrono 或需要显式启用的慢速整车 HSC 用例。
- 完整 C++ / CUDA / Chrono 构建：**65 passed / 0 failed / 1 skipped**；跳过项为需要 `BAGUA_SLOW_VEHICLE_HSC=1` 的完整整车 HSC 集成用例。
- 启动流程：`startupLauncherHasNoSceneTab` 验证正常启动时 `scene == nullptr`、场景页签为 0，选择项目后才创建页签。
- 缝合空锚点：`stitchPickWithoutAnchorsShowsError` 覆盖“拾取 A/B 时 A 部件没有锚点”的路径；现在显示错误提示，不再访问空 `anchors` 向量。
- 视口装配显示：`assemblyViewportOverlayToggleAndHighlight` 验证装配显示开关、锚点开关、面板复选框同步，以及选中装配的双部件高亮和标签；截图见 `images/ui-assembly-highlight.png`。
- 缝合配对：`stitchPairEditorUsesReadableTargets` 验证 A 下拉框显示世界坐标、B 网格顶点显示可读目标、距离列和配对信息可见。
- 项目保存：新项目默认从文档目录开始；保存前检查目标目录、可写性和文件夹冲突，错误信息包含具体路径与 `QSaveFile` 原因；`projectSaveErrorReportsPath` 覆盖父路径是文件的失败分支。
- 编译告警：`validateBackends` 未使用参数、`LinkChange` 初始化顺序、`Assets` / `HscNative` / `MainWindowAssets` 局部变量遮蔽、测试未使用变量和 CUDA 未使用内核均已清理；`MainWindow.cpp` 增加显式 Qt 头文件并移除未使用的 `<cmath>`，qmake shadow build 改用 `$$_PRO_FILE_PWD_` 解析项目内 include。
- 缓存改造：`cacheUsesDedicatedDirectory` 验证临时目录位于 `<程序目录>/cache`，并验证旧版直接写在程序目录的 `hsc-runtime-*` 会被启动清理；完整测试运行后 `build/` 与 `dist/BaguaDT/` 根目录不再出现 `hsc-runtime-*` / `hsc-probe-*`。
- Qt Creator：`CMakePresets.json` 的 `qtcreator-editor` 已配置、构建并通过编辑器测试；`qtcreator-full` 已配置并构建 `BaguaDT`，Chrono/HSC 运行库自动复制到可执行文件旁；`BaguaDT.pro` qmake 编辑器项目也已实际构建成功。
- 完整整车 HSC 集成（手动启用）：从 `ground_1e8` 参考场景拆分 20 个组件、逐个导入、装配向导生成 19 条约束，HSC 启动、首帧推进并录制一帧，**3 passed / 0 failed**（约 33 s，和 GPU 首次编译/缓存有关）。
- 仿真线程：`Physics`（HSC / Chrono）在独立 `QThread` 中构建、步进和销毁；GUI 只接收 `SimulationFrame` 快照。`simulationRunsOnWorkerThread` 断言求解线程不是 GUI 线程；`simulationControlsAndTabs`、`hscLiveRecordingAndProperties`、`linkSceneCloseWhileRunning` 等工作线程路径通过。
- 项目格式：`.bgp` 清单解析、路径越界拒绝、场景/录制存在性检查与项目管理窗口由 `projectManagerUsesBgpFormat` 覆盖；`examples/HSC/HSC.bgp`、`examples/hsc-vehicle/HSC-Vehicle.bgp` 已转换。
- 新增覆盖：组件导出 / 导入保留网格、四面体、质量、固定状态与锚点；车辆装配向导自动匹配四轮与松散轮胎，生成 4 移动副 + 4 转动副 + 11 缝合 + 8 接触排除；`guiAssembledVehicleHscStep` 验证 HSC 首帧与录制。
- 新增覆盖：轴向 / 平面 / 中心操作柄、网格吸附、世界 / 局部切换、方向键微调，装配共同连接点与约束创建，顶点吸附，场景树过滤与属性行重置 / 撤销。
- 原有装配、场景持久化、录制回放、Chrono / HSC 测试继续通过。交互细节见 [交互与装配操作](INTERACTION.md)。
- 截图：`images/ui-main.png`、`images/ui-assembly.png`、`images/ui-assembly-highlight.png`、`images/ui-project-manager.png`、`images/ui-vehicle-wizard.png`、`images/ui-stitch-editor.png`。

## 2026-10-19 审计

- 完整 C++ / CUDA / Chrono 构建：**48 passed / 0 failed / 0 skipped**，包含故意制造求解失败的恢复测试。
- 编辑器独立构建：**31 passed / 0 failed / 17 skipped**；跳过项均依赖物理后端。旧构建缓存的中文 include 前缀乱码已被构建脚本自动识别并清理，新建构建目录也独立验证通过。
- MSVC AddressSanitizer 编辑器构建：**31 passed / 0 failed / 17 skipped**。用于检查装配交互与项目打开路径的内存访问。
- 新增覆盖：删除物体/锚点时引用完整性及撤销、连续新建同类型链接、限位表单、缩放/旋转物体的锚点拾取、完整仿射锚点坐标的记录重开、Chrono 的场景 dt、项目重开不覆盖已有文档。
- 打包版在仅含 Windows 系统目录的 PATH 下，以现有 INI 的自适应逆设置运行双轮悬架 480 步（2 秒物理时间），成功保存 481 帧记录。另以 `--play --capture` 检查定时器运行路径与装配显示。

主要证据：`artifacts-audit-final-build.log`、`artifacts-audit-editor-repaired.log`、`artifacts-audit-editor-clean.log`、`artifacts-audit-final-asan.log`、`artifacts-audit-cart.log`，以及 `artifacts/audit-cart.brec`。细节和边界见 [审计报告](AUDIT_2026-10-19.md)。

复现：

```powershell
.\scripts\BuildHscNative.ps1 -BuildOnly
.\scripts\Build.ps1 -Package
.\scripts\Build.ps1 -EditorOnly -BuildDirectory build-editor
.\scripts\Build.ps1 -EditorOnly -Sanitize -BuildDirectory build-audit-asan
.\dist\BaguaDT\BaguaDT.exe .\examples\link-cart.bagua.json --hsc on --record .\artifacts\cart.brec --simulate-steps 480
```

以下为各历史阶段的验证记录，数量与范围以各段的时间为准。

# BaguaDT 验证记录

## Demo 0.3 当前验证：原生 HSC 后端

验证日期：2026-10-19。完整 C++ / CUDA / Chrono / libuipc 构建、CTest 与打包通过：**39 passed / 0 failed / 0 skipped**。

- 物理开关替换整套物理模块：关闭时使用 Chrono NSC / SMC 链，开启时使用原生 libuipc 后端（12 自由度 ABD、四面体 FEM、IPC 接触与 CCD、Newton、CUDA HSC）。
- 原生自由落体位移与解析值一致，且运行时未修改编辑场景；HSC 统计（CUDA、调用次数、收敛）随每步更新。
- 刚柔 IPC 接触产生非零接触对与非零耦合秩；0.1 m 立方体以 100 m/s 撞击厚 0.1 m 的板，单步 4.17 ms 内停在板上方约 1 mm（`d_hat`）处，无穿透。
- 原生快照写入 `.brec` 后重新载入，末帧逐顶点与运行状态一致。
- 四类关节（球铰 / 转动副 / 移动副 / 受驱动转动）各自推进 3 步且 Newton 收敛。
- Gmsh 2.2 ASCII 四面体资产导入、判定为变形体、持久化往返，并在 Chrono 与原生两个后端启动推进。
- 录制面板新增「▶● 模拟并录制」与「离线高质量」：一次点击即可启动物理系统并开始录制，录制中该按钮禁用；离线模式下每帧恰好推进一个物理步，导出记录逐帧检查帧间距都等于 1/240 s（容差 1e-9 s），元数据 `sampling` 为 `lock-step fixed timestep`。
- 录制列表：同一场景连续录两条，列表出现两行且默认选中新记录（帧数分别为 5 与 4）；`selectRecordingAt(0)` 切换后帧数随之变化并可进入回放；先单独保存第一条、再删除它，列表与计数降为 1，剩余记录仍可单独保存，两个 `.brec` 重新载入后帧数与内容各自正确。
- 便携项目带上多条记录：手工构造 `recordings: [take1.brec, take2.brec]` 的项目后打开，列表显示「记录 1 · 91 帧 · 0.375 s」与「记录 2 · 241 帧 · 1.000 s」，`recording` 指定的第二条被选中，状态栏显示「记录 2/2 · 帧 97 / 241」，时间轴按 0.4 s 定位。
- 装配前置能力：`femAbdStitchCarriesLoad` 用固定 FEM 底座吊起自由 ABD 配重，240 步后停在 0.1915 m（静止长度 0.1905 m，拉伸 1.0 mm = 19.62 N ÷ 20000 N/m），证明 FEM↔ABD 软缝合确实传递载荷；这是车辆胎唇连接与悬架弹簧的基础。
- 连接表达（场景格式）：`linkModelPersistenceAndValidation` 对五类链接（球铰 / 转动副 / 移动副 / 固定副 / 缝合）做保存-载入往返，逐字段比对锚点、轴、强度、限位区间、缝合对与 `anchorPairs`；8 类非法输入（未知类型、缺少对象、自连接、限位上下界颠倒、零轴、`dt` 越界、运动学关节连到变形体、缝合锚点越界）全部被拒绝且不覆盖当前场景；旧的 `joints`（世界锚点、`ball/hinge` 命名）自动转换为 `links`，世界锚点按刚体姿态换算为体坐标（`(0,1.5,0)` → `(0,-0.5,0)` / `(0,0.5,0)`）。
- 原生装配构建：`nativeArticulatedSuspension` 用固定底座 → 移动副（限位 ±0.15 m）→ 弹簧缝合（k=20000 N/m，rest=0.3 m）→ 转动副挂轮组成微型悬架，场景 `dt=1/120`。240 步后 Newton 收敛，导柱行程 −0.0505 m（设计 −0.05）、弹簧误差 0.49 mm、轮心 0.2995 m（设计 0.299），诊断接口返回三条链接的行程 / 角度 / 缝合误差。
- 部件对接触排除：`nativeContactExclusionAllowsOverlap` 让两个重叠 0.1 m 的刚体带排除启动，60 步收敛、接触数 0、本体不位移；同一场景去掉排除后 libuipc 直接拒绝世界（探针 `--shape overlap` 对照：`refused -> HSC world initialization failed`）。这是车辆内部重叠代理能装配的前提。
- 装配示例（`examples/link-cart.bagua.json`）：车架 + 2 转向节 + 2 车轮，6 条链接（2 移动副带 ±0.1 m 限位、2 弹簧缝合 rest=0.15 m、2 转动副）与 4 对接触排除；在编辑器里开启 HSC 推进 240 步到 1.000 s，每步收敛，车轮落在地面上、车架由弹簧支撑（几何上部件之间无接触路径，载荷只能经关节与弹簧传递）。
- 命令行批量录制（原生后端，1 s 物理时间）：`--record run.brec --simulate-steps 240` 得到 241 帧、时长 1.000 s，逐帧检查帧间距恒为 1/240 s（最大偏差 1e-16 s），元数据 `sampling` 为 `lock-step fixed timestep`；实际耗时远长于 1 s，即用更长的时间换取等间距采样。重新打开该记录并定位到 0.550 s 时显示第 133 / 241 帧。
- 立方体 / 球 / 圆柱表面封闭且无退化三角形；球的两极与经线接缝、圆柱的侧面接缝需要精确合并，否则 ABD 无法建立体积。
- 实际打开两个示例并推进：`hsc-coupled` 到 2.000 s，HSC 秩 3、r ≈ 2.4e-3，弹性盒在连接球处可见下陷；`hsc-native-contact` 到 2.500 s，HSC 秩 9、r ≈ 2.2e-3，落体在基座上压出凹坑。截图见 `artifacts/native-coupled.png` 与 `artifacts/native-contact.png`。
- 原有 Chrono 测试继续通过：圆柱静置、缩放旋转碰撞体、自由落体与接触、关节映射到 Chrono 链、HSC 与 SparseLU 对照（480 步，耦合秩 3，相对残差约 1.63e-10）、录制回放、页签、编辑与拾取。

测试输出为 `artifacts-hsc-gpu-build.log` 与 `artifacts-hsc-native-incremental.log`。本版的限制见 [HSC 接入说明](HSC_INTEGRATION.md)：接触刚度受自适应通道限制，也没有与论文同规模模型的性能基准。当前装配界面与范围见审计报告。

## Demo 0.3 历史验证：Chrono HSC 适配

验证日期：2026-10-19。完整 C++ / CUDA / Chrono 构建、CTest 与打包通过：**25 passed / 0 failed / 0 skipped**；仅编辑器构建：**18 passed / 0 failed / 7 skipped**。后者仍验证 CPU HSC 制造解与已有记录回放，跳过依赖 Chrono 的动力学测试。

- 研究原论文、补充材料、复现项目 CPU 与 CUDA 后端；详见 [接入研究](HSC_INTEGRATION.md)。Chrono 源码未修改。
- 从复现项目导出刚度 1e2 / 1e4 / 1e6 的 SPD 矩阵及制造解，验证三个 12 自由度 ABD 块的参考求解、ACA 残差、完整系统残差和解误差；错误输入不覆盖已有结果。
- 实际 Chrono 共旋 FEM / 刚体 / stiff bushing 场景，HSC 与 SparseLU 分别连续推进 480 步（2 秒）。实际耦合秩峰值 3，最终原系统相对残差约 1.63e-10，低于 1e-8 容差；最终相对反对称范数约 8.11e-6。每步刚体位置差小于 1e-4 m，最终局部 FEM 顶点差小于 2e-4，且编辑场景未被运行时修改。
- FEM 材料编辑、连接体选择和撤销；实际设置对话框的开关状态和取消操作不改变设置。
- HSC 模拟逐步录制 31 帧并保存，再打开记录、关闭 HSC、定位末帧，变形顶点逐项相同。元数据记录实际 HSC 状态和求解器。
- 顶栏 / F5–F8 在回放期间禁用，不再操纵回放；回放 API 不推进模拟，模拟 API 不改变回放游标。
- 录制不隐式启动或恢复模拟；有物理运行时先由顶栏重置才能开始回放，已有记录保留。原有资产、车辆快照、碰撞、圆柱稳定、编辑和页签测试继续通过。
- 以仅有 Windows 系统目录的 PATH 启动打包程序，真实 HSC 示例推进 120 步并截图，退出码 0、stderr 为空。已检查 `artifacts/hsc-live-preview.png` 与设置对话框 `artifacts/hsc-settings-preview.png`。

测试输出为 `artifacts-hsc-build.log`、`artifacts-hsc-editor-build.log`；包内旧车辆示例仍是来源数据回放，不是重新运行原始车辆 HSC 求解。本版 HSC 是 CPU 小规模实验适配，不包含论文 GPU/ABD/IPC 全栈，也未验证跨机器性能。

## Demo 0.2 历史验证

验证日期：2026-10-19。完整 C++ / CUDA / Chrono 构建并打包成功，QtTest **21 passed / 0 failed / 0 skipped**。仅编辑器构建为 **16 passed / 0 failed / 5 skipped**，跳过项是需要 Chrono 的测试，资产与已有结果回放仍可使用。

- OBJ 多组、负索引、MTL 颜色、毫米与 Z 向上转换；ASCII/二进制 STL、ASCII PLY；错误导入不覆盖场景。
- 资产随场景内嵌，删除原始 OBJ / MTL 后仍可重开；复制对象共享网格，错误拓扑拒绝载入。
- 导入的动态凸包在地面上落稳，固定三角网格支撑球体；保留原来的圆柱静置 30 秒、缩放旋转、关闭碰撞穿透及自由落体检查。
- 自定义 .brec 写入、重新载入、插值、变形顶点、无插值采样、另存、截断文件拒绝与状态保留。
- HSC 两组全部 214 个保存快照解压读取；首、中、末帧的车身与胎体世界坐标对照原始 NPZ，误差小于 2e-5 m。
- 原生 Chrono 录制、导出与回放；精确时间跳转、末帧、4 倍循环、播放暂停与切换页签暂停。
- 录制后继续增删编辑对象，旧记录仍显示独立对象列表，退出回放保留编辑数据。
- 便携项目保留编辑场景与记录，恢复原回放时间，重复保存不会增加重复的场景/回放页签。

打包程序使用只包含 Windows 系统目录的 PATH，直接打开包内 HSC 项目并跳到 3.5 s，生成 `artifacts/hsc-replay-preview.png`；退出码 0，stderr 为空。截图已检查：车辆、变形轮胎、两组页签、资产记录和回放时间轴正常显示。

本次未重跑 HSC 求解器，示例数据来自用户目录中已完成的原生 HSC 实验；未验证其他机器、大型 CAD 装配或新拓扑动画。测试原始输出位于 `artifacts/test-results.txt` 和 `artifacts/editor-test-results.txt`。

## Demo 0.1 初版历史记录

验证日期：2026-10-19（Asia/Shanghai）。

环境：Windows 11，MSVC 14.50，Qt 6.8.3 MSVC x64，CUDA 13.4.92，RTX 4060，Chrono 10.0.0，Eigen 3.4.0。

运行 `scripts/Build.ps1 -Package`，CMake / Ninja 完整后端构建成功，CTest 编辑器测试通过。QtTest 结果为 7 passed / 0 failed，包含初始化、清理和 5 个功能测试方法。

验证内容：

- 创建、复制、删除、属性修改的撤销与重做；JSON 保存加载及对象属性保留。
- 拒绝重复 ID、非法尺寸等不合法场景，失败时保留原场景。
- 四种几何网格生成、射线 / 三角形拾取、场景树与视口双向选择。
- 移动、旋转、缩放的三个轴，共九种操作柄拖动；每次拖动一条撤销记录；Esc 取消。
- 属性输入、场景树重命名、可见性、工具切换、复制 / 删除 / 撤销 / 重做快捷键。
- 相机旋转、平移、滚轮缩放，以及保存加载后的视图恢复。
- 输入框尚未失焦时保存，保留正在编辑的名称。
- CUDA 执行世界包围盒内核，结果与 CPU 对照通过；Chrono 场景刚体映射通过。
- 实际 OpenGL framebuffer 和操作柄覆盖层像素检查。

另以仅包含 Windows 系统目录的 PATH 启动 `dist/BaguaDT/BaguaDT.exe --capture ...`，程序退出码为 0，stderr 为空。截图经过人工视觉检查。

输出：`artifacts/test-results.txt`、`artifacts/editor-preview.png`、`artifacts/package-stderr.txt`。

限制：未验证其他机器 / GPU、超大场景、VR、动力学时间推进和接触求解。本版本不包含这些物理功能。

## 浅色紧凑界面更新

- 改用 Windows 原生风格控件与浅色调色板，移除原有深色全局样式和大尺寸按钮。
- 属性按 XYZ 表格排列，启用数值微调箭头，颜色使用小色块和十六进制值。
- 采用 16 px 工具栏图标、9 pt 字体、紧凑面板和浅灰三维视口，逻辑尺寸随 DPI 缩放。
- 原有 QtTest 检查全部通过（7 passed / 0 failed）；打包程序在系统 PATH 下启动并生成截图，退出码 0。
- 截图经过视觉检查，见 `artifacts/editor-light-preview.png`。

## 多场景页签更新

- 新建和打开增加独立文档，活动页签驱动场景树、属性面板、文件路径和 `QUndoGroup` 活动撤销栈。
- 验证场景之间的对象、未提交名称输入、选中状态、视角和工具状态保留，撤销 / 重做只影响当前场景。
- 验证拖动排序后文档绑定正确、重复打开文件不会重载覆盖编辑，以及另存为不会覆盖另一已打开文档。
- 验证关闭后台页签时取消 / 保存、关闭当前页签时放弃、关闭最后一页后自动新建，以及退出时取消保留全部文档。
- 同时打开 23 个文档，验证下拉场景列表、Ctrl+Tab / Ctrl+Shift+Tab 和单个共享视口。
- 完整构建测试结果：9 passed / 0 failed（包含生命周期检查）。
- 打包程序独立启动检查与截图：`artifacts/editor-tabs-preview.png`。

## 右键摄像头漫游更新

- 按住右键后 WASD/QE 持续移动；右键拖动原地转向，Alt+左键保留环绕。
- 使用定时器和实际时间步长推进，多方向输入归一化；Shift 加速，右键加滚轮调节速度。
- 回归测试覆盖六个方向、组合按键、松开按键 / 右键后停止、视角旋转时相机位置不变，以及 Q/E 不触发编辑工具切换。
- 验证失去输入焦点和切换页签时清除漫游状态，且对象数据与撤销栈保持不变。
- 完整构建结果：10 passed / 0 failed（包含生命周期检查）；打包启动退出码为 0。

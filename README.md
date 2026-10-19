# BaguaDT · 场景编辑器 Demo 0.3

面向物理仿真的原生桌面场景编辑器。技术主体为 **C++17、Qt Widgets / OpenGL 3.3、CUDA C++、Project Chrono 和 libuipc**。

已完成场景编辑、Project Chrono 刚体动力学 / 碰撞、弹性盒与四面体 FEM、关节约束，以及可开关的原生物理后端（12 自由度 ABD、IPC 接触与 CCD、Newton 与 CUDA HSC 求解）；尚未接入 VR。

现支持 **OBJ / STL / ASCII PLY / Gmsh 四面体资产导入、便携项目保存、仿真录制与刚体/变形网格回放**。内附 HSC 车辆越障两组真实运行结果：文件 → 打开 HSC 车辆回放项目，或运行包内 `OpenHSC.cmd`。操作与范围见 [资产和回放说明](docs/ASSETS_AND_REPLAY.md)，自定义数据格式见 [记录格式 v1](docs/RECORDING_FORMAT.md)。

两个可实时求解的示例：工具 → 打开 HSC 原生接触示例（`OpenHSCNative.cmd`）与工具 → 打开 HSC 刚柔耦合示例（`OpenHSCCoupled.cmd`）；开关在工具 → 物理模拟设置。开启 HSC 会替换整套物理模块，改用原生 libuipc 后端；关闭则使用 Chrono 原有链路。两条链的物理模型不同，不能互相作为同一算法的对照。[研究结论、接入路线和范围](docs/HSC_INTEGRATION.md)。

多部件装配示例：工具 → 打开装配示例（双轮悬架），或 `examples/link-cart.bagua.json`。车架、两个转向节与两个车轮由移动副（带行程限位）、弹簧缝合与转动副连接，演示 `links` / `exclusions` / 虚拟锚点的用法（需要开启 HSC）。装配能力的范围与后续计划见 [装配实现方案](docs/ARTICULATION_PLAN.md)。

界面采用紧凑的 Windows 浅色桌面风格：原生控件、9 pt 字体、16 px 工具栏图标、至少 22 px 高的属性输入框和 XYZ 对齐的属性表格。上述尺寸为 Qt 逻辑尺寸，随 Windows DPI 缩放。默认窗口为 1280 × 800，侧栏和底部面板可拖动调整，属性面板支持滚动。

装配界面位于底部「装配 · 链接」页签（可通过视图菜单恢复），支持链接创建/更新/删除、锚点点选与数值编辑、轴线显示、缝合配对和接触排除。运行时显示关节诊断。接手修正与尚未完成项见 [审计报告](docs/AUDIT_2026-10-19.md)。

## 运行

已打包版本：双击 `dist/BaguaDT/BaguaDT.exe`。程序默认打开四个基本几何体组成的示例。

```powershell
.\dist\BaguaDT\BaguaDT.exe .\examples\starter.bagua.json
```

批量导出高质量录制时不必逐帧操作界面：`--record run.brec --simulate-steps N` 会一个物理步一帧地录制并写盘，见 [资产和回放说明](docs/ASSETS_AND_REPLAY.md#本机仿真录制)。

分发时复制完整的 `dist/BaguaDT` 文件夹。Qt 插件、Chrono DLL 和运行库随包提供，CUDA runtime 静态链接；GPU 后端需要兼容的 NVIDIA 驱动。没有可用 CUDA 设备时仍可编辑场景，日志会报告后端不可用。

目标机器若缺少 Microsoft Visual C++ 运行库，先运行包内的 `vc_redist.x64.exe`。

## 功能和操作

| 功能 | 操作 |
| --- | --- |
| 相机漫游 | 按住右键，W/S 前后、A/D 左右、Q/E 下上；鼠标原地转向 |
| 漫游加速 / 调速 | 按住右键时 Shift 加速 / 滚轮调节移动速度 |
| 相机环绕 / 平移 / 缩放 | Alt+左键拖动 / 中键拖动 / 未按右键时滚轮 |
| 选择 | 点击模型或场景树；空白处点击取消选择 |
| 移动 / 旋转 / 缩放 | 视口获焦且松开右键时 W / E / R；也可点击工具栏 |
| 操作柄 | 拖动彩色轴或旋转环；Shift 精细调整；Esc 取消当前拖动 |
| 聚焦 / 重置相机 | F / Home |
| 创建立方体 / 球 / 圆柱 / 平面 | Ctrl+1 / 2 / 3 / 4；创建菜单或底部资源按钮 |
| 复制 / 删除 / 重命名 | Ctrl+D / Delete / F2；名称也可在右侧编辑 |
| 位置、姿态、缩放、几何尺寸 | 属性面板中输入数值后按 Enter 或离开输入框 |
| 颜色 / 可见性 | 右侧颜色选择按钮 / 复选框；场景树也有可见性复选框 |
| 撤销 / 重做 | Ctrl+Z / Ctrl+Y 或 Ctrl+Shift+Z |
| 新建 / 打开 / 保存 / 另存为 | Ctrl+N / Ctrl+O / Ctrl+S / Ctrl+Shift+S |
| 切换场景 | 点击视口上方页签；Ctrl+Tab / Ctrl+Shift+Tab |
| 新增 / 关闭页签 | 页签栏右侧 + / 页签关闭按钮或 Ctrl+W |
| 场景较多时定位 | 页签栏右侧下拉场景列表；也可使用滚动箭头 |
| 恢复关闭的面板 | 视图菜单 |
| CUDA / Chrono 自检 | 工具 → 验证 CUDA / Chrono 后端 |
| 刚体碰撞、固定状态、质量 | 右侧「刚体物理」属性组 |
| 初始平移 / 转动速度 | 右侧「初始速度」属性组 |
| 开始 / 继续模拟、暂停模拟、单步模拟、重置模拟 | 顶栏或 F5 / F6 / F7 / F8；始终仅控制物理模拟 |
| 物理后端开关 / 弹性材料 | 工具 → 物理模拟设置 / 属性面板「弹性体 · FEM」 |
| 导入网格 | Ctrl+I；支持多选、单位/向上轴转换；也可拖入文件 |
| 录制与保存 | 底部「记录与回放」→ 录制 / 停止录制 / 保存录制记录 |
| 多条记录 | 底部列表列出当前场景的每条录制；选中即切换，「保存录制记录…」/「删除所选」逐条处理，右键同效 |
| 一键模拟并录制 | 底部「▶● 模拟并录制」：启动模拟的同时开始录制 |
| 高质量录制 | 底部勾选「离线高质量」：锁定固定步长，不追实时，回放帧间距等于场景 dt |
| 回放控制 | 仅底部记录栏：开始回放、暂停、退出、时间轴、前后帧、速度、循环、插值 |
| 完整项目 | 文件 → 保存完整项目 / 打开 `.bagua.project.json` |

移动柄使用世界坐标轴；旋转环绕世界坐标轴；缩放柄沿对象的局部坐标轴。每次拖动只生成一条撤销记录。输入框编辑期间，文本编辑快捷键由输入框处理。

漫游默认速度为 3 m/s，Shift 为 4 倍速度。W/S 沿相机朝向，A/D 横移，Q/E 沿世界 Y 轴升降；组合按键不会增加总速度。移动按实际时间计算，不依赖键盘自动重复。松开右键、失去焦点、切换场景或按 Esc 都会停止漫游并清除按键状态。摄像头移动不修改场景对象，也不进入对象撤销历史。

## 多场景页签

新建和打开场景都会增加页签，已有场景继续保留。页签可以拖动排序；每个场景独立保留对象、选择、撤销 / 重做历史、视角、网格开关、操作工具和文件路径。切换页签会同步场景树与属性面板，并提交尚未失焦的属性输入。

修改过的页签以 `*` 标记，保存仅作用于当前场景。重复打开同一文件会切换到已有页签并保留编辑内容；另存为时不允许覆盖其他已打开页签对应的文件。

关闭有修改的场景会提示保存、放弃或取消；退出应用时逐一检查所有场景。取消退出会保留全部页签。关闭最后一个页签后自动建立一个空场景。各场景仍分别保存为 `.bagua.json` 文件；页签排列和撤销历史不写入场景文件。

所有页签共用一个 OpenGL 视口，切换时绑定对应的场景模型；不会为每个后台场景重复创建 GPU 渲染上下文。

单位为米，右手坐标系，Y 轴向上。几何位于自身中心，平面位于局部 XZ 面。

- 几何尺寸是局部完整尺寸；球的 XYZ 是三个直径，可形成椭球。
- 圆柱的 X/Z 是截面直径，Y 是高度，可形成椭圆柱。
- 平面使用 X/Z 尺寸；Y 尺寸不参与显示。
- 几何尺寸和缩放独立，最终尺寸为二者逐分量相乘。
- 姿态以 Qt `QQuaternion::fromEulerAngles` 的俯仰 X、偏航 Y、滚转 Z 约定转换；文件中记录欧拉角，单位为度。

## 文件

扩展名为 `.bagua.json`，包含格式版本、单位、坐标轴、时间步长、对象 ID / 名称 / 类型、位置、旋转、缩放、几何尺寸、RGB 颜色、可见性、碰撞开关、固定状态、质量、初始平移与转动速度、选中对象，以及相机和网格状态。`links` 数组描述部件之间的连接（球铰 / 转动副 / 移动副 / 固定副 / 转动约束 / 软顶点缝合），锚点用物体局部坐标，可带行程或角度限位；`exclusions` 关闭指定部件对之间的接触；对象上的 `anchors` 是可连接的虚拟锚点。这些字段可在装配面板编辑或通过场景文件定义，格式与验证见 [装配实现方案](docs/ARTICULATION_PLAN.md)。旧版本文件缺少物理字段时默认开启碰撞、质量 1 kg，平面固定，其他对象可运动；旧 `joints` 字段会自动转换为 `links`。物理参数支持撤销、复制和保存。

保存采用 `QSaveFile` 原子提交。文件解析和验证成功后才替换当前场景；不合法文件不会清空原场景。支持的场景上限为 256 MB / 10,000 对象，面向小型编辑场景，未对大规模模型性能作承诺。

对象编辑有未保存提示。相机移动和选择不进入撤销栈，也不会单独触发未保存提示；手动保存时会记录当前视图。撤销历史和编辑器窗口布局不属于场景文件。

## 本机构建

本次实际使用：

| 依赖 | 位置 / 版本 |
| --- | --- |
| CUDA | `C:/Program Files/NVIDIA GPU Computing Toolkit/CUDA/v13.4` |
| Chrono 源码 | `D:/Collection/Chrono`，10.0.0 |
| Chrono 构建 / 安装 | 工作区 `.deps/chrono-build` / `.deps/chrono-install` |
| libuipc（原生后端） | 工作区 `.deps/hsc-native`，由 `scripts/BuildHscNative.ps1` 构建 |
| Qt | 工作区 `.deps/Qt/6.8.3/msvc2022_64` |
| Eigen | 工作区 `.deps/eigen-3.4.0` |
| 编译器 | Visual Studio 2026，MSVC 14.50，x64 |
| 构建工具 | Visual Studio 自带 CMake / Ninja |
| CUDA 架构 | 默认 89，适配本机 RTX 4060，可用脚本参数修改 |

`D:/Collection/Qt/6.12.0/mingw_64` 是 MinGW 版本，不能直接与本工程的 MSVC / CUDA C++ 库混用。故本次使用独立的 Qt MSVC SDK；没有修改原 Qt 或 Chrono 目录。

在项目目录运行 PowerShell：

```powershell
# 首次需要先准备原生 HSC 后端（拉取并构建固定版本的 libuipc，耗时较长）。
# 只在 HSC 关闭时使用 Chrono 链的构建可以跳过这一步。
.\scripts\BuildHscNative.ps1

# 完整构建、测试与打包。
.\scripts\Build.ps1 -Package

# 指定其他兼容 SDK。
.\scripts\Build.ps1 -QtRoot 'D:/Qt/6.8.3/msvc2022_64' `
  -EigenRoot 'D:/Libraries/eigen-3.4.0' `
  -ChronoSource 'D:/Collection/Chrono' `
  -CudaRoot 'C:/Program Files/NVIDIA GPU Computing Toolkit/CUDA/v13.4'

# 仅编辑器，不编译 CUDA / Chrono，使用独立构建目录。
.\scripts\Build.ps1 -EditorOnly -BuildDirectory build-editor

# 清除 CMake 配置缓存并重新探测工具链。
.\scripts\Build.ps1 -Fresh

# 可选：对编辑器运行 AddressSanitizer 检查。
.\scripts\Build.ps1 -EditorOnly -Sanitize -BuildDirectory build-asan
```

`BuildHscNative.ps1` 会把 libuipc 源码放在 `.deps/hsc-native`，用 vcpkg 安装依赖、应用 `native/overlays` 的补丁，然后构建 `BaguaHscNative.dll`；`Build.ps1` 随后把它与 libuipc 运行库一起复制到构建目录。改动 `native/` 下的文件后重新运行 `BuildHscNative.ps1 -BuildOnly` 做增量构建。缺少该 DLL 时，依赖原生后端的测试会失败并提示先构建它。

脚本自动查找 Visual Studio。默认使用本机验证过的 14.50 工具集，可用 `-MsvcToolset` 指定其他已安装版本。选择 MSVC 工具链对应的 Qt SDK，并确保该 MSVC 版本在 CUDA 支持范围内。

`.deps`、`build*`、`dist` 与 `artifacts` 不进入版本控制。全新检出时需要先准备 SDK。Qt 可使用官方安装器选择 MSVC 包；也可以使用 aqtinstall 获取 `6.8.3 win64_msvc2022_64` 的 `qtbase` 存档。Eigen 3.4.0 仅需头文件。应用运行不依赖 Python。

也可使用标准 CMake，在 x64 MSVC 开发环境中配置：

```powershell
cmake -S . -B build-manual -G Ninja -DCMAKE_BUILD_TYPE=Release `
  -DCMAKE_PREFIX_PATH='D:/Qt/msvc2022_64;D:/Chrono/install' `
  -DCMAKE_CUDA_ARCHITECTURES=89
cmake --build build-manual
```

## 代码结构

| 文件 | 职责 |
| --- | --- |
| `src/Scene.*` | 独立场景数据、JSON 验证 / 原子保存、快照撤销命令 |
| `src/Geometry.*` | 基本几何网格与射线 / 三角形拾取 |
| `src/Viewport.*` | OpenGL 渲染、相机、选中高亮和操作柄交互 |
| `src/MainWindow.*` | 多文档页签、活动撤销栈、菜单、停靠面板、属性编辑与文件流程 |
| `src/Backends.*` | CUDA 对照与 Chrono 实际步进自检 |
| `src/Hsc.*` / `src/ChronoHsc.*` | CPU HSC 参考算法与公开 Chrono 求解器接口适配（保留的基线） |
| `src/HscNative.*` | 原生后端桥：场景 → JSON → `BaguaHscNative.dll`，快照回读与状态统计 |
| `native/HscNativeWorld.cpp` | 原生世界：ABD、四面体 FEM、接触、关节、连接与快照 |
| `native/LinearHsc.cu` / `native/overlays/` | CUDA HSC 线性求解器与 libuipc 覆盖层补丁 |
| `src/Physics.*` | 后端选择、Chrono 运行时、碰撞体、质量惯量、固定步长动力学及预览状态 |
| `src/Fem.*` / `src/MainWindowFem.cpp` | 弹性盒四面体、变形表面及参数编辑 |
| `src/MainWindowSettings.cpp` | 物理后端开关、参数和便携设置 |
| `src/MainWindowVelocity.cpp` | 初始平移 / 转动速度编辑 |
| `src/Assets.*` | 网格导入（含 Gmsh 四面体）、单位与坐标转换、内嵌资产序列化 |
| `src/Recording.*` | 记录写入、索引读取、解压缓存、刚体与变形顶点插值 |
| `src/MainWindowAssets.cpp` | 资产库、录制/回放面板、便携项目与拖放 |
| `scripts/ImportHSC.py` | 从 HSC 真实快照转换两组自包含回放与来源记录 |
| `scripts/BuildHscNative.ps1` | 固定版本 libuipc 的拉取、补丁与构建 |
| `src/CudaBackend.cu` | CUDA 世界包围盒计算，结果与 CPU 对照 |
| `tests/EditorTests.cpp` | 场景持久化、异常文件、几何拾取、原生后端和编辑交互测试 |

关闭 HSC 时，含弹性体的场景使用 Chrono SMC + 共旋四面体 FEM（默认 SparseLU）；纯刚体使用 NSC / PSOR。开启 HSC 时整套物理改由原生 libuipc 后端负责：ABD、四面体 FEM、IPC 接触与 CCD、Newton，以及本项目移植的 CUDA HSC 线性求解器。详见 [HSC 接入说明](docs/HSC_INTEGRATION.md)。

CUDA 参与三处：场景包围盒自检、原生后端的接触与求解内核、HSC 线性求解器。Chrono 路径的纯刚体求解在 CPU 上执行，使用 Bullet 碰撞检测、PSOR 求解器（100 次最大迭代）、场景 dt（默认 1/240 s）、重力 (0, -9.81, 0) m/s²。质量由属性指定，惯量根据实际缩放尺寸及均匀密度形状计算。Chrono 接触材质统一使用摩擦系数 0.5、恢复系数 0.1；原生后端使用 IPC 势垒（`d_hat` 默认 1 mm，摩擦系数 0.5）。

## 刚体仿真

打开 `examples/physics-drop.bagua.json`，点击播放可查看物体落地。选中对象，在右侧修改「启用碰撞」「固定刚体」「质量」「初始速度」。关闭碰撞仅关闭接触，动态刚体仍受重力影响；固定刚体不运动；隐藏对象仍参与物理计算。创建和从场景文件载入的对象均支持这些属性。

盒体使用盒碰撞体；球 / 椭球使用对应解析碰撞体；圆柱使用 Y 轴圆柱碰撞体；椭圆柱使用与视口一致的 40 边凸包。平面始终固定，碰撞体为显示面下方厚 0.1 m 的有限尺寸板，顶面与可见平面对齐；不是无限地面。旋转、非均匀缩放均映射到碰撞几何。

播放从编辑场景建立独立运行状态，初始速度取自上一步设置的值。暂停保留当前结果；单步推进一个场景 dt；重置恢复原始编辑场景。运行与暂停预览期间禁止对象编辑，可以选择物体和移动相机；属性显示原始编辑值。保存始终保存原始对象及物理设置，不保存瞬时仿真状态。页签切换自动暂停离开的场景，返回后可继续或重置。仿真不会写入撤销历史或触发未保存标记。

物理步进与渲染分别按固定步长和 Qt 定时器驱动，每次最多累计 0.1 s，过载时仿真可能慢于真实时间。当前为小场景交互演示：Chrono 路径是离散碰撞，不保证捕获高速物体穿越薄障碍物，关节支持球铰 / 转动副 / 移动副 / 受驱动转动；原生后端使用 IPC 势垒与 CCD，可拒绝高速穿透。有效物理尺寸限制为 0.001–10000 m，质量 0.001–1e9 kg，不符合范围时日志提示启动失败。原生后端要求动态刚体使用封闭三角表面。

场景使用单层对象列表，尚未提供父子层级、多选、吸附或 STEP/CAD 装配导入。底部提供四种内建几何体及已导入网格的资产记录。

## 验证

`Build.ps1` 在构建后自动运行 QtTest / CTest，结果见 `build/test-results.txt`。测试使用实际 OpenGL 上下文，要求可用的 Windows 图形会话。完整后端构建的自检要求可用 CUDA GPU；无 GPU 的自动化环境可选择 `-EditorOnly`。

图形检查可生成本应用窗口截图并自动退出：

```powershell
.\dist\BaguaDT\BaguaDT.exe --capture D:/Collection/BaguaDT/artifacts/editor-preview.png
```

依赖声明见 [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md)。项目代码采用 MIT 许可证。

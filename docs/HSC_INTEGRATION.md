# HSC 接入研究与当前实现

更新：2026-10-19。当前有两个可切换的物理后端：默认的 Chrono 刚体 / FEM 链，以及启用 HSC 时使用的**原生 libuipc 后端**（12 自由度 ABD、四面体 FEM、IPC 接触与 CCD、Newton 迭代与 CUDA HSC 线性求解）。HSC 开关不再只是替换 Chrono 内部的一个线性求解器，而是替换整套物理模块。

## 使用与切换

1. 工具 → 物理模拟设置…，勾选「启用 HSC 原生物理系统（CUDA）」，确定。默认关闭。
2. 工具 → 打开 HSC 原生接触示例（`examples/hsc-native-contact.bagua.json`，或包内 `OpenHSCNative.cmd`）；刚柔耦合示例仍是 `examples/hsc-coupled.bagua.json` / `OpenHSCCoupled.cmd`。
3. 顶栏「开始模拟」或 F5 推进；F6 暂停、F7 单步、F8 重置。右侧属性可以调弹性材料、网格分辨率、底面约束、连接刚体和连接刚度，以及初始平移 / 转动速度。
4. 状态栏在原生后端运行中显示「时间 · HSC 秩N · r=残差」；悬停可看求解器描述。关闭 HSC 时显示「时间 · N 接触」。
5. 对照时先重置，切换开关，再开始模拟。两侧物理模型不同，不能把结果差异都归因于 HSC。

设置写在可执行文件旁 `BaguaDT.ini`，重启后保留。开关只影响下一次重置后建立的物理系统；运行中改变设置不会更换已建立的求解器。

| 场景 | HSC 关闭 | HSC 开启 |
| --- | --- | --- |
| 纯刚体 | Chrono NSC / PSOR / Bullet | 原生 ABD / IPC / CCD / Newton / HSC |
| 含弹性体 | Chrono SMC / 共旋四面体 FEM / SparseLU | 原生四面体 FEM / IPC |
| 已保存记录回放 | 读取状态 | 读取相同状态，不启动求解器 |

## 原生后端的组成

原生后端是本机 `.deps/hsc-native` 中固定版本的 libuipc 构建，加上本项目的三个覆盖层与一个线性求解器：

| 组成 | 位置 |
| --- | --- |
| 场景桥（Qt → JSON → 原生世界） | `src/HscNative.cpp` |
| 原生世界（ABD / FEM / 接触 / 关节 / 快照） | `native/HscNativeWorld.cpp` |
| CUDA HSC 线性求解器 | `native/LinearHsc.cu`，补丁把它安装为 libuipc 的 `linear_system/linear_hsc.cu` |
| 覆盖层（补丁） | `native/overlays/`：`log_neo_hookean`、`paper_rotation` 约束 |
| 构建与补丁脚本 | `scripts/BuildHscNative.ps1`、`scripts/hsc_native/patch.py` |

补丁只改固定版本 SDK 的副本，原仓库与 Chrono 都不被修改：它把全局线性系统的稀疏矩阵暴露给求解器、在场景配置里注册 `linear_hsc` 与五个 HSC 参数、修正非有限能量的线搜索判定，并加入本项目的 `native/` 子目录。每次打补丁都记录改动文件的 SHA-256 到 `patch-manifest.json`，源码不匹配时直接报错，而不是盲目套用。

`scripts/Build.ps1` 把 `BaguaHscNative.dll` 与 libuipc 运行库一起复制到构建目录与 `dist`，因此编辑器不自带 CUDA 版本的物理代码，而是加载这份固定版本的原生构建。缺少该 DLL 时，开启 HSC 会直接失败并报告加载错误，不会静默回退到 Chrono。

### 场景映射

| 编辑器对象 | 原生物理模型 |
| --- | --- |
| 立方体 / 球 / 圆柱 / 平面（非变形） | 12 自由度 ABD 刚体；平面是厚 0.1 m 的固定板，**顶面与对象的 y 重合**，不是无限地面 |
| 立方体 + 「弹性体 · FEM」 | 按分辨率细分的四面体 FEM，对数 Neo-Hookean 材料，可固定底面节点 |
| 四面体资产（Gmsh 2.2 ASCII，`.msh`） | 直接使用资产的四面体与表面，可作为变形体 |
| 关节（场景文件 `links`） | ABD 球铰 / 转动副 / 移动副 / 固定副，可带限位；软顶点缝合连接 FEM 与 ABD（胎唇、弹簧） |
| 弹性体的「连接刚体」 | FEM 节点与 ABD 表面的有限长度软缝合（静止长度等于初始间距） |

ABD 需要**封闭流形**三角表面：每条边必须恰好被两个三角形共用。内置的立方体、球、圆柱都按此构造（球的两极与经线接缝、圆柱的侧面接缝都精确合并，且不产生零面积三角形）；导入的网格若不封闭，启动时以「Dynamic bodies need a watertight surface」拒绝，而不是让原生层给出内部断言。

平面使用对象位置作为可见面高度，`Object` 的默认位置是 y = 0.5，因此把平面当作地面时要把位置设到地面高度。

### 数值设置

| 项 | 值 |
| --- | --- |
| 时间步长 | 场景 dt，默认 1/240 s；两后端均生效 |
| 重力 | (0, −9.81, 0) m/s² |
| 接触 | IPC 势垒，`d_hat` 默认 1 mm，摩擦系数 0.5 |
| 接触刚度 | 请求值默认 1e8 Pa，会被按质量、`d_hat`、时间步推出的自适应通道限制，并在日志中提示实际取值 |
| Newton | 最多 300 次；线搜索最多 64 次；严格模式开启（超限报错，不静默降级） |
| ACA 秩上限 | 默认 48（设置面板可改） |
| Neumann 项数 / ω | 默认 5 / 0.7 |
| Schur PCG | 相对残差默认 1e-4，迭代上限默认 2000 |
| 自适应延长 ABD 逆 | 默认关闭（复现项目的增强项） |

全局线性系统使用 `linear_hsc`：按 3 / 12 自由度分块的 Jacobi-Neumann 多项式近似逆、全主元 ACA 低秩耦合块、Schur 补与 PCG。接触、缝合与关节都进入同一套装配，因此刚柔耦合块来自实际的接触与约束，而不是测试夹具构造的占位项。

### 接触与防穿透

IPC 势垒在距离小于 `d_hat` 时提供能量，高速情形由轨迹过滤器与线搜索共同保证不穿越：每步对候选的顶点-三角形、边-边和点对做最早接触时间检查，线搜索再拒绝增加能量（穿透）的试探步。

自动测试 `nativeHscContactCcdAndReplay` 用 0.1 m 立方体以 100 m/s 撞厚 0.1 m 的板：一步 4.17 ms 的自由位移是 0.417 m，远大于板厚，最终停在板上方约 1 mm（`d_hat`）的间隙，且接触计数非零。同一测试还验证原生快照能写入 `.brec` 并逐顶点回放。

## Chrono HSC 适配（保留的历史实现）

在原生后端之前，本项目曾把 HSC 实现为 `chrono::ChSolver` 的子类 `ChronoHsc`，在 Chrono 的 SMC/FEM 系统里替换线性求解器。该实现仍保留在代码与测试中（`hscMatchesReproductionReference`、`hscChronoCoupledToggle`），用于和 CPU 参考求解器 `src/Hsc.cpp` 对照，但编辑器的开关不再选择它：`HscOptions::nativePipeline` 为真时直接使用原生后端。

保留这段历史的原因与教训：

- 当前 Chrono 版本的 `ChContactSMC::CreateJacobians()` / `CalculateJacobians()` 主体被注释，仅开启 `SetContactStiff(true)` 得不到完整的刚柔接触交叉刚度；把刚体放在弹性盒上会有接触力，却不能据此声称验证了耦合块。示例因此改用公开的 stiff `ChLoadNodeXYZBodyBushing` 构造非零 `E`。
- Chrono 的线性化隐式 Euler 系统与论文 IPC 的能量形式不同，HSC 在那条链上求解的是 `M + hR + h²K` 类增量系统。
- 稠密 CPU 存储上限 1536 个活动自由度，只适合小型验证。

原生后端把这些限制变成明确的取舍：接触与 CCD 由 libuipc 负责，HSC 只负责求解该非线性步内的线性系统。

## 研究依据

已核对本机下列原始材料，而不是仅根据名称推断算法：

- `D:/Collection/HSC/Guo 等 - 2026 - Heterogeneous Subspace Corrections for GPU Deformable Multibody Dynamics.pdf`，尤其第 4–5 页公式 (2)–(6)。
- 同目录补充材料 `s26-multibody-supp.pdf` 中的 ACA / 求解算法。
- `D:/Collection/HSC_reimpl/reproduction/core.py`：块矩阵、Neumann 多项式、ACA、Schur PCG 与制造解测试。
- `D:/Collection/HSC_reimpl/reproduction/hsc_backend/linear_hsc.cu`：libuipc 线性求解器接口、CUDA FEM/Schur 乘积、CPU 小型 ABD/ACA 初始化，以及自适应 ABD 逆近似扩展。
- 本机 Chrono 10 的 `ChSolver`、`ChSystemDescriptor`、FEA 示例、`ChLoadsNodeXYZ`、SMC 接触实现。

论文使用 FEM 节点的 3 自由度与 ABD 物体的 12 自由度，处理耦合 Newton 系统：

```text
H = [ D   Eᵀ ]     [δf]   [bf]
    [ E    A ]  ·  [δq] = [bq]

S = D − Eᵀ A⁻¹ E
```

HSC 用 ACA 近似耦合块 `E ≈ U Vᵀ`，用按 3/12 自由度分块的 Jacobi-Neumann 多项式近似逆，再通过 Schur 补和 PCG 求解。论文使用 GPU，并以 IPC 等非线性动力学、接触和约束系统形成矩阵。HSC 本身不生成碰撞、不定义弹性材料，也不自动保证网格不翻转。

复现项目的原生后端面向特定 libuipc 版本，不是可直接链接进 Chrono 的通用 CUDA DLL。它的小型 ABD/ACA 初始化实际在 CPU 上，FEM/Schur 迭代在 CUDA 上。其自适应逆扩展属于复现增强，不能等同于论文固定项数的全部实验设置。

## 模拟、录制和回放的控制约定

| 入口 | 唯一职责 |
| --- | --- |
| 顶栏开始 / 暂停 / 单步 / 重置，F5–F8 | 控制当前页签的实时物理系统 |
| 底部模拟并录制 | 同时启动物理系统与录制；后续仍分别由下面两条控制 |
| 底部录制 / 停止录制 | 采样物理状态，不启动或恢复模拟 |
| 底部记录列表 | 选择、回放、保存或删除某一条录制，不推进物理求解 |
| 底部离线高质量 | 录制采样策略：锁步固定步长，不追实时 |
| 底部开始回放 / 暂停回放 / 时间轴 / 前后帧 / 退出回放 | 控制保存数据的游标，不推进物理求解 |

同一页签内模拟预览与回放互斥。进入回放前必须由顶栏重置模拟；回放期间顶栏模拟按钮禁用，F5–F8 不映射到回放。退出回放使用底部按钮。各页签仍各自保留录制和运行状态。

录制沿用 `.brec` 的变形顶点格式，不仅保存刚体位置。元数据包含实际求解器描述与 `hscEnabled`；场景初始状态包含 FEM 材料、分辨率和连接参数。回放不依赖当前 HSC 开关。记录不是求解器检查点，不能从中恢复速度或继续一次物理积分。

## 验证

本轮审计后完整构建下 QtTest 为 **48 passed / 0 failed / 0 skipped**（含初始化与清理）。与原生后端直接相关的测试：

| 测试 | 覆盖内容 |
| --- | --- |
| `nativeHscWorldSmoke` | 4 个时间步的自由落体位移与解析值一致，编辑场景未被运行时修改 |
| `nativeHscContactCcdAndReplay` | 刚柔 IPC 接触产生非零接触与耦合秩；100 m/s 撞击不穿透；原生快照写入 `.brec` 并逐顶点回放 |
| `nativeHscJoints` | 球铰 / 转动副 / 移动副 / 受驱动转动四类关节都能推进且收敛 |
| `tetrahedralAssetImport` | Gmsh 2.2 ASCII 四面体导入、持久化，并在原生与 Chrono 后端启动 |
| `primitiveMeshesAreWatertight` | 立方体 / 球 / 圆柱表面封闭且无退化三角形（ABD 的前提） |
| `hscMatchesReproductionReference` | 复现项目导出的三组 SPD 制造解（刚度 1e2 / 1e4 / 1e6） |
| `hscChronoCoupledToggle` | Chrono 链上 HSC 与 SparseLU 推进 480 步，耦合秩 3、最终相对残差约 1.63e-10 |

实际运行两个示例（`--hsc on --simulate-steps`）：刚柔耦合示例推进到 2.000 s，HSC 秩 3、r ≈ 2.4e-3，弹性盒在连接球处可见下陷；原生接触示例推进到 2.500 s，HSC 秩 9、r ≈ 2.2e-3，落体压出基座凹坑。两者都是本机实时计算结果。

## 限制与下一步

- 原生链按增量势能执行 Newton / IPC；历史 Chrono 适配才是 Chrono 隐式 Euler 的增量矩阵。两者不能混称。尚未验证论文全部实验设置；旋转能量的相对参考系约定详见审计报告。
- 接触刚度落入自适应通道时会被改写（日志提示），不能把它当作精确的材料参数。
- 单一 CUDA 设备、严格模式开启：Newton 或线搜索超限会直接报错，需要重置。
- 网格必须封闭、拓扑固定；不支持每帧增删对象或改变网格连接关系。
- 关节可通过底部装配面板创建、编辑、删除和拾取锚点，也支持场景文件的 `links` 字段。支持球铰 / 转动副 / 移动副 / 固定副 / 转动约束、关节限位、软顶点缝合与部件对接触排除；驱动电机与轴向阻尼尚未实现。格式与验证见 [装配实现方案](ARTICULATION_PLAN.md)。
- 尚未给出与论文同规模模型的性能基准，也没有在 CPU 后端上做过等效性验证。
- 下一步优先级：真实车辆驱动与阻尼、专用装配导入，以及同场景的定量基准。详见 [审计报告](AUDIT_2026-10-19.md)。

状态栏的 r 是最后一次 HSC 方向相对原始完整矩阵的残差，设置面板的 PCG 容差用于 Schur 子系统，两者不同；非线性步骤是否收敛由原生 Newton 检查判断。不能仅凭 r 或子系统收敛宣称整套模型与论文等价。

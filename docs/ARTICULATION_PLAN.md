# 复杂骨架链接（装配）实现方案

更新：2026-10-19。针对 `examples/HSC` 里那套车辆资产"能导入、不能仿真"的问题，先调研原复现项目的链接实现，再给出本项目的落地计划。

下文 `scene.py` 指 `D:\Collection\HSC_reimpl\reproduction\vehicle_assembly\scene.py`，`run_case.py` 指 `...\experiments\vehicle_full_pass\run_case.py`；其余路径相对本仓库。

## 结论

1. **缺的不是物理能力，而是装配的表达与桥接。** 我们固定的 libuipc 版本已经具备原项目用到的全部原语：ABD 球铰 / 转动副 / 移动副 / 固定副、驱动副、关节限位、关节轴向外力、软顶点缝合。项目缺的是场景里"链接"的数据模型、原生后端的构建设置、编辑器界面，以及从原项目资产生成装配体的导入器。
2. **原项目的车辆是四类连接拼出来的**：移动副（悬架导柱，含限位和阻尼通道）+ 软缝合弹簧（悬架刚度）+ 转动副（轮毂，含驱动电机）+ 每轮 39 对软缝合（胎唇连接刚性轮辋与变形轮胎）。它没有任何"复杂骨架"的专门机制，全部由这四类原语组合而成。
3. **唯一的库级缺口是"没有原生阻尼"和"没有真正的力矩电机"**：原项目用 `external_force` + 每帧动画回调自己写阻尼，用每帧设定 `aim_angle` 的方式做位置级驱动。我们可以照做，不必改库。

## 接手前的历史状态：为什么这套资产只能回放

`examples/HSC` 是两组 `.brec` 录制（7 个轮胎的逐顶点变形都在里面），本目录只做回放。如果把底盘、轮辋、转向节、轮胎当作网格导入，当前程序只能把它们当成互不相干的刚体/变形体，无法还原装配关系：

| 车辆需要 | 当前实现 | 位置 |
| --- | --- | --- |
| 移动副 + 行程限位 | 只有 `prismatic`，无 `limit` | `native/HscNativeWorld.cpp:130-160` |
| 悬架弹簧（非零静止长度） | 只有单对 `attachmentBody`，静止长度固定为初始间距 | `native/HscNativeWorld.cpp:110-122` |
| 转动副 + 驱动电机 | 只有 `hinge`；"rotation" 是自研 `paper_rotation` 约束，不是驱动副 | `native/overlays/src/constitution/paper_rotation.cpp` |
| 每轮 39 对胎唇缝合 | 每场景只支持 FEM→ABD 的单对连接 | `native/HscNativeWorld.cpp:122` |
| 关节限位 / 阻尼 | 无 | — |
| 特定部件对之间关闭接触 | 只能整对象关碰撞 | `native/HscNativeWorld.cpp:138` |
| 刚体上的连接点（虚拟锚点） | 无 | — |
| 1/30 s 时间步 | 硬编码 1/240 s | `src/Physics.h:22`、`src/HscNative.cpp:121` |
| 关节编辑 | 只能手写场景 JSON 的 `joints`，且校验要求两个非变形体、世界坐标锚点、无自由度扩展 | `src/Scene.cpp:369-390` |

所以"导入资产"和"计算仿真"之间缺的是装配这一层，而不是网格或求解器。

## 调研：原项目如何实现组件链接

### 1. 装配拓扑

```text
ground (implicit half-plane)
   └── IPC 接触
chassis (ABD, 2086.52 kg, 4 个虚拟锚点)
   ├─ wheel_0..3 : spindle_i (ABD, 34.155 kg, 1 锚点)
   │     ├── [prismatic  +Z, ratio 1e5] ── chassis
   │     │     ├── AffineBodyPrismaticJointLimit(-0.15, +0.15, 100)
   │     │     └── AffineBodyPrismaticJointExternalForce (阻尼通道)
   │     ├── [SoftVertexStitch, 1 对, k=45374.0375 N/m, rest=0.4627 m] ── chassis
   │     └── wheel_i_rim (ABD, 18.8 kg, 39 个虚拟锚点)
   │           ├── [revolute +Y, ratio 1e5] ── spindle_i
   │           │     └── AffineBodyDrivingRevoluteJoint(1e4) + 每帧 aim_angle
   │           └── [SoftVertexStitch, 39 对, k=1e8 N/m, rest=0] ── wheel_i_tire (FEM, 3548 顶点 / 11171 四面体 / 63.81 kg)
   └── loose_0..2 : 独立的 rim+tire（只作障碍物，不与车连接）
```

没有任何物体固定（`is_fixed` 全为 0），整车由轮胎与地面接触支撑。

### 2. 四类连接与参数

| 连接 | 构型 | 关键参数 | 代码 |
| --- | --- | --- | --- |
| 悬架导柱 | `AffineBodyPrismaticJoint`（chassis→spindle），轴 = 两个世界点 `p`、`p+[0,0,0.2]` | `strength_ratio 1e5` | `scene.py:183-188` |
| 行程限位 | `AffineBodyPrismaticJointLimit` | `[-0.15, +0.15] m`，strength 100（三次罚函数，软限位） | `scene.py:188` |
| 悬架弹簧 | `SoftVertexStitch`（chassis 锚点 ↔ spindle 锚点，1 对） | `k=45374.0375`，`rest=0.462663`（= 0.35 + mg/4k，静载预压） | `scene.py:191`、`:293-294` |
| 阻尼 | `AffineBodyPrismaticJointExternalForce` + 动画回调 | `c=4753.8923 N·s/m`，按 `(d-prev)/dt` 写反向力 | `scene.py:187-188`、`:210-216` |
| 轮毂 | `AffineBodyRevoluteJoint`（spindle→rim），轴 = 轮心 ±0.10 m（+Y） | `strength_ratio 1e5` | `scene.py:192-195` |
| 电机 | `AffineBodyDrivingRevoluteJoint`（1e4）+ 动画写 `aim_angle` | `2.0 rad/s`，斜坡 0.5 s（二次→线性） | `scene.py:196-198`、`:217-223` |
| 胎唇 | `SoftVertexStitch`（rim 锚点 ↔ 轮胎胎唇节点，39 对） | `k=1e8 N/m`，`rest=0` | `scene.py:167`、`:176` |
| 接触排除 | `contact_tabular().insert(a, b, 0, 1e10, false)` | chassis↔spindle、spindle↔rim 各 4 对 | `scene.py:152-154`、`:224-225` |

`strength_ratio` 是**无量纲比值**，实际刚度 `K = ratio × (m_i + m_j)`（libuipc 实现如此），所以底盘-导柱的等效刚度约 2.1e8，轮毂约 5.3e6。这解释了为什么两个关节都用 1e5 却软硬不同。

### 3. 三个关键技巧

**虚拟锚点顶点。** 刚体网格末尾追加若干顶点（轮辋 39 个在胎唇位置、底盘 4 个在悬架安装点、转向节 1 个在轮心），设置 `builtin.is_surf = 0` 使其不参与表面/体积/接触，并把这些顶点的 `contact_element_id` 指向一个专用接触元素，该元素与所有其它元素之间的接触都被关闭（`scene.py:126-150`、`:109-112`）。这样 `SoftVertexStitch` 才有真实顶点可以缝合——缝合是**顶点级**约束，不能直接连到刚体的 12 自由度上。

**阻尼自己写。** libuipc 没有轴向阻尼构型，原项目在动画回调里读移动副的 `distance` 属性，用后向差分算速度并写 `external_force`（`scene.py:210-216`）。注意 `c/dt` 相当于一个很大的弹簧刚度，稳定性依赖时间步——这是 1/30 s 时间步下调出来的。

**静载预压。** 弹簧静止长度不是 Chrono 的自由长度，而是 `0.35 + (mass*9.8/4)/k`，使整车在初始位姿就接近静平衡（实测 1.20% 误差）。

### 4. 原项目自己的取舍与坑

- 双叉臂被简化成"导柱 + 滑块"，且滑块与转向节合并为**一个** ABD 体（`scene.py:291` 注释、README 说明）——不是论文原模型。
- 限位是罚能量、不是硬截断，关节都是有限刚度，会有微小漂移（行程实测最大 0.1157 m < 0.15 m，横向误差 4.4e-7 m）。
- `AffineBodyDrivingRevoluteJoint` 是**位置级驱动**（设定目标角），不是力矩电机；没有刹车、没有转向自由度、没有防倾杆。
- 求解器侧：固定 5 项 Neumann 时，刚性关节耦合让完整系统残差停在 ~0.998，必须启用自适应多项式延长（我们已有 `nativeAdaptiveInverse` 开关，默认关闭）。
- 7 个 FEM 轮胎共 78,197 个四面体；1e6 Pa 的软胎会翻转，最终用 LogNeoHookean + `det(F) > 1e-8` 的线搜索检查。

### 5. 原项目没有记录的东西

`scene_manifest.json` 只记录了物体（质量、位姿、锚点数）、车轮（顶点/四面体/胎唇数）、悬架（k、rest、c、行程、电机转速）、排除对和缝合（对数、k、rest）。**没有任何逐关节数据**：prismatic/revolute 的锚点、轴端点、`strength_ratio`、限位强度、驱动强度都不在文件里，只能从 `scene.py` 读。这意味着"按 manifest 导入即可复原装配"做不到，导入器要么内置这套装配规则，要么原项目先导出装配描述。

## 我们可用的原语（已核对）

| 需求 | libuipc 原语（我们的固定版本） | 备注 |
| --- | --- | --- |
| 球铰 / 转动副 / 移动副 / 固定副 | `AffineBodySphericalJoint` / `RevoluteJoint` / `PrismaticJoint` / `FixedJoint` | 均有世界坐标与**体坐标**两套 `create_geometry` |
| 关节限位 | `AffineBodyPrismaticJointLimit` / `RevoluteJointLimit` | 三次罚函数，`lower < upper` |
| 驱动 | `AffineBodyDrivingRevoluteJoint` / `DrivingPrismaticJoint` | 需先建基础关节，再写 `aim_angle` / `aim_distance` |
| 关节轴向力 | `AffineBodyPrismaticJointExternalForce` / `RevoluteJointExternalForce` | 阻尼/作动通道 |
| 顶点缝合 | `SoftVertexStitch`（弹簧）、`VertexEdgeStitch`、`VertexTriangleStitch` | 顶点级，单实例；只要 `simplicial_complex` + `global_vertex_offset` |
| 每帧控制 | `Animator::insert(obj, callback)`，`UpdateInfo{dt, frame, geo_slots}` | 电机与阻尼都走这里 |
| 部件对排除 | `ContactTabular::insert(a, b, ..., enable=false)` | 已有 API，我们只用了整对象开关 |

**ABD↔FEM 缝合确认可用（已实测）**：`SoftVertexStitch::create_geometry` 只要求两侧是单实例 `SimplicialComplex` 且带 `global_vertex_offset`（`src/constitution/soft_vertex_stitch.cpp:36-51`、`.../soft_vertex_stitch.cu:160-180`），而 ABD 几何由 `affine_body_vertex_reporter.cu` 分配该偏移，顶点也进入全局顶点缓冲。M0 尖刀测试（固定 FEM 底座 + 自由 ABD 配重 + 单对缝合）实测：240 步后配重停在静置间隙 0.1915 m（静止长度 0.1905 m，拉伸 1.0 mm = 19.62 N ÷ 20000 N/m），没有自由落体。该场景已固化为回归测试 `femAbdStitchCarriesLoad`。原项目 39 对胎唇缝合的实测误差 0.028–0.062 mm 与此一致。


## 实现进展

M1（连接表达）、M2（原生链接构建）及 M6 的装配编辑已实现；本轮全面审计与修正见 [审计报告](AUDIT_2026-10-19.md)。M3 的真实车辆驱动与阻尼、M4/M5 整车验证仍未完成。

### 已落地的场景格式（v1 场景文件向后兼容）

```json
"dt": 0.03333333333333333,
"objects": [
  {"id": "wheel_0_rim", "shape": "mesh", "anchors": [[0.2495, -0.1390, 0]]}
],
"links": [
  {"id": "susp_0", "type": "prismatic", "a": "chassis", "b": "spindle_0",
   "anchorA": [-2.6, -0.951, 0.829], "anchorB": [-2.6, -0.951, 0.479],
   "axis": [0, 0, 1], "strength": 1e5,
   "limit": {"lower": -0.15, "upper": 0.15, "strength": 100}},
  {"id": "susp_0_spring", "type": "stitch", "a": "chassis", "b": "spindle_0",
   "pairs": [[0, 0]], "anchorPairs": true, "stiffness": 45374.04, "rest": 0.462663},
  {"id": "wheel_0_bead", "type": "stitch", "a": "wheel_0_rim", "b": "wheel_0_tire",
   "pairs": [[0, 12], [1, 15]], "stiffness": 1e8, "rest": 0}
],
"exclusions": [{"a": "chassis", "b": "spindle_0"}]
```

| 字段 | 语义 |
| --- | --- |
| `dt` | 场景级时间步，`[1/2000, 1/10]` 秒；原生后端与步进循环都按它推进 |
| `objects[].anchors` | 体坐标虚拟锚点顶点：不参与表面/体积/接触，只作为连接点 |
| `links[].type` | `spherical` / `revolute` / `prismatic` / `fixed` / `stitch`（`spring` 同义）/ 历史 `rotation` |
| `anchorA` / `anchorB` | 体坐标米的锚点；跟随平移、旋转与 scale，dimensions 不重复缩放锚点 |
| `axis` | 世界方向，构建时按各自刚体的姿态换算；`revolute` / `prismatic` / `rotation` 使用 |
| `strength` | libuipc 无量纲强度比，等效刚度 `K = strength × (m_a + m_b)` |
| `limit` | `revolute`（弧度）或 `prismatic`（米）的软限位，`lower < upper` |
| `pairs` / `anchorPairs` | 缝合顶点对：`anchorPairs` 为真时两侧都是锚点序号，否则第二项是对方网格顶点号 |
| `stiffness` / `rest` | 缝合刚度（N/m）与静止长度（m，0 表示零长弹簧） |
| `exclusions` | 指定部件对之间关闭接触，装配体内部允许故意重叠 |

加载时校验：类型、对象存在且不相等、运动学链接两端必须是刚体、锚点序号与缝合顶点范围、限位区间、轴非零、`dt` 范围。旧场景的 `joints`（世界锚点、`ball/hinge` 命名）会自动映射为 `links`，世界锚点按各自刚体姿态换算成体坐标。

### 已实现的原生构建

- 四类关节用体坐标锚点构建（`revolute`/`prismatic` 用两个点定义轴，按各自刚体姿态换算世界轴）；`rotation` 保留原 `paper_rotation` 语义。
- 限位在基础关节之后施加到同一份关节几何上。
- 缝合按顶点对构建，支持零长与有限静止长度；`anchorPairs` 让刚体之间的弹簧直接引用两侧锚点。
- 虚拟锚点：追加在形状顶点之后、`is_surf = 0`、独立接触元素，并与所有物体元素关闭接触。
- 接触排除：逐对关闭，并与"整对象关碰撞"去重。
- 诊断：每步在快照里返回每条链接的 `distance` / `angle` / 缝合误差 / 限位区间，Qt 侧通过 `Physics::linkStats()` 读取。

### 验证结果

| 检查 | 结果 |
| --- | --- |
| M0 缝合保持力 | 固定 FEM 底座吊 2 kg 自由刚体，240 步后 0.1915 m（静止长度 0.1905 m，拉伸 1.0 mm = 19.62 N ÷ 20000 N/m），测试 `femAbdStitchCarriesLoad` |
| 装配式悬架 | 固定底座 → 移动副（限位 ±0.15）→ 弹簧缝合（k=20000，rest=0.3）→ 转动副挂轮；240 步后收敛，导柱行程 −0.0505 m（设计 −0.05），弹簧误差 0.49 mm，轮心 0.2995 m（设计 0.299），测试 `nativeArticulatedSuspension` |
| 接触排除 | 重叠 0.1 m 的两体：带排除时可初始化、60 步收敛、0 接触、本体不位移；去掉排除则 libuipc 直接拒绝世界（"World is not valid"），测试 `nativeContactExclusionAllowsOverlap` 与探针对照 |
| 格式与校验 | 五类链接与限位/缝合字段往返一致；8 类非法输入被拒绝且不覆盖现有场景；旧 `joints` 迁移正确，测试 `linkModelPersistenceAndValidation` |
| 时间步 | 场景 `dt=1/120` 时 `Physics::stepSize()` 与之一致，原生后端按它推进 |

全套 39 项测试通过。仍未覆盖：电机与阻尼（M3）、真实车辆导入（M5）、Chrono 侧的限位/弹簧子集（M7）。

## 实现方案与后续计划

已落地的部分见上文「实现进展」。下面只保留尚未实现的部分与设计依据。

### 1. 电机与阻尼（M3，下一步）

原项目的轮毂驱动是**位置级**的：基础转动副 + `AffineBodyDrivingRevoluteJoint`，再由动画回调每帧写 `aim_angle`（`scene.py:196-198`、`:217-223`）。阻尼没有原生构型，原项目在动画回调里读移动副的 `distance` 做后向差分，写 `external_force`（`scene.py:210-216`）。

落到我们的实现：

1. `Link` 增加 `drive { speed, ramp, strength }`：构建时在基础关节几何上调用 `AffineBodyDrivingRevoluteJoint::apply_to(sc, strength)`，把 `driving/is_constrained` 置 1。
2. 在 `world->init()` 之前用 `Animator::insert(obj, callback)` 注册回调，回调里按 `phase(t)`（二次→线性斜坡）写 `aim_angle = speed · phase`；`phase` 与 `scene.py` 一致。
3. `Link` 增加 `damper { coefficient }`：对带阻尼的移动副调用 `AffineBodyPrismaticJointExternalForce::apply_to(sc, 0)` 并把 `external_force/is_constrained` 置 1；回调里 `vel = (distance - last)/dt`、`external_force = -c · vel`、更新 `last`。
4. 电机相位与阻尼历史都必须进快照，否则暂停/继续与录制回放会不一致；录制只存几何，不受影响。

注意：驱动副是"每帧给目标角"，不是力矩电机；没有刹车、转向与防倾杆。阻尼等价刚度约 `c/dt`，换 `dt` 必须重新校核。

### 2. 动画通道的约束

- 回调必须在 `world->init()` 之前注册（`Animator::insert` 只能追加对象，不能事后补回调）。
- 我们的原生世界只有 `advance()` / `retrieve()` 两步，循环结构不用改；只需在构造阶段注册并持有状态。
- 回调里通过 `info.geo_slots()` 拿到关节几何的 `edges()` 属性，直接改 `aim_angle` / `external_force`。

### 3. 编辑器装配面板（M6，已实现基础编辑）

- 链接列表：按物体分组的树（chassis → 4×spindle → 4×rim → 4×tire），显示类型、强度（含换算 K）、限位、缝合对数。
- 新建：选两个物体、选类型，再在视口点选锚点与轴；世界坐标自动换算成体坐标写入 `anchorA/anchorB`。
- 编辑：限位、缝合对（支持"按锚点自动配对"）、接触排除矩阵（默认按同一链条相邻部件预填）。
- 运行诊断：直接读 `Physics::linkStats()`，显示每条链接的行程/角度/缝合误差，超阈值高亮。
- 启动前校验复用场景加载的规则，一次性把问题列进日志。

### 4. 车辆导入（M5）

原项目的 manifest 不记录逐关节数据，所以导入分两步：

1. **通用部分**：读 `scene_manifest.json` 的 `bodies` / `wheels`（质量、位姿、锚点数）、`geometry.npz`（`body_v_i/body_f_i`、`tire_faces/tire_tets`）、`tire_tet4.npz` 的 `BC_CONN`（胎唇节点）生成 `objects`。
2. **装配规则（本车型专用）**：按 `build_vehicle` 重建 `links`——轮心 ±0.10 m 为转动副，轮心与底盘锚点为移动副，底盘第 i 锚点与转向节第 0 锚点为弹簧，轮辋 39 锚点与 `BC_CONN` 为胎唇缝合，8 对接触排除；参数取 manifest 的 `suspensions` / `bindings` / `materials`。

产物：`examples/hsc-vehicle/vehicle.bagua.json`（可仿真）与更小的 `vehicle-mini.bagua.json`（底盘 + 2 轮 + 1 障碍物）。现有 `.brec` 回放保持不动。

长期做法：在原项目侧给 manifest 增加 `links` 段，导入器就不依赖专用规则。

### 5. Chrono 路径的定位（M7）

Chrono 没有 ABD，变形体与刚体的装配只能靠节点-刚体衬套（每胎唇节点一个，7×39=273 个载荷），代价高、语义也不同。因此：

- **原生后端是装配的保真路径**；
- Chrono 路径只保证刚体链接子集（已实现球铰/转动副/移动副/固定副/转动约束），缝合链接会明确报错并提示启用 HSC，而不是静默降级；
- Chrono 侧的限位与弹簧-阻尼映射尚未实现。

### 6. 剩余设计细节

- 场景级 `dt` 已生效；UI 里还需要给 1/30 这类大步长加稳定性提示。
- 锚点已有点选、高亮、数值编辑与轴线显示；批量框选与轴方向的视口拖拽尚未实现。
- 关节诊断的阈值（行程、横向漂移、缝合误差）建议做成场景可配置，用于回归测试。


### 7. 验收标准（M4/M5，尚未执行）

| 检查 | 判据 | 对应原项目 |
| --- | --- | --- |
| M0 缝合保持力 ✅ | 固定 FEM 底座吊自由 ABD 配重，240 步后停在静止长度上方 1.0 mm（19.6 N ÷ 20000 N/m），没有自由落体 | `max_bead_error_m` |
| 胎唇保持力（M4） | 真实轮胎 39 对胎唇接在轮辋上，行驶后误差 < 0.1 mm | `max_bead_error_m` |
| 静置平衡 | 整车静置 60 步，弹簧长度变化 < 3%，行程 ≈ 0 | `spring balance < 3%` |
| 驱动传递 | 轮辋转角与 `aim_angle` 差 < 0.02 rad | `wheel_rotation_transmitted` |
| 关节漂移 | 移动副横向误差 < 1 mm，转动副锚点误差 < 1 mm | `prismatic_lateral_error` / `revolute_anchor_error` |
| 限位 | 行程 < 0.151 m，越界时残差上升 | `travel_limits_m` |
| 整程越障 | 四轮通过三个障碍，离地间隙 > 0.15 m，接触出现 | `status.json` 的 `passage` |
| 性能预算 | 记录每帧墙钟时间，与 `elapsed_s / simulation_time_s ≈ 2770` 的原项目对比 | `status.json` |

验收顺序：M0 已完成；M4 用 mini 场景通过胎唇保持力、静置平衡、驱动传递、关节漂移四项；再跑整程。整程允许比原项目慢，但必须能跑完并可回放。


## 风险与取舍

1. **长耗时**：原项目 7.1 s 仿真用了 19,670 s（约 5.5 小时），78,197 个四面体 + 12 个 ABD。我们即使对齐 dt=1/30 也不会更快；UI 必须能显示进度与预计剩余时间，并允许中途暂停/继续（我们只有录制，没有求解器检查点，暂停即停在帧边界）。
2. **HSC 秩与迭代**：车辆实测秩 84–96，远超我们的默认 48；`max_iter` 需要 12000。默认值要按场景调整，否则会以"未收敛"失败。
3. **刚性关节耦合**：固定项的 Neumann 多项式会失效（原项目残差 ~0.998），必须开自适应延长；否则表现为 Newton 不收敛而非明显报错。
4. **步长相关调参**：阻尼 `c/dt` 与弹簧在 1/30 s 下调好，改 dt 需要重新校核；最小演示也应固定 dt。
5. **资产依赖**：可仿真场景依赖原项目的 `assets/*.npz`、`tire_tet4.npz`、Chrono HMMWV json。要么把这些资产随示例分发（注意许可，`licenses/` 已有 COACD/Manifold3D/Chrono 条目），要么在导入时要求用户指向原项目目录。
6. **轮胎翻转**：软胎（1e6 Pa）会翻转，需要 LogNeoHookean + 线搜索的 `det(F)` 检查；我们的原生后端已用 LogNeoHookean 覆盖层，已有正行列式路径检查；真实轮胎仍需 M5 专门验证。
7. **不是论文原模型**：原项目自己声明是 Chrono 衍生替代装配，双叉臂被简化。我们的导入器照搬即可，但文档不能声称复现论文几何。
8. **回放与仿真分离**：`examples/HSC` 的 `.brec` 仍是回放；新场景是另一份数据，不要把两者混在同一个示例目录里，以免用户误以为回放在重算。

## 附录：车辆装配参数（导入器需要的完整清单）

| 项 | 值 |
| --- | --- |
| 质量 | chassis 2086.52 kg；rim 18.8 kg；spindle 34.155 kg；tire 63.809 kg |
| 网格 | chassis 287 v/562 f；rim 137 v/192 f；spindle 9 v/12 f；tire 3548 v/11171 tet |
| 锚点 | chassis 4；rim 39（胎唇位置）；spindle 1（轮心） |
| 悬架 | k=45374.0375 N/m，c=4753.8923 N·s/m，rest=0.462663 m，行程 ±0.15 m |
| 电机 | 2.0 rad/s，斜坡 0.5 s，驱动 strength 1e4 |
| 关节强度 | prismatic 1e5（K≈2.12e8），revolute 1e5（K≈5.30e6） |
| 胎唇缝合 | 39 对/轮，k=1e8 N/m，rest=0 |
| 材质 | ABD κ=1e9；胎 E=1e10（装车）/1e8、1e6（障碍）；ν=0.3；ρ=1000 |
| 接触 | 默认 (μ=0.6, κ=1e10)，`d_hat`=1e-3，摩擦开启 |
| 求解 | dt=1/30；newton.max_iter=400；line_search 32；velocity_tol=3e-3；HSC rank≤256（实测 84–96）、neumann 5、ω=0.7、自适应逆开启 |
| 排除 | chassis↔spindle（导柱重叠）、spindle↔rim（轮毂腔） |

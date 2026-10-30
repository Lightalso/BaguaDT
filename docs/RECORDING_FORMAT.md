# BaguaDT recording / project v1

`.brec` 是单文件，几何、元数据和状态自包含。所有数值默认小端；浮点位置为 IEEE754 float32，时间为 float64。字符串为不带终止字符的 UTF-8。Qt 的 QVector/QObject 对象布局不写入文件。

| 文件部分 | 编码 |
| --- | --- |
| magic | 8 字节 ASCII `BGREC001` |
| header size | uint32 |
| header | 指定字节数的 UTF-8 JSON |
| frame count | uint32 |
| frame index | 每帧 float64 时间秒、uint64 绝对字节偏移、uint32 压缩数据长度，共 20 字节 |
| frame data | 按索引连续排列的独立压缩块 |

header 字段：`format="baguadt.recording"`、`version=1`、`scene`（完整 `.bagua.json` 场景）、`metadata`（开放的来源/求解器/参数字典）。scene 的 `assets` 对象按资产 ID 存放网格名称、来源、SHA-256、`positions` 三维顶点数组和扁平的三角形 `indices`；场景对象以 `mesh` 引用资产 ID。

本应用写入的 `metadata` 键：`producer`（求解器描述）、`hscEnabled`、`created`（UTC ISO 时间）、`integrationStep`（秒）、`sampling`（`lock-step fixed timestep` 表示一帧一个固定步长，`render-tick physical timestamps` 表示按渲染节拍采样）。读取方不应依赖这些键必然存在。

帧压缩块采用 Qt `qCompress` 表示：4 字节**大端** uint32 解压长度，随后为 zlib 数据。Python 等外部程序可用 `struct.pack('>I', len(raw)) + zlib.compress(raw, 3)` 写入。

每个解压帧的内容：

1. uint32 对象数，与 header 中对象列表顺序一致。
2. 每个对象依次写入：uint32 ID 字节数、UTF-8 ID；12 个 float32，顺序为位置 XYZ、欧拉旋转 XYZ（Qt 角度约定，度）、缩放 XYZ、几何尺寸 XYZ；uint8 可见性（0/1）；uint32 网格顶点数；顶点数×3 个 float32 局部 XYZ 坐标。

基本几何体顶点数为 0；网格的顶点数必须与初始资产一致，三角形拓扑固定，源/材料信息继承初始场景。物体变换顺序为局部尺寸×缩放、Qt 欧拉旋转、平移。网格顶点可发生变形，包括 ABD 非刚体仿射形变。

时间从 0 开始且严格递增；可以是不均匀采样。索引不得重叠、跳出文件或留有未引用尾部；读入检查格式、长度、时间、对象 ID、数值有限性和拓扑一致性。索引与边界帧在打开时验证，其他帧按需验证；读取损坏帧时暂停回放并显示错误，不覆盖编辑场景。单文件上限 8 GiB，最多 100 万帧；头部上限 256 MiB，单帧解压上限 128 MiB。更大的求解结果建议分段记录。

项目清单 `.bgp`（旧版 `.bagua.project.json` 仍可读取）：

```json
{
  "format": "baguadt.project",
  "version": 1,
  "name": "Example",
  "scenes": [
    {"name": "Case", "scene": "data/scene.bagua.json", "recording": "data/run.brec", "mode": "replay", "time": 1.5}
  ]
}
```

`scene` 和 `recording` 至少提供一个。只有 recording 时以记录的初始场景建立页签，默认回放模式。两者同时提供时，scene 保留编辑版本，recording 保留录制时的初始版本，二者互不覆盖。mode 可为 edit/replay，time 为秒；载入暂停。所有路径必须相对于项目目录，并保持在该目录内部。

一个场景可以保存多条录制：`recordings` 是这些记录的路径数组（顺序即列表顺序），`recording` 指向其中被选中的一条，便于只认识单条记录的旧读取方继续工作。没有 `recordings` 时按单条 `recording` 处理。

```json
{
  "format": "baguadt.project",
  "version": 1,
  "name": "Example",
  "scenes": [
    {"name": "Case", "scene": "data/scene.bagua.json",
     "recordings": ["data/take1.brec", "data/take2.brec"],
     "recording": "data/take2.brec",
     "mode": "replay", "time": 1.5}
  ]
}
```

逐步录制使用场景 `dt`，不是固定写死 1/240 s。录制中切换模式时 `sampling` 为 `mixed`，`samplingChanges` 按 `fromFrame` / `time` / `mode` 记录之后的采样方式。原生 ABD 网格的可选 `frameVertices`（四个独立顶点）与 `frameRestInverse`（行优先 4×4 矩阵）用于从已有逐帧顶点恢复完整仿射连接点坐标系；BGREC001 帧载荷格式保持兼容。

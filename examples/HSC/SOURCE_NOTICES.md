# 第三方来源及修改记录

本目录的 `upstream/` 保存精选原始文件，而非完整项目 checkout。所有来源、commit、文件长度和 SHA-256 见 `download_manifest.json`。

| 来源 | 本次看到的许可证文件 | 使用范围 |
|---|---|---|
| Project Chrono | `upstream/projectchrono__chrono/LICENSE`，BSD-3-Clause | 车身、轮辋、轮胎、车辆配置及加载器参考 |
| libuipc | `upstream/spiriMirror__libuipc/LICENSE`，Apache-2.0 | 接触、CCD、FEM/ABD 和关节后端；早期装配运行 PyPI pyuipc 0.0.28，当前完整行程使用本地编译的 0.9.0 |
| libuipc-samples | `upstream/spiriMirror__libuipc-samples/LICENSE`，Apache-2.0 | 关节、驱动及接触示例参考 |
| Stiff-GIPC | `upstream/KemengHuang__Stiff-GIPC/LICENSE`，MPL-2.0 | 候选方案源码审查；本目录收集的文件未直接集成或独立运行 |
| IPC Toolkit | `upstream/ipc-sim__ipc-toolkit/LICENSE`，MIT | 候选方案源码审查，未集成或运行 |

上游声明存在两处需要保留的区别：libuipc 根 LICENSE、README、Python 元数据分类和 PyPI 发布元数据标注 Apache-2.0，但所审查 commit 的 `python/LICENSE` 内容是 GPL-3.0；两份原文均已保存。Stiff-GIPC 根目录是 MPL-2.0，README 另有学术/商业使用说明。此处记录事实，不将一个目录的标签自动覆盖所有子目录或依赖。当前本地研究继续可进行；后续若分发集成版本，应针对实际使用版本厘清这些声明。

Chrono HMMWV README 将 JSON 模型参数来源指向 James Aardema 的公开 HMMWV 后球头失效分析技术报告；该 README 已保留。没有找到本次 HMMWV 资产文件旁另列的专用许可证，按仓库根许可证保留来源及全文。

本地衍生物由 `prepare_assets.py` 生成：

1. C3D10 四面体取四个角节点转为 Tet4；不保留二次形函数。
2. 轮胎绕 Z 旋转 90°，轴由 X 对齐到 Y，按统一比例缩至最大半径 0.47 m。
3. 车身平移、四个安装车轮的位置、地面 12 个车轮阵列及轮辋显示缩放均为本地装配选择。
4. OBJ、MSH、NPZ、预览图和 JSON 不是作者原场景，也不是物理模拟帧。

以上四项描述资产准备阶段。当前完整行程只使用一排三个地面轮胎；`result/vehicle_full_pass/` 中的逐帧顶点和预览来自实际求解，不能与静态资产预览混淆。

当前后端固定于 libuipc 提交 `9c748a759bb9952a7aeedd59076a70591dc61d6c`，本地加入 HSC、对数 Neo-Hookean 和防翻转线搜索。接口和内核结构的衍生文件、补丁及来源哈希保存在 [hsc_backend](../hsc_backend/README.md)，其根许可证和 NOTICE 分别保存在 [UPSTREAM-LICENSE](../hsc_backend/UPSTREAM-LICENSE)、[UPSTREAM-NOTICE](../hsc_backend/UPSTREAM-NOTICE)。完整上游源码位于本机 `runtime.json` 所记录的构建根目录。

发布或转交衍生资产时应同时保留本文件和 `upstream/projectchrono__chrono/LICENSE`。原始下载文件未修改。

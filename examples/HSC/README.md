# HSC 车辆越障 · BaguaDT 示例

在 BaguaDT 的「项目 → 打开 HSC 车辆回放项目」中打开，或打开本目录 `HSC.bgp`。程序目录也提供 `OpenHSC.cmd`。

两页签展示已完成的原生 HSC 运行结果：地面胎 E=1e8 / 1e6 Pa，时长 7.100 / 6.967 秒。点击底部「回放」，可拖动时间轴、逐帧、变速或循环播放；七个轮胎保留逐顶点变形。

本例读取已有真实运行快照，没有在 BaguaDT 内重跑 HSC。车辆是原复现项目采用的 Chrono 衍生替代资产。重跑原求解器需要另行准备原项目的 HSC/libuipc 运行环境。

本目录可以随 BaguaDT 完整复制，回放不依赖原始 `HSC_reimpl` 目录，也不需要 Python。`provenance` 保留原始参数、状态、轨迹和求解统计；`validation.json` 提供源坐标核验点；许可及来源见 `SOURCE_NOTICES.md` 和 `Chrono-ASSETS-LICENSE`。

完整操作说明见程序包的 `docs/ASSETS_AND_REPLAY.md`，数据格式见 `docs/RECORDING_FORMAT.md`。

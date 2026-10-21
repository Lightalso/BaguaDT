# 来源及许可

本目录是 `D:/Collection/HSC_reimpl` 的整车装配导入结果，车辆几何属于该复现项目使用的 Project Chrono 衍生替代资产，并非论文作者发布的原始整车模型。

车身、轮辋及轮胎的来源遵循 Project Chrono BSD-3-Clause 许可证，全文保存在同目录 `Chrono-ASSETS-LICENSE`。资产的既有变换、四面体降阶和装配来源说明另见 `../HSC/SOURCE_NOTICES.md`。此次导入保留原复现的质量、材料、驱动速度、悬架刚度、阻尼及胎圈节点集合，新增 Y 向上坐标转换和 BaguaDT 场景格式。

原生后端固定于 libuipc 提交 `9c748a759bb9952a7aeedd59076a70591dc61d6c`；后端及其依赖的许可证随程序保存在 `licenses/`。Chrono 库源码未作修改。

# 可重新求解的 HSC 整车

使用包内 `OpenHSCVehicle.cmd` 或工具菜单“打开 HSC 整车仿真示例”，打开 `HSC-Vehicle.bagua.project.json`。两页签分别包含 `ground_1e8.bagua.json` 与 `ground_1e6.bagua.json` 的完整初始装配，顶栏开始物理模拟。底部播放控制只操作记录；它不会启动求解。

`validation/ground_1e8.partial.brec` 为此次实际求解的部分试跑：13 步、0.433333 s，含初始状态共 14 帧。已重新打开并逐帧验证。按用户要求未继续整程计算，未完成越障；详细指标见 [验证报告](../../docs/HSC_VEHICLE_VALIDATION.md)。

车辆来自本机 `D:/Collection/HSC_reimpl` 中的 Chrono 衍生替代资产。它包含 12 个 ABD 刚体、7 个四面体轮胎、无限地面、4 个驱动转动副、4 个带限位及粘性阻尼的移动副、4 个悬架弹簧与 7 条胎圈缝合链接。七个轮胎合计 78,197 个四面体，胎圈共有 273 对连接。

本目录不读取原工程的逐帧模拟结果。`.provenance.json` 保留导入源和脚本的 SHA-256；源坐标通过 `(x,y,z) → (x,z,-y)` 的旋转转换为编辑器的 Y 向上坐标。

在工具菜单选择“导入 HSC 整车装配”，可从原工程重新生成场景；这一步需要 Python 与 NumPy。生成后的场景自包含，运行及回放不依赖 Python 或原工程。

命令行整程验证：

```powershell
.\BaguaVehicleRunner.exe examples/hsc-vehicle/ground_1e8.bagua.json output/vehicle-1e8 600
```

输出目录必须是新目录。程序写入 `status.json`、逐帧 `metrics.jsonl` 和可在编辑器打开的 `vehicle.brec`。在输出目录创建 `STOP` 文件，可在下一帧边界停止并保存部分记录。`COMPLETED_FULL_PASS` 表示四轮跨越障碍、整车尾部超过障碍至少 0.15 m 且持续 5 帧，并且约束检查通过。未完成的部分记录不能作为整程通过证据。

来源及许可见 [SOURCE_NOTICES.md](SOURCE_NOTICES.md)。实现和验收条件见 [整车接入说明](../../docs/HSC_VEHICLE.md)。

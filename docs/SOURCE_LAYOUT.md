# 源代码目录结构

`src/` 按依赖方向分为六层。头文件通过 CMake 的公共 include 目录互相引用，因此代码中仍使用 `#include "Scene.h"` 这样的短路径；新增文件时不要产生重名头文件。

```
src/
  app/          进程入口
  core/         不依赖 Qt Widgets / OpenGL 的场景数据与几何算法
  render/       OpenGL 视口与编辑器主题
  simulation/   物理后端、工作线程会话、HSC / CUDA 原生桥
  io/           网格资产与记录文件格式
  ui/           Qt 编辑器窗口、面板、属性与装配编排
tests/          QtTest 回归测试
native/         libuipc 覆盖层与 CUDA 原生后端
```

## 模块职责

| 目录 | 职责 | 可以依赖 |
| --- | --- | --- |
| `app/` | `main.cpp`：QApplication、主题、命令行与截屏入口 | 全部 |
| `core/` | `Scene` 数据模型、撤销、JSON 持久化；`Geometry` 网格与拾取；`Cache` 统一临时目录 | Qt Core / Gui |
| `render/` | `Viewport` 渲染、操作柄、相机与 HUD；`Theme` 样式 | `core` |
| `simulation/` | `Physics` 后端、`SimulationSession` 工作线程、Chrono / HSC / CUDA 桥 | `core`、`io` 的资产类型 |
| `io/` | `Assets` 导入、`Recording` 记录格式、`.bgp` 项目清单 | `core` |
| `ui/` | 主窗口、场景树、属性、录制面板、装配编排 | 其余所有模块 |

## UI 文件拆分

| 文件 | 内容 |
| --- | --- |
| `ui/MainWindow.cpp` | 构造、菜单/工具栏、场景页签、文档生命周期 |
| `ui/MainWindowDocks.cpp` | 场景树、属性面板、变换字段、选择同步 |
| `ui/MainWindowFile.cpp` | 打开 / 保存 / 另存为 / 关闭确认 |
| `ui/MainWindowSimulation.cpp` | 仿真控制、状态栏、工作线程帧回调 |
| `ui/MainWindowProject.cpp` | `.bgp` 项目状态、最近项目与项目管理入口 |
| `ui/MainWindowComponents.cpp` | 组件拆分 / 导入、网格导入对话框 |
| `ui/MainWindowAssemblyWizard.cpp` | 四轮悬挂、轮辋-轮胎缝合与 HSC 预设 |
| `ui/VehicleAssemblyDialog.*` | 车辆装配向导界面与自动名称匹配 |
| `ui/ProjectManagerDialog.*` | 专用项目管理窗口：最近项目、场景清单、缺失检查 |
| `core/Cache.*` | `<程序目录>/cache` 的创建、启动清理与旧临时目录迁移 |
| `ui/MainWindowAssemblyPanel.cpp` | 装配页的控件树与事件绑定 |
| `ui/MainWindowAssembly.cpp` | 约束读写、表单同步、拾取、诊断 |
| `ui/MainWindowAssemblyShared.h` | 仅装配面板使用的内联 UI 小工具 |
| `ui/MainWindowAssets.cpp` | 资产库、录制/回放面板、便携项目 |
| `ui/EditorControls.*` | 可拖动刷值数值框、可折叠分组 |

## 约束

- `core/`、`render/`、`simulation/` 不包含 `QApplication` 或主窗口头文件。
- `simulation/` 不在 GUI 线程执行求解；`SimulationSession` 通过 `SimulationFrame` 向 `ui/` 投递快照。
- 新增 Qt 控件优先放进对应 `ui/MainWindow*.cpp`，不要把新的业务逻辑继续堆进 `MainWindow.cpp`。
- 新增头文件必须保持基名唯一；CMake 使用基名 include 路径。

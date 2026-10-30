# 在 Qt Creator 中开发

项目根目录只保留 CMake 项目，根目录的 `CMakeLists.txt` 会被 Qt Creator 识别为项目；`CMakePresets.json` 提供两套即开即用的配置。qmake 回退项目在 `qmake/` 下，不会抢占默认项目。

## 打开

1. Qt Creator →「文件 → 打开文件或项目」。
2. 选择根目录的 `CMakeLists.txt`，或选择 `CMakePresets.json`。
3. 在 Configure Project 页面选择：
   - **Qt Creator · Full (CUDA + Chrono) — 默认**：完整 HSC/Chrono 后端、测试和打包程序。
   - **Qt Creator · Editor only**：仅 Qt Widgets/OpenGL 编辑器，不编译 CUDA/Chrono。
4. Kit 选择 **Desktop Qt 6.8.3 MSVC2022 64-bit**。该 kit 已注册并设为 Qt Creator 默认 kit；若被其他项目切换，重新在该项目中选择即可。

## 预设内容

| Preset | 用途 | 构建目录 |
| --- | --- | --- |
| `qtcreator-full` | CUDA + Chrono + 测试，RelWithDebInfo | `build-qtcreator-full` |
| `qtcreator-editor` | 纯编辑器，Debug | `build-qtcreator-editor` |

两者都设置：

- `CMAKE_PREFIX_PATH` 指向项目内 Qt 与 Chrono；
- `CMAKE_CUDA_COMPILER` 指向 CUDA 13.4；
- `CMAKE_CUDA_ARCHITECTURES=89`（RTX 4060）；
- Eigen 路径、CTest 和 `compile_commands.json`。

如果 Qt/CUDA 安装位置不同，新建 `CMakeUserPresets.json` 继承对应 preset 并覆盖变量。该文件已被 `.gitignore` 忽略。

## 代码模型 / clangd

`qmake/BaguaDT.pro` 使用 `$$_PRO_FILE_PWD_` 生成头文件搜索路径，因此在 Qt Creator 的 shadow build 目录中也能找到 `src/io/Assets.h` 等模块头文件。若 Qt Creator 仍显示旧的 include 错误：

1. 关闭项目，删除本机的 `qmake/BaguaDT.pro.user` 和对应 `build` 目录（均为本机生成物）。
2. 重新打开 `qmake/BaguaDT.pro`，选择 **MSVC 2022 + Qt 6.8.3** kit；或改用上面的 CMake preset。
3. 执行一次 qmake/CMake configure，让 Clang Code Model 重新读取 include 路径。

不要用 MinGW kit 打开这个 qmake 项目，除非你已经为 MinGW 单独准备 Qt；项目内 `.deps/Qt` 是 MSVC 构建。

## 运行与调试

- CMake 的 POST_BUILD 会把 Chrono 与 HSC 原生 DLL 复制到可执行文件旁。
- Qt Creator 运行配置的 Working directory 建议设为项目根目录，便于直接访问 `examples/`。
- 完整构建的调试信息来自 `RelWithDebInfo`；编辑器预设是 `Debug`。
- HSC 原生 DLL 若尚未构建，先运行一次：

```powershell
.\scripts\BuildHscNative.ps1 -BuildOnly
```

## qmake 备用项目

`qmake/BaguaDT.pro` 供 Qt Creator 快速打开源码和做纯 UI 编译使用：

- 不包含 CUDA `.cu`、Chrono 适配、矩阵 Runner、VehicleRunner 和测试；
- 不定义 `BAGUA_HAS_CUDA` / `BAGUA_HAS_CHRONO`；
- 完整功能、HSC 仿真与测试请使用 CMake preset。

qmake 生成的 `Makefile`、`*.pro.user` 都在忽略列表中，不属于项目源码。

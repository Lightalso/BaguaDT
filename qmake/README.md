# qmake fallback

The root of this repository is a CMake project. Qt Creator opens the CMake project by default.

This directory contains the optional qmake fallback for quick source navigation and UI-only work:

```text
qmake/BaguaDT.pro
```

It deliberately does not compile the CUDA or Project Chrono backends. For the complete application, HSC vehicle simulation and tests, open the root `CMakeLists.txt` / `CMakePresets.json` and use the **Qt Creator · Full (CUDA + Chrono) — 默认** preset.

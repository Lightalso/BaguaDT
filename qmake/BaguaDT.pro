# Qt Creator / qmake fallback project.
#
# This project is intentionally editor-only: it does not compile the CUDA or
# Project Chrono backends. Use CMakePresets.json ("Qt Creator · Full") for the
# complete application, HSC vehicle simulation and tests. This .pro file lives
# outside the repository root so Qt Creator opens the CMake project by default.
# Open qmake/BaguaDT.pro explicitly for UI-only work.
QT += core gui widgets opengl openglwidgets

CONFIG += c++17 windows
CONFIG -= app_bundle
TEMPLATE = app
TARGET = BaguaDT

DEFINES += QT_DEPRECATED_WARNINGS
INCLUDEPATH += \
    $$_PRO_FILE_PWD_/../src \
    $$_PRO_FILE_PWD_/../src/core \
    $$_PRO_FILE_PWD_/../src/render \
    $$_PRO_FILE_PWD_/../src/ui \
    $$_PRO_FILE_PWD_/../src/simulation \
    $$_PRO_FILE_PWD_/../src/io \
    $$_PRO_FILE_PWD_/../.deps/eigen-3.4.0

win32-msvc* {
    QMAKE_CXXFLAGS += /utf-8 /W4 /permissive-
}

SOURCES += \
    ../src/app/main.cpp \
    ../src/core/Cache.cpp \
    ../src/core/Geometry.cpp \
    ../src/core/Scene.cpp \
    ../src/io/Assets.cpp \
    ../src/io/ProjectFile.cpp \
    ../src/io/Recording.cpp \
    ../src/render/Theme.cpp \
    ../src/render/Viewport.cpp \
    ../src/simulation/Backends.cpp \
    ../src/simulation/ChronoHsc.cpp \
    ../src/simulation/Fem.cpp \
    ../src/simulation/Hsc.cpp \
    ../src/simulation/HscNative.cpp \
    ../src/simulation/Physics.cpp \
    ../src/simulation/SimulationSession.cpp \
    ../src/ui/EditorControls.cpp \
    ../src/ui/MainWindow.cpp \
    ../src/ui/MainWindowAssembly.cpp \
    ../src/ui/MainWindowAssemblyPanel.cpp \
    ../src/ui/MainWindowAssemblyWizard.cpp \
    ../src/ui/MainWindowAssets.cpp \
    ../src/ui/MainWindowComponents.cpp \
    ../src/ui/MainWindowDocks.cpp \
    ../src/ui/MainWindowFem.cpp \
    ../src/ui/MainWindowFile.cpp \
    ../src/ui/MainWindowProject.cpp \
    ../src/ui/MainWindowSettings.cpp \
    ../src/ui/MainWindowSimulation.cpp \
    ../src/ui/MainWindowVehicle.cpp \
    ../src/ui/MainWindowVelocity.cpp \
    ../src/ui/ProjectManagerDialog.cpp \
    ../src/ui/VehicleAssemblyDialog.cpp

HEADERS += \
    ../src/core/Cache.h \
    ../src/core/Geometry.h \
    ../src/core/Scene.h \
    ../src/io/Assets.h \
    ../src/io/ProjectFile.h \
    ../src/io/Recording.h \
    ../src/render/Theme.h \
    ../src/render/Viewport.h \
    ../src/simulation/Backends.h \
    ../src/simulation/ChronoHsc.h \
    ../src/simulation/Fem.h \
    ../src/simulation/Hsc.h \
    ../src/simulation/HscNative.h \
    ../src/simulation/HscTypes.h \
    ../src/simulation/PaperRotationMath.h \
    ../src/simulation/Physics.h \
    ../src/simulation/SimulationSession.h \
    ../src/ui/EditorControls.h \
    ../src/ui/MainWindow.h \
    ../src/ui/MainWindowAssemblyShared.h \
    ../src/ui/ProjectManagerDialog.h \
    ../src/ui/VehicleAssemblyDialog.h

DISTFILES += \
    ../CMakeLists.txt \
    ../CMakePresets.json \
    ../README.md \
    ../docs

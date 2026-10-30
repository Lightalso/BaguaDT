cmake_minimum_required(VERSION 3.24)
project(BaguaDT VERSION 0.3.0 LANGUAGES CXX)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
option(BAGUA_ENABLE_CUDA "Build CUDA scene-transform backend" ON)
option(BAGUA_ENABLE_CHRONO "Build Project Chrono adapter" ON)
option(BAGUA_SANITIZE "Enable MSVC AddressSanitizer for editor debugging" OFF)
find_package(Qt6 6.5 REQUIRED COMPONENTS Widgets OpenGLWidgets OpenGL Test)
find_path(BAGUA_EIGEN_INCLUDE Eigen/Dense HINTS "${CMAKE_SOURCE_DIR}/.deps/eigen-3.4.0" REQUIRED)

# Qt Creator runs the executable from the build directory. Copy the native and
# Chrono runtime DLLs next to the target so F5 works without a separate package
# step. Qt's own DLLs are supplied by the Qt Creator kit environment.
function(bagua_copy_windows_runtime target)
    if(NOT WIN32)
        return()
    endif()
    foreach(runtime_dir
            "${CMAKE_SOURCE_DIR}/.deps/chrono-install/bin"
            "${CMAKE_SOURCE_DIR}/.deps/hsc-native/build/Release/bin")
        if(EXISTS "${runtime_dir}")
            file(GLOB runtime_dlls "${runtime_dir}/*.dll")
            if(runtime_dlls)
                add_custom_command(TARGET ${target} POST_BUILD
                    COMMAND ${CMAKE_COMMAND} -E copy_if_different ${runtime_dlls} "$<TARGET_FILE_DIR:${target}>"
                    COMMENT "Copying BaguaDT runtime DLLs for ${target}")
            endif()
        endif()
    endforeach()
endfunction()

add_library(bagua_editor STATIC
    # Scene model and geometry
    src/core/Scene.cpp src/core/Scene.h src/core/Geometry.cpp src/core/Geometry.h
    src/core/Cache.cpp src/core/Cache.h
    # Rendering and editor theme
    src/render/Theme.cpp src/render/Theme.h src/render/Viewport.cpp src/render/Viewport.h
    # Qt widgets and the editor shell
    src/ui/EditorControls.cpp src/ui/EditorControls.h
    src/ui/MainWindow.cpp src/ui/MainWindow.h
    src/ui/MainWindowDocks.cpp src/ui/MainWindowFile.cpp src/ui/MainWindowSimulation.cpp
    src/ui/MainWindowProject.cpp src/ui/MainWindowComponents.cpp src/ui/MainWindowAssemblyWizard.cpp
    src/ui/ProjectManagerDialog.cpp src/ui/ProjectManagerDialog.h
    src/ui/VehicleAssemblyDialog.cpp src/ui/VehicleAssemblyDialog.h
    src/ui/MainWindowAssets.cpp src/ui/MainWindowSettings.cpp src/ui/MainWindowFem.cpp
    src/ui/MainWindowVelocity.cpp src/ui/MainWindowAssemblyPanel.cpp src/ui/MainWindowAssembly.cpp
    src/ui/MainWindowAssemblyShared.h src/ui/MainWindowVehicle.cpp
    # Simulation backends, worker session and native bridge
    src/simulation/SimulationSession.cpp src/simulation/SimulationSession.h
    src/simulation/Backends.cpp src/simulation/Backends.h
    src/simulation/Physics.cpp src/simulation/Physics.h src/simulation/Fem.cpp src/simulation/Fem.h
    src/simulation/Hsc.cpp src/simulation/Hsc.h src/simulation/HscTypes.h
    src/simulation/ChronoHsc.cpp src/simulation/ChronoHsc.h
    src/simulation/HscNative.cpp src/simulation/HscNative.h
    # Asset and recording I/O
    src/io/Assets.cpp src/io/Assets.h src/io/Recording.cpp src/io/Recording.h
    src/io/ProjectFile.cpp src/io/ProjectFile.h)
target_include_directories(bagua_editor SYSTEM PRIVATE ${BAGUA_EIGEN_INCLUDE})
target_link_libraries(bagua_editor PUBLIC Qt6::Widgets Qt6::OpenGLWidgets Qt6::OpenGL)
target_include_directories(bagua_editor PUBLIC
    src src/core src/render src/ui src/simulation src/io)
if(MSVC)
    target_compile_options(bagua_editor PUBLIC /utf-8 /W4 /permissive-)
    if(BAGUA_SANITIZE)
        target_compile_options(bagua_editor PUBLIC /fsanitize=address /Z7)
        target_link_options(bagua_editor PUBLIC /INCREMENTAL:NO /DEBUG)
    endif()
endif()
if(BAGUA_ENABLE_CUDA)
    enable_language(CUDA)
    find_package(CUDAToolkit REQUIRED)
    add_library(bagua_cuda STATIC src/simulation/CudaBackend.cu src/simulation/HscCuda.cu)
    set_target_properties(bagua_cuda PROPERTIES CUDA_STANDARD 17 AUTOMOC OFF)
    set_target_properties(bagua_cuda PROPERTIES CUDA_RUNTIME_LIBRARY Static)
    target_compile_options(bagua_cuda PRIVATE $<$<COMPILE_LANGUAGE:CUDA>:-Xcompiler=/utf-8;-Xcompiler=/Zc:preprocessor>)
    target_link_libraries(bagua_cuda PUBLIC CUDA::cudart_static)
    target_link_libraries(bagua_editor PRIVATE bagua_cuda)
    target_compile_definitions(bagua_editor PRIVATE BAGUA_HAS_CUDA)
    add_executable(BaguaHscMatrixRunner src/simulation/HscMatrixRunner.cpp)
    target_link_libraries(BaguaHscMatrixRunner PRIVATE bagua_cuda Qt6::Core)
    bagua_copy_windows_runtime(BaguaHscMatrixRunner)
endif()
if(BAGUA_ENABLE_CHRONO)
    find_package(Chrono CONFIG REQUIRED)
    if(CHRONO_TARGETS)
        target_link_libraries(bagua_editor PRIVATE ${CHRONO_TARGETS})
    else()
        target_include_directories(bagua_editor SYSTEM PRIVATE ${CHRONO_INCLUDE_DIRS})
        target_link_libraries(bagua_editor PRIVATE ${CHRONO_LIBRARIES})
    endif()
    target_compile_definitions(bagua_editor PRIVATE BAGUA_HAS_CHRONO)
endif()
qt_add_executable(BaguaDT WIN32 src/app/main.cpp)
target_link_libraries(BaguaDT PRIVATE bagua_editor)
bagua_copy_windows_runtime(BaguaDT)
add_executable(BaguaVehicleRunner src/simulation/VehicleRunner.cpp)
target_link_libraries(BaguaVehicleRunner PRIVATE bagua_editor)
bagua_copy_windows_runtime(BaguaVehicleRunner)
include(CTest)
if(BUILD_TESTING)
    qt_add_executable(bagua_tests tests/EditorTests.cpp)
    target_link_libraries(bagua_tests PRIVATE bagua_editor Qt6::Test)
    bagua_copy_windows_runtime(bagua_tests)
    if(BAGUA_ENABLE_CUDA)
        target_compile_definitions(bagua_tests PRIVATE BAGUA_TEST_CUDA)
        target_link_libraries(bagua_tests PRIVATE CUDA::cudart_static)
    endif()
    add_test(NAME editor COMMAND bagua_tests -o test-results.txt,txt)
endif()
install(TARGETS BaguaDT RUNTIME DESTINATION .)

cmake_minimum_required(VERSION 3.26)
project(CellularAutomaton3D LANGUAGES CXX)

option(USE_CUDA "Build the CUDA (GPU) back-end" ON)
set(CUDA_ARCH "89" CACHE STRING "CUDA compute capability (89 = Ada, 86 = Ampere, 75 = Turing)")

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

find_package(Qt6 6.4 REQUIRED COMPONENTS Widgets OpenGL OpenGLWidgets)
find_package(Threads REQUIRED)
qt_standard_project_setup()

qt_add_executable(CellularAutomaton3D WIN32 MACOSX_BUNDLE
    src/main.cpp
    src/MainWindow.cpp      src/MainWindow.h
    src/GLViewport.cpp      src/GLViewport.h
    src/Automaton.cpp       src/Automaton.h
    src/ThreadPool.cpp      src/ThreadPool.h
    src/CARule.h
    src/CellInstance.h
    src/CudaAutomaton.h
)
target_link_libraries(CellularAutomaton3D PRIVATE
    Qt6::Widgets Qt6::OpenGL Qt6::OpenGLWidgets Threads::Threads)

# Warnings for the C++ sources only: nvcc does not understand these flags.
if(MSVC)
    set(warning_flags /W4)
else()
    set(warning_flags -Wall -Wextra -Wpedantic)
endif()
target_compile_options(CellularAutomaton3D PRIVATE "$<$<COMPILE_LANGUAGE:CXX>:${warning_flags}>")

# The GPU back-end, or a stub with the same interface when CUDA is switched off.
if(USE_CUDA)
    enable_language(CUDA)
    target_sources(CellularAutomaton3D PRIVATE src/CudaAutomaton.cu)
    # nvcc supports C++20 from CUDA 12.0 on.
    set_target_properties(CellularAutomaton3D PROPERTIES
        CUDA_STANDARD 20
        CUDA_STANDARD_REQUIRED ON
        CUDA_ARCHITECTURES "${CUDA_ARCH}")
else()
    target_sources(CellularAutomaton3D PRIVATE src/CudaAutomatonStub.cpp)
endif()

# Copy the Qt runtime next to the executable so it starts straight from the build tree.
if(WIN32)
    find_program(WINDEPLOYQT_EXE windeployqt HINTS "${Qt6_DIR}/../../../bin")
    if(WINDEPLOYQT_EXE)
        add_custom_command(TARGET CellularAutomaton3D POST_BUILD
            COMMAND "${WINDEPLOYQT_EXE}" --no-translations "$<TARGET_FILE:CellularAutomaton3D>"
            COMMENT "Deploying the Qt runtime with windeployqt")
    endif()
endif()

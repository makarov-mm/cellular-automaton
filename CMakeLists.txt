cmake_minimum_required(VERSION 3.20)
project(CellularAutomaton3D LANGUAGES CXX)

option(USE_CUDA "Build with CUDA GPU acceleration" ON)
set(CUDA_ARCH "89" CACHE STRING "CUDA compute capability (89=Ada, 86=Ampere, 75=Turing)")

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)

find_package(Qt6 REQUIRED COMPONENTS Widgets OpenGL OpenGLWidgets)

set(SOURCES
    src/main.cpp
    src/MainWindow.cpp
    src/GLViewport.cpp
    src/Automaton.cpp
)
set(HEADERS
    src/MainWindow.h
    src/GLViewport.h
    src/Automaton.h
    src/Types.h
    src/CARule.h
)

if(USE_CUDA)
    enable_language(CUDA)
    list(APPEND SOURCES src/CudaAutomaton.cu)
    list(APPEND HEADERS src/CudaAutomaton.h)
endif()

add_executable(CellularAutomaton3D ${SOURCES} ${HEADERS})
target_link_libraries(CellularAutomaton3D PRIVATE
    Qt6::Widgets Qt6::OpenGL Qt6::OpenGLWidgets)

if(USE_CUDA)
    target_compile_definitions(CellularAutomaton3D PRIVATE HAVE_CUDA)
    set_target_properties(CellularAutomaton3D PROPERTIES
        CUDA_ARCHITECTURES "${CUDA_ARCH}"
        CUDA_SEPARABLE_COMPILATION OFF)
endif()

if(WIN32)
    find_program(WINDEPLOYQT_EXE windeployqt HINTS "${Qt6_DIR}/../../../bin")
    if(WINDEPLOYQT_EXE)
        add_custom_command(TARGET CellularAutomaton3D POST_BUILD
            COMMAND "${WINDEPLOYQT_EXE}" --no-translations "$<TARGET_FILE:CellularAutomaton3D>"
            COMMENT "Deploying Qt runtime with windeployqt")
    endif()
endif()

cmake_minimum_required(VERSION 3.21)
project(robot_gui LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC ON)

find_package(Qt6 REQUIRED COMPONENTS Core Widgets Sql Multimedia Network Concurrent)

qt_standard_project_setup()

set(SOURCES
    src/main.cpp
    src/app/MainWindow.cpp
    src/core/AppPaths.cpp
    src/core/ConfigManager.cpp
    src/core/MemoryStore.cpp
    src/core/ReasonerEngine.cpp
    src/core/ResearchService.cpp
    src/core/ToolManager.cpp
    src/core/GgWaveService.cpp
    src/core/SttEngine.cpp
    src/core/TtsEngine.cpp
    src/core/AudioUtils.cpp
    src/core/AgentController.cpp
)

set(HEADERS
    src/app/MainWindow.h
    src/core/AppPaths.h
    src/core/ConfigManager.h
    src/core/MemoryStore.h
    src/core/ReasonerEngine.h
    src/core/ResearchService.h
    src/core/ToolManager.h
    src/core/GgWaveService.h
    src/core/SttEngine.h
    src/core/TtsEngine.h
    src/core/AudioUtils.h
    src/core/AgentController.h
)

add_executable(robot_gui ${SOURCES} ${HEADERS})

target_include_directories(robot_gui PRIVATE src)

add_library(ggwave_local STATIC
    third_party/ggwave/src/ggwave.cpp
)

target_include_directories(ggwave_local PUBLIC
    third_party/ggwave/include
    third_party/ggwave/src
)

target_link_libraries(robot_gui PRIVATE
    Qt6::Core
    Qt6::Widgets
    Qt6::Sql
    Qt6::Multimedia
    Qt6::Network
    Qt6::Concurrent
    ggwave_local
)

if (WIN32)
    target_compile_definitions(robot_gui PRIVATE NOMINMAX)
endif()

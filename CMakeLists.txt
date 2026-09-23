cmake_minimum_required(VERSION 3.20)

project(LibraryManagementSystem
        VERSION 1.0.0
        DESCRIPTION "控制台图书管理系统（C++20 / CMake）"
        LANGUAGES CXX)

# ---- 全局编译配置 --------------------------------------------------------

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "构建类型" FORCE)
endif()

set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)

# 统一的高告警等级，作为 INTERFACE 目标挂到每个 target 上。
add_library(libms_warnings INTERFACE)
if(MSVC)
    target_compile_options(libms_warnings INTERFACE /W4 /permissive- /utf-8)
else()
    target_compile_options(libms_warnings INTERFACE
        -Wall -Wextra -Wpedantic -Wshadow -Wnon-virtual-dtor -Wold-style-cast)
endif()

# MinGW 默认动态链接 libstdc++-6.dll，运行时按 PATH 顺序解析。
# 若 PATH 上存在另一个 MinGW（典型：Git for Windows 自带的旧版 libstdc++），
# 会加载到 ABI 不兼容的运行库并在标准库内部随机崩溃。
# 静态链接运行时让产物自包含，彻底消除这个环境依赖。
add_library(libms_runtime INTERFACE)
if(MINGW)
    target_link_options(libms_runtime INTERFACE -static-libgcc -static-libstdc++)
endif()

# ---- 核心库 --------------------------------------------------------------

add_library(libms STATIC
    src/Book.cpp
    src/Library.cpp
    src/TextStorage.cpp
    src/ConsoleUI.cpp)

target_include_directories(libms PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(libms PRIVATE libms_warnings)

if(MSVC)
    target_compile_options(libms PUBLIC /utf-8)
endif()

# ---- 可执行程序 ----------------------------------------------------------

add_executable(libms_app src/main.cpp)
target_link_libraries(libms_app PRIVATE libms libms_runtime)

# ---- 单元测试 ------------------------------------------------------------

enable_testing()

add_executable(libms_tests tests/test_library.cpp)
target_link_libraries(libms_tests PRIVATE libms libms_runtime)
add_test(NAME libms_unit COMMAND libms_tests)

# ---- 安装规则 ------------------------------------------------------------

include(GNUInstallDirs)
install(TARGETS libms_app RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})

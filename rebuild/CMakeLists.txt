# ============================================================
# mhtabx - 多进程 Tab 管理器（mhtab.exe 开源复刻）
# ============================================================
cmake_minimum_required(VERSION 3.20)
project(mhtabx LANGUAGES CXX)

# C++ 标准
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# 只在 Windows 编译
if(NOT WIN32)
    message(FATAL_ERROR "mhtabx only supports Windows.")
endif()

# MSVC 特定设置
if(MSVC)
    # 使用宽字符 Unicode
    add_compile_definitions(UNICODE _UNICODE)
    # 禁用安全函数警告
    add_compile_definitions(_CRT_SECURE_NO_WARNINGS)
    # 启用所有警告
    add_compile_options(/W4 /permissive-)
    # 静态 CRT 链接（便于分发，无运行时依赖）
    set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
    # 启用 __cplusplus 正确值
    add_compile_options(/Zc:__cplusplus)
endif()

# Windows 版本目标 Win10 RS1 (1607)
add_compile_definitions(
    _WIN32_WINNT=0x0A00
    WINVER=0x0A00
    NOMINMAX
    WIN32_LEAN_AND_MEAN
)

# ============================================================
# 主可执行: mhtabx.exe
# ============================================================
set(MHTABX_SOURCES
    src/main.cpp
    src/MainFrame.cpp
    src/Utils.cpp
    src/resource/app.rc
)

add_executable(mhtabx WIN32 ${MHTABX_SOURCES})

target_include_directories(mhtabx PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_link_libraries(mhtabx PRIVATE
    # Win32 基础库
    user32
    gdi32
    shell32
    advapi32      # CryptoAPI (MD5)
    # Common Controls v6（Tab 控件现代外观）
    comctl32
    # UXTheme（主题支持）
    uxtheme
)

# 链接 Common Controls v6 manifest（启用现代主题）
if(MSVC)
    target_link_options(mhtabx PRIVATE
        "/MANIFESTDEPENDENCY:type='win32' name='Microsoft.Windows.Common-Controls' version='6.0.0.0' processorArchitecture='*' publicKeyToken='6595b64144ccf1df' language='*'"
    )
endif()

# 产物输出到 bin/
set_target_properties(mhtabx PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin
)

# ============================================================
# 示例子进程: demo_child.exe（测试嵌入用）
# ============================================================
add_subdirectory(child)

# ============================================================
# 信息输出
# ============================================================
message(STATUS "----------------------------------------")
message(STATUS "mhtabx build configuration:")
message(STATUS "  Generator:    ${CMAKE_GENERATOR}")
message(STATUS "  Compiler:     ${CMAKE_CXX_COMPILER_ID} ${CMAKE_CXX_COMPILER_VERSION}")
message(STATUS "  C++ Standard: ${CMAKE_CXX_STANDARD}")
message(STATUS "  Output Dir:   ${CMAKE_BINARY_DIR}/bin")
message(STATUS "----------------------------------------")

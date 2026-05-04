# ============================================================
# demo_child.exe - 示例可嵌入子进程
#
# 作用：模拟原 mhtab 的 my.exe 角色，提供一个可被 SetParent
# 嵌入到 mhtabx 主窗口 Tab 页中的测试进程。
# ============================================================

add_executable(demo_child WIN32 demo_child.cpp)

target_include_directories(demo_child PRIVATE
    ${CMAKE_SOURCE_DIR}/include
)

target_link_libraries(demo_child PRIVATE
    user32
    gdi32
)

if(MSVC)
    target_link_options(demo_child PRIVATE
        "/MANIFESTDEPENDENCY:type='win32' name='Microsoft.Windows.Common-Controls' version='6.0.0.0' processorArchitecture='*' publicKeyToken='6595b64144ccf1df' language='*'"
    )
endif()

set_target_properties(demo_child PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin
)

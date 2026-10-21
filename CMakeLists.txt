cmake_minimum_required(VERSION 3.24)

project(gamelauncher_chat LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Qt 的元对象/界面/资源需要 moc/uic/rcc；在顶层统一开启，子目录自动继承
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTOUIC ON)
set(CMAKE_AUTORCC ON)

find_package(Qt6 6.5 REQUIRED COMPONENTS Core Gui Widgets Network Sql Test)
qt_standard_project_setup(REQUIRES 6.5)

# 源文件含中文注释/文案：MSVC 必须按 UTF-8 读取，否则乱码
if(MSVC)
    add_compile_options(/utf-8)
endif()

add_subdirectory(shared)   # protocol（共享协议静态库）
add_subdirectory(client)   # client（Qt GUI 客户端）
add_subdirectory(server)   # mock_server（参考服务端）

# Debug 默认启用增量链接：若上一次链接被打断（exe 仍被占用、构建被终止），
# 残留的 .ilk/.recipe 会让后续增量链接产出损坏的 PE（导入表为空 → 进程在 main 之前崩溃，
# 见 docs/DEVLOG.md 2026-10-21）。本工程完全重链只需数秒，因此 Debug 一律关闭增量链接。
if(MSVC)
    foreach(tgt client mock_server)
        target_link_options(${tgt} PRIVATE "$<$<CONFIG:Debug>:/INCREMENTAL:NO>")
    endforeach()
endif()

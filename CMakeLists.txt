cmake_minimum_required(VERSION 3.19)
project(markdown_tool VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)

if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "" FORCE)
endif()

# Qt 6.2 (Ubuntu 22.04) —— 不用 qt_standard_project_setup(), 那是 6.3+ 才有
#
# core 與測試只需要 QtCore, 刻意不依賴 Gui:
# Ubuntu 的 qt6-base-dev 沒有把 OpenGL 開發標頭拉進來, 少了 libgl1-mesa-dev
# 就會讓 Qt6Gui 整個 find_package 失敗。把 GUI 部分做成可選, 純邏輯測試
# 便能在 GUI 依賴補齊前先跑。
find_package(Qt6 6.2 REQUIRED COMPONENTS Core Test)
find_package(Qt6 6.2 QUIET COMPONENTS Gui Widgets Svg)

# ---- vendored md4c (MIT) ----
add_library(md4c STATIC
    third_party/md4c/md4c.c
    third_party/md4c/md4c-html.c
    third_party/md4c/entity.c
)
target_include_directories(md4c PUBLIC third_party/md4c)
set_target_properties(md4c PROPERTIES AUTOMOC OFF)

# ---- core: 純邏輯, app 與測試共用 ----
add_library(mdcore STATIC
    src/core/MarkdownParser.cpp
    src/core/CodeHighlighter.cpp
)
target_include_directories(mdcore PUBLIC src)
target_link_libraries(mdcore PUBLIC Qt6::Core md4c)

# ---- tests ----
enable_testing()
foreach(t markdownparser)
    add_executable(test_${t} tests/test_${t}.cpp)
    target_link_libraries(test_${t} PRIVATE mdcore Qt6::Test)
    add_test(NAME ${t} COMMAND test_${t})
endforeach()

# ---- GUI app (需要 Qt6 Gui/Widgets/Svg) ----
if(Qt6Widgets_FOUND AND Qt6Svg_FOUND)
    message(STATUS "markdown-tool: GUI 依賴齊備, 會建置 app")
else()
    message(WARNING
        "markdown-tool: 找不到 Qt6 Gui/Widgets/Svg, 只建置 core 與測試。\n"
        "  補齊方式: sudo apt install libgl1-mesa-dev libglx-dev libopengl-dev qt6-svg-dev")
endif()

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
    src/core/IMermaidRenderer.cpp
    src/core/MermaidCache.cpp
    src/core/MmdcRenderer.cpp
    src/core/FileWatcher.cpp
)
target_include_directories(mdcore PUBLIC src)
target_link_libraries(mdcore PUBLIC Qt6::Core md4c)

# ---- gui: 抽成 library 讓 e2e 測試也能連結 ----
if(Qt6Widgets_FOUND AND Qt6Svg_FOUND)
    set(MD_GUI_AVAILABLE TRUE)

    add_library(mdgui STATIC
        src/MainWindow.cpp
        src/Sidebar.cpp
        src/TocPanel.cpp
        src/FileBrowserPanel.cpp
        src/Theme.cpp
        src/render/IRenderBackend.cpp
        src/render/TextBrowserBackend.cpp
    )
    target_include_directories(mdgui PUBLIC src)
    target_link_libraries(mdgui PUBLIC mdcore Qt6::Widgets Qt6::Svg)

    add_executable(markdown-tool src/main.cpp)
    target_link_libraries(markdown-tool PRIVATE mdgui)
else()
    set(MD_GUI_AVAILABLE FALSE)
    message(WARNING
        "markdown-tool: 找不到 Qt6 Gui/Widgets/Svg, 只建置 core 與測試。\n"
        "  補齊方式: sudo apt install libgl1-mesa-dev libglx-dev libopengl-dev qt6-svg-dev")
endif()

# ---- tests ----
enable_testing()

# 純邏輯測試: 只需要 QtCore
foreach(t markdownparser codehighlighter mermaidcache)
    add_executable(test_${t} tests/test_${t}.cpp)
    target_link_libraries(test_${t} PRIVATE mdcore Qt6::Test)
    add_test(NAME ${t} COMMAND test_${t})
endforeach()

# 整合測試: 會真的呼叫外部 mmdc, 找不到時自己 skip
# 這支需要 QtSvg/QtGui: 它要驗「Qt 真的畫得出 mermaid 的文字」
if(MD_GUI_AVAILABLE)
    add_executable(test_mmdc_integration tests/test_mmdc_integration.cpp)
    target_link_libraries(test_mmdc_integration PRIVATE mdcore Qt6::Test Qt6::Gui Qt6::Svg)
    add_test(NAME mmdc_integration COMMAND test_mmdc_integration)
    set_tests_properties(mmdc_integration PROPERTIES
        ENVIRONMENT "QT_QPA_PLATFORM=offscreen" TIMEOUT 300)
endif()

# e2e: 驅動真正的 MainWindow, 用 offscreen platform 跑
if(MD_GUI_AVAILABLE)
    foreach(t e2e_viewer e2e_regression)
        add_executable(test_${t} tests/test_${t}.cpp)
        target_link_libraries(test_${t} PRIVATE mdgui Qt6::Test)
        add_test(NAME ${t} COMMAND test_${t})
        set_tests_properties(${t} PROPERTIES
            ENVIRONMENT "QT_QPA_PLATFORM=offscreen" TIMEOUT 300)
    endforeach()
    target_compile_definitions(test_e2e_regression PRIVATE
        SAMPLE_MD="${CMAKE_CURRENT_SOURCE_DIR}/docs/sample.md")
endif()

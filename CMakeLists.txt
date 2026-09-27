cmake_minimum_required(VERSION 3.12.0)
project(ugjy VERSION 0.1.0 LANGUAGES C)

# C11 標準を強制
set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)


option(UGJY_PORTABLE "Disable -march=native for portable binaries" OFF)
if(MSVC)
    add_compile_options(/O2 /W3 /MP)
else()
    if(NOT UGJY_PORTABLE)
        add_compile_options(-march=native)
    endif()
    add_compile_options(
        -Wall -Wextra -Wpedantic
        -O3 -flto
        -fno-math-errno
        -fno-trapping-math
        -fomit-frame-pointer
        -ffunction-sections -fdata-sections
    )
    add_link_options(-flto -O3)
    if(NOT APPLE)
        add_link_options(-Wl,--gc-sections)
        add_link_options(-s)
    endif()
endif()

# 開発時のバグを早期発見するためのコンパイラ警告
if(MSVC)
    add_compile_options(/W4)
else()
    add_compile_options(-Wall -Wextra -Wpedantic)
endif()

# Src 内の全ソースからライブラリ用ソース（main.c 除外）を抽出
file(GLOB_RECURSE LIB_SOURCES CONFIGURE_DEPENDS "Src/*.c")
list(FILTER LIB_SOURCES EXCLUDE REGEX "Src/main\\.c$")

# 共通インクルードパスの設定
set(UGJY_INC_DIRS
    ${CMAKE_CURRENT_SOURCE_DIR}/Inc
    ${CMAKE_CURRENT_SOURCE_DIR}/Inc/core
    ${CMAKE_CURRENT_SOURCE_DIR}/Inc/model
    ${CMAKE_CURRENT_SOURCE_DIR}/Inc/queue
    ${CMAKE_CURRENT_SOURCE_DIR}/Lib
    ${CMAKE_CURRENT_SOURCE_DIR}/Lib/onnxruntime/include
)

# 1. 共有ライブラリターゲットの定義 (libugjy.so -> Rust / C# FFI 用)
add_library(ugjy_shared SHARED ${LIB_SOURCES})
set_target_properties(ugjy_shared PROPERTIES
    OUTPUT_NAME "ugjy"
    SOVERSION 1
    VERSION 0.1.0
    POSITION_INDEPENDENT_CODE ON
    BUILD_RPATH "${CMAKE_CURRENT_SOURCE_DIR}/Lib/onnxruntime/lib"
)
target_include_directories(ugjy_shared PUBLIC ${UGJY_INC_DIRS})
target_link_directories(ugjy_shared PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/Lib)

if(UNIX AND NOT APPLE)
    target_link_libraries(ugjy_shared PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/Lib/onnxruntime/lib/libonnxruntime.so
        ${CMAKE_CURRENT_SOURCE_DIR}/Lib/libugjy_g2p.a
        m
        pthread
        dl
    )
endif()

# 2. CLI 実行可能ファイルターゲットの定義 (libugjy.so とリンク)
add_executable(ugjy Src/main.c)
target_include_directories(ugjy PRIVATE ${UGJY_INC_DIRS})
target_link_libraries(ugjy PRIVATE ugjy_shared)
set_target_properties(ugjy PROPERTIES
    BUILD_RPATH "${CMAKE_CURRENT_SOURCE_DIR}/Lib/onnxruntime/lib;$ORIGIN"
)

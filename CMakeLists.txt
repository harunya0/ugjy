cmake_minimum_required(VERSION 3.12.0)
project(ugjy VERSION 0.1.0 LANGUAGES C)

# C11 標準を強制
set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)


option(UGJY_PORTABLE "Disable -march=native for portable binaries" OFF)

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
    C_VISIBILITY_PRESET hidden
    VISIBILITY_INLINES_HIDDEN ON
    BUILD_RPATH "${CMAKE_CURRENT_SOURCE_DIR}/Lib/onnxruntime/lib"
)

target_compile_definitions(ugjy_shared PRIVATE UGJY_BUILDING_SHARED)
target_include_directories(ugjy_shared PUBLIC ${UGJY_INC_DIRS})
target_link_directories(ugjy_shared PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/Lib)

if(MSVC)
    target_compile_options(ugjy_shared PRIVATE /O2 /W4 /MP)
else()
    target_compile_options(ugjy_shared PRIVATE
        $<$<NOT:$<BOOL:${UGJY_PORTABLE}>>:-march=native>
        -Wall -Wextra -Wpedantic
        -O3 -flto=auto
        -fvisibility=hidden
        -fno-math-errno
        -fno-trapping-math
        -fomit-frame-pointer
        -ffunction-sections -fdata-sections
    )
    target_link_options(ugjy_shared PRIVATE
        -flto=auto -O3
        $<$<NOT:$<PLATFORM_ID:Darwin>>:-Wl,--gc-sections -s>
    )
endif()

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

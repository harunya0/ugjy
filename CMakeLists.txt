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

# Src 内のすべての .c ファイルを自動検出
# CONFIGURE_DEPENDS を付けることで、新しい .c ファイルを追加した際に CMake が自動検知して再生成します
file(GLOB SOURCES CONFIGURE_DEPENDS "Src/*.c")

# 実行可能ファイルの定義
add_executable(ugjy ${SOURCES})

# インクルードパスの設定
# - Inc: ugjy 自体のヘッダー
# - Lib: 今後配置するサードパーティ製ライブラリのヘッダー（必要に応じてサブディレクトリも追加可能）
target_include_directories(ugjy PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/Inc
    ${CMAKE_CURRENT_SOURCE_DIR}/Lib
    ${CMAKE_CURRENT_SOURCE_DIR}/Lib/onnxruntime/include
)

# 将来 Lib/ 配下に置く外部ライブラリ（.so, .a, .lib など）の検索パス
target_link_directories(ugjy PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/Lib
)

# Linux 環境用: 数学関数（math.h）やスレッド（pthread）を使う場合の標準ライブラリ
if(UNIX AND NOT APPLE)
    target_link_libraries(ugjy PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/Lib/onnxruntime/lib/libonnxruntime.so
        ${CMAKE_CURRENT_SOURCE_DIR}/Lib/libugjy_g2p.a
        m
        pthread
        dl
    )
endif()

# 実行時に libonnxruntime.so を自動で読み込めるようにする
set_target_properties(ugjy PROPERTIES
    BUILD_RPATH "${CMAKE_CURRENT_SOURCE_DIR}/Lib/onnxruntime/lib"
)

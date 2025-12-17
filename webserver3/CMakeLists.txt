cmake_minimum_required(VERSION 4.1)
project(minimal_http_server)
set(CMAKE_CXX_STANDARD 20)

if (MSVC)
    add_compile_options("/utf-8")
endif()

find_package(Threads REQUIRED)

# 定义一个变量来存储所有的源文件
set(SERVER_SOURCES
    src/webserver3.cpp
    src/thread_pool.cpp
    src/HttpRequestParser.cpp
    # ... 每次添加新文件，都需要在这里手动添加 ...
)

# 定义目标名称变量
# 确保这个变量在 add_executable() 之前被定义
set(TARGET_NAME "minimal_http_server")

add_executable(${TARGET_NAME}
    ${SERVER_SOURCES}
)

target_include_directories(${TARGET_NAME} PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
)

target_link_libraries(${TARGET_NAME} PRIVATE
    Threads::Threads
)

# Windows 下需要链接 ws2_32
if (WIN32)
    # 给 minimal_http_server 这个目标添加链接库
    target_link_libraries(${TARGET_NAME} PRIVATE ws2_32)
endif()


# ==============================================================================
# 2. 地址清理器 (Address Sanitizer, ASan) 配置
# ==============================================================================

# 2.1. 定义控制选项
# 用户可以通过命令行 `-DUSE_ASAN=ON` 来启用内存检查功能。默认禁用。
option(USE_ASAN "Enable Address Sanitizer (ASan) and LeakSanitizer (LSan) for checks." OFF)

if(USE_ASAN)
    message(STATUS "--------------------------------------------------------")
    message(STATUS "  Address Sanitizer (ASan/LSan) 启用中...")
    message(STATUS "--------------------------------------------------------")

    # 2.2. GCC/Clang 编译器配置（Linux, macOS 等）
    if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
        message(STATUS "配置适用于 GCC 或 Clang 编译器。")

        # 2.2.1. 定义 ASan 编译阶段标志
        set(ASAN_COMPILE_FLAGS
            # 核心标志，启用 ASan 和 LSan
            -fsanitize=address,leak
            # 添加调试信息，用于可读的堆栈回溯
            -g
            # 优化等级选择0不优化，来保证可以看到代码所有错误
            -O0
            # 确保生成完整的栈帧，对于准确的 ASan 回溯至关重要
            -fno-omit-frame-pointer)

        # 2.2.2. 定义 ASan 链接阶段标志
        # 链接器只需知道启用哪个 Sanitizer 来链接对应的运行时库
        set(ASAN_LINK_FLAGS
            -fsanitize=address,leak)

        # 2.2.3. 应用标志
        # 编译选项应用于目标
        target_compile_options(${TARGET_NAME} PRIVATE ${ASAN_COMPILE_FLAGS})
        # 链接选项应用于目标 (只包含 sanitizer 核心标志)
        target_link_options(${TARGET_NAME} PRIVATE ${ASAN_LINK_FLAGS})

        message(STATUS "AddressSanitizer (ASan/LSan) 已为目标 '${TARGET_NAME}' 启用。")

 # 2.3. MSVC 编译器配置（Windows）
    elseif(CMAKE_CXX_COMPILER_ID STREQUAL "MSVC")
        message(STATUS "配置适用于 MSVC 编译器 (Windows)。")

        # 检查 MSVC 版本支持（ASan 要求 VS 2019 v16.4+）
        if(CMAKE_CXX_COMPILER_VERSION VERSION_LESS 19.24)
            message(WARNING "MSVC ASan 需要 Visual Studio 2019 (v16.4) 或更高版本。")
        endif()

        # MSVC 的 /fsanitize=address 需要在编译和链接时同时使用
        set(MSVC_ASAN_FLAGS "/fsanitize=address /Zi")

        # 应用标志
        target_compile_options(${TARGET_NAME} PRIVATE ${MSVC_ASAN_FLAGS})
        target_link_options(${TARGET_NAME} PRIVATE ${MSVC_ASAN_FLAGS})

        message(STATUS "MSVC AddressSanitizer (ASan) 已为目标 '${TARGET_NAME}' 启用。")

    # 2.4. 其他编译器处理
    else()
        message(WARNING "当前编译器 (${CMAKE_CXX_COMPILER_ID}) 不支持配置 Address Sanitizer (ASan)。ASan 已被禁用。")
    endif()
endif()
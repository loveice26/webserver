cmake_minimum_required(VERSION 4.1)
project(minimal_http_server)
set(CMAKE_CXX_STANDARD 20)

if (MSVC)
    add_compile_options("/utf-8")
endif()

find_package(Threads REQUIRED)

# 定义一个变量来存储所有的源文件
set(SERVER_SOURCES
    src/webserver2.cpp
    src/thread_pool.cpp
    # ... 每次添加新文件，都需要在这里手动添加 ...
)

add_executable(minimal_http_server
    ${SERVER_SOURCES}
)

target_include_directories(minimal_http_server PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
)

target_link_libraries(minimal_http_server PRIVATE
    Threads::Threads
)

# Windows 下需要链接 ws2_32
if (WIN32)
    # 给 minimal_http_server 这个目标添加链接库
    target_link_libraries(minimal_http_server PRIVATE ws2_32)
endif()

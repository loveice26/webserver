# 设置 cmkae 最低版本
cmake_minimum_required(VERSION 3.10)

if (MSVC)
    add_compile_options("/utf-8")
endif()

# 定义项目名为MinimalHttpServer, CXX 表示这是一个C++项目
project(MinimalHttpServer CXX)

# 设置为 C++11 标准
set(CMAKE_CXX_STANDARD 11)

# 定义一个可执行文件 minimal_http_server 源文件 src/webserver1.cpp
add_executable(minimal_http_server src/webserver1.cpp)

# Windows 下需要链接 ws2_32
if (WIN32)
    # 给 minimal_http_server 这个目标添加链接库
    target_link_libraries(minimal_http_server ws2_32)
endif()

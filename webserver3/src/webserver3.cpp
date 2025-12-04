#include <iostream>
#include <fstream>
#include <sstream>
#include <algorithm>
#include <cctype>

// --- 跨平台网络库头文件和类型定义 ---
#if _WIN32
    // Windows Sockets (Winsock) 头文件
    #include <winsock2.h>
    #include <ws2tcpip.h>
    // 链接 Winsock 库
    #pragma comment(lib, "ws2_32.lib")

    using SocketHandle = SOCKET;          // 在 Windows 上，套接字句柄是 SOCKET 类型
    #define CLOSE_SOCKET(s) closesocket(s) // 宏定义关闭套接字函数
    // 在 Windows 上，使用 ptrdiff_t 模拟 POSIX 的 ssize_t
    typedef ptrdiff_t ssize_t;
#else
    // POSIX (Linux/macOS) 网络头文件
    #include <netinet/in.h>
    #include <arpa/inet.h>
    #include <unistd.h>
    
    using SocketHandle = int;             // 在 POSIX 上，套接字句柄是 int 类型
    #define CLOSE_SOCKET(s) close(s)      // 宏定义关闭套接字函数
    #define INVALID_SOCKET -1             // POSIX 上无效套接字为 -1
    // POSIX 系统默认包含 ssize_t
#endif

// 包含自定义的头文件
#include "thread_pool.hpp"
#include "HttpRequestParser.hpp"

// --- 网络 I/O 封装 ---

// 封装 recv 函数
inline int NetRecv(SocketHandle s, char* buf, int len, int flags = 0) {
    return recv(s, buf, len, flags);
}

// 封装 send 函数
inline int NetSend(SocketHandle s, const char* buf, int len, int flags = 0) {
    return send(s, buf, len, flags);
}

// 保证 send 全量发送（处理 TCP 粘包/分块特性）
ssize_t send_all(SocketHandle s, const char* buf, size_t len) {
    size_t sent = 0; // 已发送的字节数
    while (sent < len) { // 循环直到所有数据发送完毕
        // 尝试发送剩余的数据
        int n = NetSend(s, buf + sent, (int)(len - sent), 0);
        
        // 检查错误或连接关闭
        if (n <= 0) return n; 
        
        sent += n; // 更新已发送的字节数
    }
    return (ssize_t)sent; // 返回总共发送的字节数
}

// 封装响应发送，使用 send_all 确保完整性
void send_response(SocketHandle client_socket, const std::string& msg) {
    send_all(client_socket, msg.c_str(), msg.size());
}

// --- 文件操作辅助函数 ---

// 读取文件内容到 std::string
std::string read_file(const std::string& path) {
    // std::ios::binary 确保文件按字节读取，避免跨平台换行符转换问题
    std::ifstream ifs(path, std::ios::binary); 
    if (!ifs) return ""; // 文件打开失败返回空字符串
    
    std::stringstream buffer;
    buffer << ifs.rdbuf(); // 将文件内容快速读入 stringstream
    return buffer.str();
}

// --- HTTP/CORS 辅助函数 ---

// 构建跨域资源共享 (CORS) 相关的响应头
std::string build_cors_headers() {
    return
        "Access-Control-Allow-Origin: *\r\n"      // 允许所有来源访问
        "Access-Control-Allow-Methods: GET, POST, OPTIONS\r\n" // 允许的 HTTP 方法
        "Access-Control-Allow-Headers: Content-Type\r\n";     // 允许的请求头
}

// --- 核心请求处理函数 ---

void handle_request(SocketHandle client_socket) {
    std::string request_data;
    const int chunk = 1024; // 每次接收数据的缓冲区大小

    // 状态标志
    bool header_received = false;
    int content_length = 0;
    int header_end_pos = -1;

    // 循环接收完整的 HTTP 请求数据
    while (true) {
        char buf[chunk];
        int n = NetRecv(client_socket, buf, chunk, 0);

        if (n <= 0) { // 接收错误或连接关闭
            break;
        }

        request_data.append(buf, buf + n); // 将接收到的数据追加到请求字符串

        // 仅在头部未完全接收时进行解析检查
        if (!header_received) {
            // HTTP 头部结束标志是两个连续的 CRLF (\r\n\r\n)
            size_t pos = request_data.find("\r\n\r\n");
            
            if (pos != std::string::npos) {
                header_received = true;
                header_end_pos = (int)pos;

                // 临时解析头部以获取 Content-Length
                HttpRequest temp_req;
                temp_req.parse(request_data);

                // 遍历头部，查找 Content-Length (key转小写处理)
                for (auto& kv : temp_req.headers) {
                    std::string key = kv.first;
                    std::transform(key.begin(), key.end(), key.begin(), ::tolower);
                    
                    if (key == "content-length") {
                        try {
                            content_length = std::stoi(kv.second);
                        } catch (...) {
                            // 错误处理：Content-Length 无效
                            content_length = 0; 
                        }

                        break; // 找到就退出循环
                    }
                }

                // 对于 POST/PUT 请求，如果 Content-Length 为 0，发送 411 错误
                if ((temp_req.method == "POST" || temp_req.method == "PUT") && content_length == 0) {
                    send_response(client_socket,
                        "HTTP/1.1 411 Length Required\r\n"
                        + build_cors_headers() +
                        "Content-Length: 0\r\n\r\n");
                    CLOSE_SOCKET(client_socket);
                    return;
                }
            }
        }

        // 头部接收完毕后，检查请求体是否完全接收
        if (header_received) {
            int body_start = header_end_pos + 4; // 4 是 "\r\n\r\n" 的长度
            // 检查当前接收到的总数据长度是否大于或等于头部 + 内容长度
            if ((int)request_data.size() >= body_start + content_length) {
                break; // 请求体已完整接收，跳出循环
            }
        }
    }

    // --- 请求解析和初步错误检查 ---

    if (request_data.empty()) { // 接收到空请求
        CLOSE_SOCKET(client_socket);
        return;
    }

    HttpRequest req;
    if (!req.parse(request_data)) { // 再次完整解析请求，如果失败
        send_response(client_socket,
            "HTTP/1.1 400 Bad Request\r\n"
            "Content-Length: 0\r\n\r\n");
        CLOSE_SOCKET(client_socket);
        return;
    }

    req.print(); // 打印请求信息 (假设 HttpRequest 有 print 方法)

    // --- 路由和方法处理 ---

    // ----------- GET 请求 -----------
    if (req.method == "GET") {
        std::string path = req.path;
        if (path == "/") path = "/index.html"; // 默认路由到 index.html

        // 尝试从 'html' 目录下读取文件
        std::string file = read_file("html" + path);

        if (file.empty()) {
            // 文件不存在，返回 404 Not Found
            send_response(client_socket,
                "HTTP/1.1 404 Not Found\r\n"
                + build_cors_headers() +
                "Content-Type: text/plain\r\n"
                "Content-Length: 9\r\n\r\n"
                "Not Found");
        } else {
            // 文件找到，返回 200 OK
            std::stringstream ss;
            ss << "HTTP/1.1 200 OK\r\n"
               << build_cors_headers()
               << "Content-Type: text/html\r\n"
               << "Content-Length: " << file.size() << "\r\n\r\n"
               << file;

            send_response(client_socket, ss.str());
        }
    }

    // ----------- POST 请求 -----------
    else if (req.method == "POST") {
        // 简单地将请求体 (req.body) 作为响应返回
        std::stringstream ss;
        ss << "HTTP/1.1 200 OK\r\n"
           << build_cors_headers()
           << "Content-Type: text/plain\r\n"
           << "Content-Length: " << req.body.size() << "\r\n\r\n"
           << req.body;

        send_response(client_socket, ss.str());
    }

    // ----------- OPTIONS（CORS 预检）请求 -----------
    else if (req.method == "OPTIONS") {
        // 返回 204 No Content，仅包含 CORS 头部
        send_response(client_socket,
            "HTTP/1.1 204 No Content\r\n"
            + build_cors_headers() +
            "Content-Length: 0\r\n\r\n");
    }

    // ----------- 其他方法 -----------
    else {
        // 返回 405 Method Not Allowed
        send_response(client_socket,
            "HTTP/1.1 405 Method Not Allowed\r\n"
            + build_cors_headers() +
            "Content-Length: 0\r\n\r\n");
    }

    // 关闭客户端套接字
    CLOSE_SOCKET(client_socket);
}

// --- 主函数：服务器启动和监听 ---

int main() {
    SocketHandle server_fd; // 服务器套接字文件描述符/句柄

    // --- Windows Winsock 初始化 ---
#if _WIN32
    WSADATA wsa;
    // 启动 Winsock 2.2 版本
    if (WSAStartup(MAKEWORD(2,2), &wsa) != 0) {
        std::cerr << "WSAStartup failed.\n";
        return 1;
    }
#endif

    // --- 创建套接字 (socket) ---
    server_fd = socket(AF_INET, SOCK_STREAM, 0); // IPv4, TCP, 默认协议
    
#ifdef _WIN32
    if (server_fd == INVALID_SOCKET) {
        std::cerr << "socket failed: " << WSAGetLastError() << "\n";
        return 1;
    }
#else
    if (server_fd < 0) {
        perror("socket failed"); // POSIX 下使用 perror 打印系统错误
        return 1;
    }
#endif

    // --- 设置套接字选项 (setsockopt) ---
    int opt = 1;
    // 设置 SO_REUSEADDR 选项，允许端口快速重用
#ifdef _WIN32
    setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, (char*)&opt, sizeof(opt));
#else
    setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
    // POSIX/Linux 上可能支持 SO_REUSEPORT
#ifdef SO_REUSEPORT
    setsockopt(server_fd, SOL_SOCKET, SO_REUSEPORT, &opt, sizeof(opt));
#endif
#endif

    // --- 绑定 (bind) ---
    sockaddr_in addr {}; // sockaddr_in 结构体用于存储地址信息
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY; // 监听所有可用网络接口 (0.0.0.0)
    addr.sin_port = htons(8080);       // 端口号 8080，htons 转换为网络字节序

#ifdef _WIN32
    if (bind(server_fd, (sockaddr*)&addr, sizeof(addr)) == SOCKET_ERROR) {
        std::cerr << "bind failed: " << WSAGetLastError() << "\n";
        CLOSE_SOCKET(server_fd);
        WSACleanup();
        return 1;
    }
#else
    if (bind(server_fd, (sockaddr*)&addr, sizeof(addr)) < 0) {
        perror("bind failed");
        CLOSE_SOCKET(server_fd);
        return 1;
    }
#endif

    // --- 监听 (listen) ---
    if (listen(server_fd, 10) < 0) { // 设置最大等待连接队列长度为 10
        perror("listen failed");
        CLOSE_SOCKET(server_fd);
        return 1;
    }

    // --- 线程池初始化 ---
    ThreadPool pool(4); // 创建一个包含 4 个工作线程的线程池

    std::cout << "Server running on port 8080\n";

    // --- 接收连接循环 (accept) ---
    while (true) {
        sockaddr_in client;
        
#if _WIN32
        int len = sizeof(client);
        // 接受客户端连接
        SocketHandle s = accept(server_fd, (sockaddr*)&client, &len);
        if (s == INVALID_SOCKET) continue; // 接受失败，继续循环
#else
        socklen_t len = sizeof(client);
        SocketHandle s = accept(server_fd, (sockaddr*)&client, &len);
        if (s < 0) continue;
#endif

        // 将请求处理任务提交给线程池异步执行
        pool.enqueue([s]() {
            handle_request(s);
        });
    }

    // --- 资源清理 (理论上不可达，因为主循环是无限的) ---
    CLOSE_SOCKET(server_fd);
#if _WIN32
    WSACleanup(); // 释放 Winsock 资源
#endif
    return 0;
}
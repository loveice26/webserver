/*  目标：支持多个客户端并发访问。
*   知识点：
*       1. std::thread 创建线程。
*       2. 简单线程池
*       3. 线程安全（std::mutex / std::lock_guard）。
*   实现：
*       1. 每个 accept 后开线程处理客户端。
*       2. 加入线程池，多个请求共享线程池。
*   实现一个简单路由：
*       1. / 返回首页
*       2. /time 返回当前时间
*/

#include <iostream>
#include <sstream>
#include <chrono>
#include <iomanip>

#ifdef _WIN32
    #include <winsock2.h>
    #include <ws2tcpip.h>
    #pragma comment(lib, "ws2_32.lib")

    #define CLOSE_SOCKET(s) closesocket(s)
    #define SocketRecv(s) closesocket(s)
    using SocketHandle = SOCKET;
    using ssize_t = int;    // windows 中没有 ssize_t 定义别名
#else
    #include <netinet/in.h>
    #include <arpa/inet.h>
    #include <unistd.h>
    #define INVALID_SOCKET -1
    #define SOCKET_ERROR -1

    #define CLOSE_SOCKET(s) close(s)
    using SocketHandle = int;
#endif

#include "thread_pool.hpp"

/**
 * @brief 从套接字接收数据（跨平台封装）。
 *
 * 此函数作为 POSIX/Windows 标准 recv() 函数的统一封装层。
 * 它从指定的套接字句柄接收数据，并将数据存入缓冲区。
 *
 * @param s 用于接收数据的套接字句柄（SocketHandle 在不同平台下可能是 int 或 SOCKET）。
 * @param buf 指向接收数据的缓冲区。
 * @param len 缓冲区的大小（或最大接收字节数）。
 * @param flags 接收操作的控制标志，例如 MSG_PEEK。默认为 0。
 * @return int 成功接收的字节数；如果连接关闭，返回 0；如果发生错误，返回 -1 (POSIX) 或 SOCKET_ERROR (Windows，需检查 WSAGetLastError)。
 * @note 在 Windows 平台上，此函数实际调用 closesocket()，在 POSIX 平台上，实际调用 close()。
 */
inline int NetRecv(SocketHandle s, char* buf, int len, int flags = 0) {
    // 调用底层的 recv() 系统函数来执行实际的数据接收操作。
    // 在 POSIX 系统上，s 是 int 类型的文件描述符；在 Windows 上，s 是 SOCKET 类型。
    return recv(s, buf, len, flags);
}

/**
 * @brief 通过套接字发送数据（跨平台封装）。
 *
 * 此函数作为 POSIX/Windows 标准 send() 函数的统一封装层。
 * 它通过指定的套接字句柄发送缓冲区中的数据。
 *
 * @param s 用于发送数据的套接字句柄（SocketHandle 在不同平台下可能是 int 或 SOCKET）。
 * @param buf 指向包含要发送数据的缓冲区。
 * @param len 待发送数据的字节数。
 * @param flags 发送操作的控制标志，例如 MSG_NOSIGNAL。默认为 0。
 * @return int 成功发送的字节数；如果发生错误，返回 -1 (POSIX) 或 SOCKET_ERROR (Windows，需检查 WSAGetLastError)。
 * @note 这是一个阻塞或非阻塞调用，取决于套接字 s 的配置。
 */
inline int NetSend(SocketHandle s, const char* buf, int len, int flags = 0) {
    // 调用底层的 send() 系统函数来执行实际的数据发送操作。
    // 确保使用 const char* 类型的缓冲区，因为数据是只读的。
    return send(s, buf, len, flags);
}

// ======================== 路由处理 ========================
/**
 * @brief 构建并发送HTTP响应给客户端。
 *
 * 此函数根据提供的状态码、内容类型和响应体，
 * 构造一个完整的HTTP/1.1响应报文，并通过给定的套接字发送给客户端。
 * 响应报文包含必要的HTTP头，如状态行、Content-Type、Content-Length和Connection: close。
 *
 * @param client_socket 客户端套接字的文件描述符。
 * @param status HTTP状态码和对应的短语，例如 "200 OK"。
 * @param content_type 响应体的内容类型，例如 "text/html" 或 "application/json"。
 * @param body 响应的主体内容。
 * * @note 响应中默认包含 "Connection: close" 头部，表示服务器在发送完响应后会关闭连接。
 */
void send_response(int client_socket, const std::string& status, const std::string& content_type, const std::string& body) {
    std::stringstream ss;
    ss << "HTTP/1.1 " << status << "\r\n";
    ss << "Content-Type: " << content_type << "\r\n";
    ss << "Content-Length: " << body.length() << "\r\n";
    ss << "Connection: close\r\n"; // 每次请求后关闭连接
    ss << "\r\n";
    ss << body;

    std::string response = ss.str();
    NetSend(client_socket, response.c_str(), response.length(), 0);
}

/**
 * @brief 获取当前系统时间并格式化为字符串。
 *
 * 此函数获取当前的系统时间点，将其转换为 `time_t` 类型，
 * 然后格式化成 "YYYY-MM-DD HH:MM:SS" 形式的本地时间字符串。
 *
 * @return std::string 格式化后的当前时间字符串，例如 "2023-10-27 15:30:00"。
 */
std::string get_current_time_str() {
    // 1. 获取当前时间点 (time_point)
    //    使用 high_resolution_clock 可能会更精确，但 system_clock 更适合表示日历时间。
    auto now = std::chrono::system_clock::now();

    // 2. 将时间点转换为 C 风格的 time_t
    //    time_t 是一个整数类型，通常用于存储自 Epoch (1970-01-01 00:00:00 UTC) 以来秒数。
    time_t now_c = std::chrono::system_clock::to_time_t(now);

    // 3. 准备一个字符串流 (stringstream) 用于构建最终的字符串
    std::stringstream ss;

    // 4. 格式化时间并插入到字符串流
    //    - localtime(&now_c) 将 time_t 转换为本地时间的 tm 结构体指针。
    //    - std::put_time(...) 使用指定的格式 "%Y-%m-%d %H:%M:%S" 进行格式化。
    ss << std::put_time(std::localtime(&now_c), "%Y-%m-%d %H:%M:%S");

    // 5. 从字符串流中提取并返回格式化后的时间字符串
    return ss.str();
}


/**
 * @brief 处理单个客户端的HTTP请求。
 *
 * 此函数从给定的客户端套接字接收数据，解析HTTP请求行以提取方法和路径。
 * 它根据请求的路径 (如 / 或 /time) 决定要发送的响应内容，并调用 send_response。
 * 最后，它会关闭与客户端的连接。
 *
 * @param client_socket 客户端套接字的文件描述符。
 *
 * @note 这是一个简化的实现，假设请求体很小，并且可以一次性读取所有请求数据。
 */
void handle_request(SocketHandle client_socket) {
    // 1. 声明并初始化一个固定大小的缓冲区 (1024 字节)
    //    用于存储从客户端套接字读取的原始数据（即HTTP请求）。
    char buffer[1024] = {0};

    // 2. 从客户端套接字接收数据
    //    valread 存储实际读取的字节数。
    //    简化：假设一次就能读完请求行（实际生产环境中需要循环读取）。
    ssize_t valread = NetRecv(client_socket, buffer, 1024, 0);

    // 3. 检查是否成功读取到数据
    if (valread > 0) {
        // 4. 将读取到的字节转换为 C++ 字符串 (std::string)
        //    使用 valread 确保只包含实际读取到的数据，不包含缓冲区其余的零。
        std::string request_str(buffer, valread);

        // 5. 使用字符串流 (stringstream) 简化请求解析
        //    将请求字符串放入流中，方便提取其中的单词。
        std::stringstream request_ss(request_str);

        // 6. 声明变量存储请求行的三个关键部分
        std::string method, path, http_version;

        // 7. 从流中提取请求行的三个部分（方法、路径、HTTP版本）
        //    例如，对于 "GET / HTTP/1.1"，它们会被依次提取。
        request_ss >> method >> path >> http_version;

        // --- 请求路由和处理 ---

        // 8. 检查请求方法是否是 GET
        if (method == "GET") {
            // 9. 检查请求路径是否是根路径 "/"
            if (path == "/") {
                // 10. 构造首页响应体
                std::string body = "<h1>Welcome to Simple Server!</h1><p>Try visiting /time for the current time.</p>";
                // 11. 发送 "200 OK" 响应
                send_response(client_socket, "200 OK", "text/html", body);
            }
            // 12. 检查请求路径是否是 "/time"
            else if (path == "/time") {
                // 13. 调用辅助函数获取当前时间字符串
                std::string current_time = get_current_time_str();
                // 14. 构造时间页面的响应体
                std::string body = "<h2>Current Time</h2><p>" + current_time + "</p>";
                // 15. 发送 "200 OK" 响应
                send_response(client_socket, "200 OK", "text/html", body);
            }
            // 16. 处理所有其他未匹配的 GET 路径
            else {
                // 17. 构造 "404 Not Found" 响应体
                std::string body = "<h1>404 Not Found</h1><p>The requested resource " + path + " was not found.</p>";
                // 18. 发送 "404 Not Found" 响应
                send_response(client_socket, "404 Not Found", "text/html", body);
            }
        }
        // 19. 处理非 GET 方法（例如 POST, HEAD 等）
        else {
            // 20. 构造 "501 Not Implemented" 响应体
            std::string body = "<h1>501 Not Implemented</h1><p>Only GET method is supported.</p>";
            // 21. 发送 "501 Not Implemented" 响应
            send_response(client_socket, "501 Not Implemented", "text/html", body);
        }
    }

    // 22. 关闭客户端 Socket
    //     无论请求是否成功处理，为了释放资源和遵循 HTTP/1.1 'Connection: close' 约定，都需要关闭连接。
    CLOSE_SOCKET(client_socket);
}

/**
 * @brief HTTP服务器主入口函数。
 *
 * 此函数负责初始化网络环境、创建服务器套接字、绑定、监听端口，
 * 并在一个无限循环中接受传入的客户端连接。它使用线程池来异步处理每个请求。
 *
 * @param argc 命令行参数计数。
 * @param argv 命令行参数数组。
 * @return int 0 表示成功退出，非 0 表示失败。
 */
int main(int argc, char const *argv[]) {
    // 声明用于服务器和新连接的套接字句柄，使用跨平台类型 SocketHandle
    SocketHandle server_fd, new_socket;

    // sockaddr_in 是用于 IPv4 地址族的结构体
    struct sockaddr_in address;
    // 存储地址结构体的大小
#ifdef _WIN32
    // Windows 环境：addrlen 是 int 类型 (Winsock API 需要)
    int addrlen = sizeof(address);
#else
    // POSIX 环境 (Linux/macOS)：addrlen 必须是 socklen_t 类型 (POSIX API 需要)
    socklen_t addrlen = sizeof(address);
#endif

    // 服务器配置参数
    int port = 8080;
    int thread_count = 4; // 线程池大小

    // --- 跨平台初始化 (仅 Windows) ---
#ifdef _WIN32
    WSADATA wsaData;
    // 在 Windows 上，必须先初始化 Winsock 库
    if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0) {
        std::cerr << "WSAStartup failed.\n";
        return EXIT_FAILURE;
    }
#endif

    // 1. 创建 Socket 文件描述符
    // AF_INET: IPv4 协议族, SOCK_STREAM: TCP 流式套接字, 0: 默认协议
    if ((server_fd = socket(AF_INET, SOCK_STREAM, 0)) == (SocketHandle)-1 || server_fd == 0) {
        // 在 Windows 上 socket 失败返回 -1 (INVALID_SOCKET), 转换为 SocketHandle 会是 (SocketHandle)-1
        // 在 POSIX 上 socket 失败返回 -1
#ifdef _WIN32
        std::cerr << "socket failed with error: " << WSAGetLastError() << std::endl;
#else
        perror("socket failed");
#endif
        return EXIT_FAILURE;
    }

    // 设置 Socket 选项，允许重用地址
    // 允许重用地址 SO_REUSEADDR (和 SO_REUSEPORT)，避免程序重启时出现 "Address already in use" 错误
    int opt = 1;
    if (setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR
#ifndef _WIN32
        // Windows 不支持 SO_REUSEPORT，在 POSIX 系统上才添加
        | SO_REUSEPORT
#endif
        , (const char*)&opt, sizeof(opt))) {
#ifdef _WIN32
        std::cerr << "setsockopt failed with error: " << WSAGetLastError() << std::endl;
#else
        perror("setsockopt");
#endif
        // 发生错误，关闭套接字并退出
        CLOSE_SOCKET(server_fd);
        return EXIT_FAILURE;
    }

    // 配置服务器地址结构
    address.sin_family = AF_INET;
    // INADDR_ANY: 监听本机所有可用的网络接口
    address.sin_addr.s_addr = INADDR_ANY;
    // htons(): 将主机字节序的端口号转换为网络字节序
    address.sin_port = htons(port);

    // 2. 绑定 Socket 到端口
    // 将服务器套接字与特定的 IP 地址和端口号关联起来
    if (bind(server_fd, (struct sockaddr *)&address, sizeof(address)) < 0) {
#ifdef _WIN32
        std::cerr << "bind failed with error: " << WSAGetLastError() << std::endl;
#else
        perror("bind failed");
#endif
        CLOSE_SOCKET(server_fd);
        return EXIT_FAILURE;
    }

    // 3. 监听端口
    // 将套接字设置为被动模式，准备接受连接，10 是最大等待连接队列长度
    if (listen(server_fd, 10) < 0) {
#ifdef _WIN32
        std::cerr << "listen failed with error: " << WSAGetLastError() << std::endl;
#else
        perror("listen");
#endif
        CLOSE_SOCKET(server_fd);
        return EXIT_FAILURE;
    }

    std::cout << "Server listening on port " << port << " with " << thread_count << " worker threads." << std::endl;

    // 创建线程池 (假设 ThreadPool 类已正确实现)
    ThreadPool pool(thread_count);

    // 4. 循环接受连接 (主服务器循环)
    while (true) {
        std::cout << "Waiting for a connection..." << std::endl;

#ifdef _WIN32
        // 接受连接
        // 这是一个阻塞调用，直到有客户端连接进来
        // 注意：addrlen 必须是 (socklen_t*) 或 (int*)，这里使用 (int*) 兼容 Winsock 和 POSIX
        if ((new_socket = accept(server_fd, (struct sockaddr *)&address, (int*)&addrlen)) < 0) {
            // 检查接受失败的原因，并在 Windows 上进行特殊处理
            if (WSAGetLastError() == WSAEINTR) { // 被中断，重新尝试
                continue;
            }
            std::cerr << "accept failed with error: " << WSAGetLastError() << std::endl;
#else
        if ((new_socket = accept(server_fd, (struct sockaddr *)&address, (socklen_t*)&addrlen)) < 0) {
            perror("accept");
#endif
            continue; // 接受失败，继续等待下一个连接
        }

        // 打印客户端信息 (这部分需要 arpa/inet.h，因此在 POSIX 环境下可以工作)
        // 在 Windows 上，需要使用 WSAAddressToString 或 GetNameInfo
#ifndef _WIN32
        char client_ip[INET_ADDRSTRLEN];
        inet_ntop(AF_INET, &address.sin_addr, client_ip, INET_ADDRSTRLEN);
        std::cout << "Connection accepted from " << client_ip << ":" << ntohs(address.sin_port) << std::endl;
#endif

        // 5. 将处理任务提交给线程池
        pool.enqueue(handle_request, new_socket);
    }

    // 6. 清理工作

    // 关闭服务器 Socket (实际上主循环是无限的，但为了完整性)
    CLOSE_SOCKET(server_fd);

    // --- 跨平台清理 (仅 Windows) ---
#ifdef _WIN32
    // 在 Windows 上，需要清理 Winsock 库
    WSACleanup();
#endif

    return 0;
}

/*  目标：能解析 HTTP 请求头，支持 GET/POST。
*   知识点：
*       1. 字符串解析：find, substr, stringstream。
*       2. HTTP 请求格式：请求行、请求头、请求体。
*       3. 响应格式：状态码、头部、Body。
*   实现：
*       1. 解析 GET /index.html HTTP/1.1。
*       2. 支持静态文件访问（返回本地 html/ 文件内容）。
*       3. 解析 POST 数据并打印。
*/

#include <iostream>
#include <fstream>
#include <sstream>

#if _WIN32
    #include <winsock2.h>
    #include <ws2tcpip.h>
    #pragma comment(lib, "ws2_32.lib")

    using SocketHandle = SOCKET;
    #define CLOSE_SOCKET(s) closesocket(s)
    using ssize_t = int;    // windows 中没有 ssize_t 定义别名
#else
    #include <netinet/in.h>
    #include <arpa/inet.h>
    #include <unistd.h>

    using SocketHandle = int;
    #define CLOSE_SOCKET(s) close(s)
    #define INVALID_SOCKET -1
    #define SOCKET_ERROR -1
#endif

#include "thread_pool.hpp"
#include "HttpRequestParser.hpp"


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
void send_response(int client_socket, const std::string& msg) {
    NetSend(client_socket, msg.c_str(), msg.length(), 0);
}

// 读取文件
std::string read_file(const std::string& path) {
    std::ifstream ifs(path, std::ios::binary);
    if (!ifs) return "";
    std::stringstream buffer;
    buffer << ifs.rdbuf();
    return buffer.str();
}

void handle_request(SocketHandle client_socket) {

    std::string request_data;
    ssize_t valread = 0;
    int chunk_size = 1024;

    // 跟踪是否已找到头部结束标志
    bool header_received = false;
    // 存储 Content-Length 的值
    int content_length = 0;

    // 循环读取数据
    while (true) {
        // 1. 预先调整 string 的大小，留出 CHUNK_SIZE 的空间用于接收新数据
        // 我们将新数据追加到 string 的末尾
        size_t current_size = request_data.size();
        request_data.resize(current_size + chunk_size);
        
        // 2. 接收数据，写入到 string 缓冲区的新增部分
        // &request_data[current_size] 获取到新增部分的起始指针
        valread = NetRecv(
            client_socket, 
            &request_data[current_size], // 写入到 string 缓冲区的末尾
            chunk_size, 
            0
        );

        if (valread <= 0) {
            // 客户端关闭连接 (0) 或发生错误 (<0)
            request_data.resize(current_size); // 放弃未使用的空间
            if (valread == 0) {
                std::cout << "Client closed connection." << std::endl;
            } else {
                std::cerr << "NetRecv failed or interrupted." << std::endl;
            }
            break; 
        }
        
        request_data.resize(current_size + valread);

        // 1. 检查头部是否结束
        if (!header_received) {
            size_t header_end_pos = request_data.find("\r\n\r\n");
            
            if (header_end_pos != std::string::npos) {
                header_received = true;
                
                // 找到头部后，立即解析头部以获取 Content-Length
                // 注意：这里需要一个轻量级解析函数来快速获取 Content-Length
                // 简化做法：先将当前数据传入完整的解析器，然后获取 Content-Length
                HttpRequest temp_req;
                temp_req.parse(request_data);
                
                if (temp_req.method == "POST" || temp_req.method == "PUT") {
                    auto it = temp_req.headers.find("content-length");
                    if (it != temp_req.headers.end()) {
                        try {
                            content_length = std::stoi(it->second);
                        } catch (...) {
                            // Content-Length 格式错误，可以返回 400 错误
                            break; 
                        }
                    }
                }
            }
        }

        // 2. 检查是否达到终止条件
        if (header_received) {
            // 计算请求体开始的位置 (头部结束标志后的 4 个字符)
            size_t body_start = request_data.find("\r\n\r\n") + 4;
            
            // 理论上应该接收的总长度
            size_t expected_total_size = body_start + content_length;
            
            // 如果已接收的长度 >= 理论总长度，则停止
            if (request_data.size() >= expected_total_size) {
                break; 
            }
            
            // 如果是 GET/HEAD 请求（content_length=0），并且头部已收到，也停止
            if (content_length == 0) {
                break;
            }
        }
    }

    // 3. 检查是否成功读取到数据
    if (valread > 0) {

        // 5. 调用 HttpRequest 解析
        HttpRequest req;
        req.parse(request_data);
        req.print();    // 打印解析数据

        if(req.method == "GET"){
            std::string file_path = "html" + req.path;
            if (req.path == "/") file_path = "html/index.html";

            std::string content = read_file(file_path);

            if (content.empty()) {
                send_response(client_socket,
                    "HTTP/1.1 404 Not Found\r\n"
                    "Content-Type: text/plain\r\n"
                    "Content-Length: 9\r\n\r\n"
                    "Not Found");
            } else {
                std::stringstream resp;
                resp << "HTTP/1.1 200 OK\r\n"
                     << "Content-Type: text/html\r\n"
                     << "Content-Length: " << content.size() << "\r\n\r\n"
                     << content;
                send_response(client_socket, resp.str());
            }
        }else if(req.method == "POST") {
            std::stringstream resp;
            resp << "HTTP/1.1 200 OK\r\n"
                 << "Content-Type: text/plain\r\n"
                 << "Content-Length: " << req.body.size() << "\r\n\r\n"
                 << req.body;

            send_response(client_socket, resp.str());
        }
    }
}


int main(){

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
        // 返回 0 也失败是因为在 linux 下 文件描述符 0 是留给标准输入的如果返回 0 就会导致覆盖原有的标准输入导致程序复杂或出错
#ifdef _WIN32
        std::cerr << "socket failed with error: " << WSAGetLastError() << std::endl;
#else
        perror("socket failed");
#endif
        return EXIT_FAILURE;
    }


    // 设置 Socket 选项，允许重用地址
    // 允许重用地址 SO_REUSEADDR (和 SO_REUSEPORT)，避免程序重启时出现 "Address already in use" 错误

    // 这段代码的作用是设置服务器套接字的选项，特别是开启 地址重用 (SO_REUSEADDR) 和 端口重用 (SO_REUSEPORT)
    // 当服务器程序意外终止或正常关闭时，它使用的 (IP地址, 端口号) 对并不会立即从网络系统中释放。
    // 它会进入一个名为 TIME_WAIT 的状态，这个状态通常会持续 2 MSL (Maximum Segment Lifetime，大约 1-4 分钟)。
    //  没有 SO_REUSEADDR： 如果您在 TIME_WAIT 状态未结束前尝试重启服务器程序并再次调用 bind()，系统会拒绝，返回 "Address already in use" 错误
    //  启用 SO_REUSEADDR： 允许您在 TIME_WAIT 状态下立即将同一个地址和端口重新绑定到新的 socket 上，从而实现快速重启服务，极大提高了开发的便利性和生产环境的容错性。

    // 在高性能服务器设计中，为了利用多核 CPU 的能力和避免单个进程成为瓶颈，有时需要运行多个独立的服务器实例来监听同一个端口。
    //  没有 SO_REUSEPORT： 传统的网络编程只允许一个 socket 绑定到特定的端口。
    //  启用 SO_REUSEPORT： 允许多个进程/线程绑定到同一个端口，操作系统内核会负责将进入的连接 (TCP) 或数据包 (UDP) 在这些绑定了相同端口的 socket 之间负载均衡地分配，这对于构建大规模、高并发的服务器集群至关重要。
    // 我们这里使用 SO_REUSEPORT 主要是为了如下
    //  优化唤醒： 内核可以在多个线程之间实现更智能、更高效的连接分配，只唤醒一个最合适的线程来处理新连接，从而避免惊群。
    //  改进负载均衡： 内核可以确保连接被均匀地分配给所有等待的线程，实现更好的负载均衡。
    // 惊群是指 当一个新连接到达时，所有等待在 accept() 调用上的线程都会被操作系统唤醒, 调用 accept()
    //  只有一个线程能成功接受连接，其他线程发现连接已经被取走，会立即返回继续等待 accept()。
    //  这种不必要的唤醒和竞争会消耗 CPU 资源（如上下文切换、锁竞争），在高并发场景下会降低整体性能。
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


    int port = 8080;        // 端口
    int thread_count = 4;   // 线程池大小

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

    // 创建线程池
    ThreadPool pool(thread_count);

    while(true) {
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
#ifdef _WIN32
        char host_name[NI_MAXHOST];      // IP 地址字符串
        char service_name[NI_MAXSERV];  // 端口号字符串

        // 使用 getnameinfo 获取主机名和服务名（端口号）
        if (getnameinfo(
            (LPSOCKADDR)&address,
            addrlen,
            host_name,
            NI_MAXHOST,
            service_name,
            NI_MAXSERV,
            NI_NUMERICHOST | NI_NUMERICSERV // 标志：只返回数字形式的IP和端口
        ) != 0) {
            std::cerr << "getnameinfo failed with error: " << WSAGetLastError() << std::endl;
            return EXIT_FAILURE;
        }

        // 打印客户端信息
        // host_name 包含了 IP，service_name 包含了端口号
        std::cout << "Connection accepted from " << host_name << ":" << service_name << std::endl;
#else
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
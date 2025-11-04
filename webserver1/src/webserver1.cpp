/*  目标：单线程版，能返回固定网页。
*   知识点：
*       1. Socket 编程：socket → bind → listen → accept → recv/send。
*       2. HTTP 协议最小格式（响应头 + 内容）。
*   实现：
*       1. 启动服务端监听 127.0.0.1:8080。
*       2. 客户端（浏览器）访问时，返回固定 HTML。
            HTTP/1.1 200 OK
            Content-Type: text/html

            <h1>Hello WebServer</h1>
*/

#include <iostream>
#include <sstream>
#include <string>
#include <fstream>
#include <cstring>

#ifdef _WIN32
    #include <WinSock2.h>
    #include <WS2tcpip.h>
    #pragma comment(lib, "Ws2_32.lib")
#else
    #include <unistd.h>
    #include <sys/types.h>
    #include <sys/socket.h>
    #include <netinet/in.h>
    #include <arpa/inet.h>
#endif

/**
 * @brief 读取HTML文件到字符串
 * 
 * @param filename: 传入文件路径
 * @return std::string 返回从文件中读取的字符, 读取失败返回空
 */
std::string read_file(const std::string& filename){
    // 读取文件用二进制模式
    std::ifstream file(filename, std::ios::in | std::ios::binary);

    // 如果读取失败返回空
    if (!file) {
        std::cerr << "Failed to open file: " << filename << std::endl;
        return "";
    }

    // 这里使用 ostringstream ,
    // 是因为直接使用 string 读取文件的时候需要按行或者按块读取循环读取并拼接,
    // 但是 ostringstream 不用可以直接得到拼接好的 string 类型字符
    std::ostringstream ss;

    // rdbuf 是直接访问流的缓冲区, 这里的意义就是直接访问文件流的缓冲区
    ss << file.rdbuf();

    return ss.str();
}


int main()
{
    const char* listen_ip = "127.0.0.1";
    const int listen_port = 8080;

    // 读取 index.html 文件
    std::string body = read_file("index.html");
    if(body.empty()){
        // 如果读取文件失败返回错误码
        body = "<h1>404 Not Found</h1>";
    }

    // 这是 HTTP 响应
     std::string response =
        "HTTP/1.1 200 OK\r\n"   // 状态行表示请求成功
        "Content-Type: text/html; charset=utf-8\r\n";   // 告诉浏览器内容类型为 HTML
    response += "Content-Length: " + std::to_string(body.size()) + "\r\n";  // 指定内容长度
    response += "Connection: close\r\n";    // 告诉浏览器服务器处理完后关闭连接
    response += "\r\n"; // 分隔头和正文
    response += body;   // HTML页面内容

#ifdef _WIN32
    WSADATA wsaData;
    // 在 windwos 下调用 WSAStartup 初始化网络功能
    // 参数1: 请求的 Winsock 版本, 这里的 MAKEWORD(2, 2) 就是请求 Winsock 2.2
    // 参数2: 指向 WSADATA 结构体初始化网络成功后会填充整个结构体
    // 返回值 0 成功, 错误返回非0错误码 用 WSAGetLastError() 获取
    if(WSAStartup(MAKEWORD(2, 2), &wsaData) != 0) {
        std::cerr << "WSAStartup failed\n";
        return 1;
    }

    int listen_fd;
    // 创建一个用于监听的 TCP 套接字, 并将句柄返回到 listen_fd 中
    // 参数1: AF_INET 指使用IPV4
    // 参数2: SOCK_STREAM 使用 TCP 数据流
    // 参数3: 使用 TCP 协议
    // 返回值: 套接字句柄
    listen_fd = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if(listen_fd == INVALID_SOCKET) {
        // 套接字创建失败
        std::cerr << "socket failed\n";
        WSACleanup();   // 关闭网络初始化功能
        return 1;
    }

    char opt = 1;
    // 参数1: 要操作的 socket 句柄
    // 参数2: 通用套接字
    // 参数3: 允许地址重复使用(默认情况下，TCP socket 关闭后，端口会进入 TIME_WAIT 状态（一般持续 1–4 分钟）。
        // 如果不设置这个选项，那么在 TIME_WAIT 期间再绑定同一个端口会失败，报错)
    // 参数4: 传入指针参数
    // 参数5: 参数4的大小
    // setsockopt 函数设置套接字选项
    setsockopt(listen_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    sockaddr_in addr;
    memset(&addr, 0, sizeof(addr)); // 初始化结构体为空
    addr.sin_family = AF_INET;  // 设置协议版本为 IPv4
    addr.sin_port = htons(listen_port); // 设置监听端口
    // 参数1: 地址协议
    // 参数2: 指向要转换的IP字符串地址
    // 参数3: 返回指向转换为二进制的IP地址
    // InetPton 函数将其标准文本表示形式中的 IPv4 或 IPv6 Internet 网络地址转换为数字二进制形式
    inet_pton(AF_INET, listen_ip, &addr.sin_addr);

    // 绑定本地IP和端口号
    if (bind(listen_fd, (sockaddr*)&addr, sizeof(addr)) == SOCKET_ERROR) {
        // 绑定失败
        std::cerr << "bind failed\n";
        closesocket(listen_fd);
        WSACleanup();
        return 1;
    }

    // 监听对应端口的请求
    // 参数1: 服务器 socket 服务器的端口和ip
    // 参数2: 最大连接次数 SOMAXCONN 代表内核允许的监听列队长度上限
    if (listen(listen_fd, SOMAXCONN) == SOCKET_ERROR) {
        std::cerr << "listen failed\n";
        closesocket(listen_fd);
        WSACleanup();
        return 1;
    }

    // 打印监听的 IP 和 端口
    std::cout << "Listening on " << listen_ip << ":" << listen_port << " ...\n";

    while (true) {
        sockaddr_in client_addr;
        int client_len = sizeof(client_addr);

        // 参数1: 服务器监听 socket
        // 参数2: 输出参数，用来保存客户端的地址(IP/端口)
        // 参数3: 地址结构体大小(传入/传出)
        // 阻塞等待客户端连接 返回值:成功返回一个新的客户端 socket
        SOCKET client_fd = accept(listen_fd, (sockaddr*)&client_addr, &client_len);
        if (client_fd == INVALID_SOCKET) { continue; }

        char buf[4096];
        // 参数1: 套接字
        // 参数2: 接收缓冲区
        // 参数3: 缓冲区大小
        // 参数4: 控制接收行为标志 0 → 默认（阻塞接收，直到有数据或连接关闭）
        // 接收收到的数据
        int n = recv(client_fd, buf, sizeof(buf) - 1, 0);

        // 如果收到数据
        if (n > 0) {
            // 打印接收的数据
            buf[n] = '\0';
            std::cout << "----- REQUEST START -----\n" << buf << "----- REQUEST END -----\n";
        }

        // 接收到后返回接收成功数据
        int total = (int)response.size();
        int sent = 0;
        while (sent < total) {
            int w = send(client_fd, response.c_str() + sent, total - sent, 0);
            if (w <= 0) break;
            sent += w;
        }

        closesocket(client_fd);
    }

    closesocket(listen_fd);
    WSACleanup();
#else
    int listen_fd;
    listen_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (listen_fd < 0) {
        perror("socket");
        return 1;
    }

    int opt = 1;
    setsockopt(listen_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons(listen_port);
    inet_pton(AF_INET, listen_ip, &addr.sin_addr);

    if (bind(listen_fd, (sockaddr*)&addr, sizeof(addr)) < 0){
        std::cerr << "bind failed\n";
        close(listen_fd);
        return 1;
    }

    if (listen(listen_fd, 10) < 0){
        std::cerr << "listen failed\n";
        close(listen_fd);
        return 1;
    }

    std::cout << "Listening on " << listen_ip << ":" << listen_port << " ...\n";

     while (true) {
        sockaddr_in client_addr;
        socklen_t client_len = sizeof(client_addr);
        int client_fd = accept(listen_fd, (sockaddr*)&client_addr, &client_len);
        if (client_fd < 0) { continue; }

        char buf[4096];
        ssize_t n = recv(client_fd, buf, sizeof(buf) - 1, 0);

        if (n > 0) {
            buf[n] = '\0';
            std::cout << "----- REQUEST START -----\n" << buf << "----- REQUEST END -----\n";
        }

        int total = (int)response.size();
        int sent = 0;
        while (sent < total) {
            ssize_t w = send(client_fd, response.c_str() + sent, total - sent, 0);
            if (w <= 0) break;
            sent += w;
        }

        close(client_fd);
    }

    close(listen_fd);
    #endif

    return 0;
}

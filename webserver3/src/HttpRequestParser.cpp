#include "HttpRequestParser.hpp"

#include <sstream>
#include <algorithm>

/**
 * @brief 解析原始 HTTP 请求字符串
 * @param raw_request 客户端发送的原始请求字符串
 * @return true 解析成功, false 解析失败
 */
bool HttpRequest::parse(const std::string& raw_request) {
    // 清理旧数据
    method.clear();
    path.clear();
    version.clear();
    headers.clear();
    body.clear();

    if (raw_request.empty()) {
        return false;
    }

    std::stringstream ss(raw_request);
    std::string line;

    // 1. 解析请求行 (Request Line: METHOD PATH VERSION)
    if (std::getline(ss, line) && !line.empty()) {
        std::stringstream line_ss(line);
        line_ss >> method >> path >> version;
        
        // 清除末尾的 CR ('\r')
        if (!version.empty() && version.back() == '\r') {
            version.pop_back();
        }

        if (method.empty() || path.empty() || version.empty()) {
            return false; // 请求行格式错误
        }
    } else {
        return false; // 没有请求行
    }

    // 2. 解析请求头 (Headers)
    // 头部以空行 (\r\n\r\n) 结束
    while (std::getline(ss, line) && line != "\r" && !line.empty()) {
        // 清除末尾的 CR ('\r')
        if (line.back() == '\r') {
            line.pop_back();
        }

        size_t colon_pos = line.find(':');
        if (colon_pos != std::string::npos) {
            std::string key = line.substr(0, colon_pos);
            std::string value = line.substr(colon_pos + 1);

            // 移除 key 前后和 value 前后的空格
            key.erase(0, key.find_first_not_of(" \t"));
            key.erase(key.find_last_not_of(" \t") + 1);
            value.erase(0, value.find_first_not_of(" \t"));
            value.erase(value.find_last_not_of(" \t") + 1);

            // 将头部键转换为小写，方便查找（例如 Content-Length vs content-length）
            std::transform(key.begin(), key.end(), key.begin(), ::tolower);
            headers[key] = value;
        }
    }

    // 3. 解析请求体 (Body)
    // 仅在 POST/PUT 等方法中存在，依赖 Content-Length 头部
    if (method == "POST" || method == "PUT") {
        auto it = headers.find("content-length");
        if (it != headers.end()) {
            try {
                int content_length = std::stoi(it->second);

                // 读取剩下的所有内容作为 Body (假设流中剩下的就是 Body)
                // 在实际 Socket 编程中，需要精确读取 Content-Length 字节
                std::string remaining_data;
                char buf[1024];
                while (ss.read(buf, sizeof(buf))) {
                    remaining_data.append(buf, ss.gcount());
                }
                remaining_data.append(buf, ss.gcount());

                // 确保 Body 长度不超过 Content-Length
                if (remaining_data.length() >= content_length) {
                    body = remaining_data.substr(0, content_length);
                } else {
                    // 在实际 Socket 编程中，需要继续读取数据直到达到 Content-Length
                    // 这里由于是字符串解析，我们只能接受当前数据
                    body = remaining_data;
                }

            } catch (const std::exception& e) {
                std::cerr << "Error parsing Content-Length: " << e.what() << std::endl;
            }
        }
    }

    return true;
}

/**
 * @brief 打印解析结果 (调试用)
 */
void HttpRequest::print() const {
    std::cout << "--- Parsed HTTP Request ---" << std::endl;
    std::cout << "Method: " << method << std::endl;
    std::cout << "Path: " << path << std::endl;
    std::cout << "Version: " << version << std::endl;

    std::cout << "Headers (" << headers.size() << "):" << std::endl;
    for (const auto& pair : headers) {
        std::cout << "  " << pair.first << ": " << pair.second << std::endl;
    }

    if (!body.empty()) {
        std::cout << "Body (Length: " << body.length() << "):" << std::endl;
        std::cout << body << std::endl;
    }
    std::cout << "---------------------------" << std::endl;
}
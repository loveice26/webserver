// 定义用于存储解析结果的数据结构和解析类

#include <iostream>
#include <map>

// 枚举：HTTP 方法
enum class HttpMethod {
    GET,
    POST,
    UNSUPPORTED
};

class HttpRequest{
public:
    std::string method;          // GET, POST, ...
    std::string path;            // /index.html
    std::string version;         // HTTP/1.1
    std::map<std::string, std::string> headers;
    std::string body;            // POST 请求体

    // 默认构造函数
    HttpRequest() {}

    bool parse(const std::string& raw_request);

    void print() const;
};
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
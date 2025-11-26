/*  目标：进入高性能 WebServer。
*   知识点：
*       1. I/O 多路复用：select → poll → epoll（Linux）或 IOCP（Windows）。
*       2. Reactor 模型（事件驱动）。
*       3. 零拷贝技术（sendfile / mmap）。
*       4. 缓存/日志系统。
*   实现：
*       1. 用 epoll 替代多线程 accept。
*       2. 写一个高性能日志系统（异步写入）。
*       3. 加入简单缓存，减少磁盘 I/O。
*/
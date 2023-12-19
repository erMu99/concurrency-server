#pragma once

#include <iostream>
#include <algorithm>
#include <vector>
#include <cstring>
#include <cassert>
#include "log.hpp"
#include <sys/types.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <fcntl.h>

#define BUFFER_DEFAULT_SIZE 1024
class Buffer
{
private:
    uint64_t _reader_idx;
    uint64_t _writer_idx;
    std::vector<char> _buffer;

public:
    Buffer() : _reader_idx(0), _writer_idx(0), _buffer(BUFFER_DEFAULT_SIZE) {}
    // 给void*，+len没有步长
    char *Begin() { return &(*_buffer.begin()); }
    // 获取当前写入起始地址
    char *WritePostion() { return Begin() + _writer_idx; }
    // 获取当前读取起始地址
    char *ReadPostion() { return Begin() + _reader_idx; }
    // 获取缓冲区末尾空闲空间大小 -- 写偏移之后的空闲空间
    uint64_t TailIdleSize() { return _buffer.size() - _writer_idx; }
    // 获取缓冲区起始空闲空间大小 -- 读偏移之前的空闲空间
    uint64_t HeadIdleSize() { return _reader_idx; }
    // 获取可读数据大小
    uint64_t ReadableSize() { return _writer_idx - _reader_idx; }
    // 将读偏移向后移动
    void MoveReadOffset(uint64_t len)
    {
        if (len == 0)
            return;
        // 不超过可读数据大小，不能是 < _writer_idx，这个可以到左边空闲位置
        assert(len <= ReadableSize());
        _reader_idx += len;
    }
    // 将写偏移向后移动
    void MoveWriteOffset(uint64_t len)
    {
        if (len == 0)
            return;
        // EnsureWriteSpace会确保后面有足够空间
        assert(len <= TailIdleSize());
        _writer_idx += len;
    }
    // 确保可写空间足够（整体空间足够则移动数据，否则扩容）
    void EnsureWriteSpace(uint64_t len)
    {
        // 后面空间足够
        if (len <= TailIdleSize())
            return;
        // 后面 + 前面空间足够
        if (len <= TailIdleSize() + HeadIdleSize())
        {
            // 将数据移动到起始位置
            uint64_t rsz = ReadableSize();
            // 参数： first last result
            // 把[first, last) 的数据拷贝到result
            std::copy(ReadPostion(), ReadPostion() + rsz, Begin());
            _reader_idx = 0;
            _writer_idx = rsz;
        }
        else
        {
            // 空间不够扩容，扩容成写偏移之后的空间大小
            _buffer.resize(_writer_idx + len);
        }
    }
    // 写入数据
    void Write(const void *data, uint64_t len)
    {
        if (len == 0)
            return;
        EnsureWriteSpace(len);
        // void* +len没有步长
        const char *d = static_cast<const char *>(data);
        std::copy(d, d + len, WritePostion());
    }
    void WriteAndPush(const void *data, uint64_t len)
    {
        Write(data, len);
        MoveWriteOffset(len);
    }
    void WriteString(const std::string &data)
    {
        Write(data.c_str(), data.size());
    }
    void WriteStringAndPush(const std::string &data)
    {
        WriteString(data);
        MoveWriteOffset(data.size());
    }
    void WriteBuffer(Buffer &data)
    {
        Write(data.ReadPostion(), data.ReadableSize());
    }
    void WriteBufferAndPush(Buffer &data)
    {
        WriteBuffer(data);
        MoveWriteOffset(data.ReadableSize());
    }
    // 读取数据
    void Read(void *buf, uint64_t len)
    {
        assert(len <= ReadableSize());
        // 是+len，将len字节数据读取到buf里
        std::copy(ReadPostion(), ReadPostion() + len, (char *)buf);
    }
    // 读取后移动偏移量
    void ReadAndPop(void *buf, uint64_t len)
    {
        Read(buf, len);
        MoveReadOffset(len);
    }
    std::string ReadAsString(uint64_t len)
    {
        assert(len <= ReadableSize());
        std::string str;
        str.resize(len);
        // c_str()返回的是const char*
        Read(&str[0], len);
        return str;
    }
    std::string ReadAsStringAndPop(uint64_t len)
    {
        assert(len <= ReadableSize());
        std::string str = ReadAsString(len);
        MoveReadOffset(len);
        return str;
    }
    // 找\n，为了方便http协议
    char *FindCRLF()
    {
        char *res = (char *)memchr(ReadPostion(), '\n', ReadableSize());

        return res;
    }
    // 读一行
    std::string GetLine()
    {
        char *pos = FindCRLF();

        if (pos == NULL)
        {
            return "";
        }
        // std::cout << "pos - ReadPos: " << pos - ReadPostion() << std::endl;
        return ReadAsString(pos - ReadPostion() + 1); // +1将换行符读出
    }
    std::string GetLineAndPop()
    {
        std::string str = GetLine();
        // std::cout << "GetLine: " << str;
        MoveReadOffset(str.size());
        // std::cout << "read idx: " << _reader_idx << std::endl;
        return str;
    }
    // 清空缓冲区
    void Clear() { _reader_idx = _writer_idx = 0; }
};

class Socket
{
#define MAX_LISTEN 1024

private:
    int _sockfd; // 套接字描述符

public:
    Socket() : _sockfd(-1) {}
    Socket(int fd) : _sockfd(fd) {}
    ~Socket() { Close(); }
    int Fd() { return _sockfd; }
    bool Create()
    {
        _sockfd = socket(AF_INET, SOCK_STREAM, 0);
        if (_sockfd < 0)
        {
            ERR_LOG("Create Socket Failed!");
            return false;
        }
        return true;
    }
    bool Bind(const std::string &ip, uint16_t port)
    {
        struct sockaddr_in local;
        local.sin_family = AF_INET;
        local.sin_addr.s_addr = inet_addr(ip.c_str());
        local.sin_port = htons(port);
        int n = bind(_sockfd, (struct sockaddr *)&local, sizeof(local));
        if (n < 0)
        {
            ERR_LOG("Bind Failed!");
            return false;
        }
        return true;
    }
    bool Listen(int backlog = MAX_LISTEN)
    {
        int n = listen(_sockfd, backlog);
        if (n < 0)
        {
            ERR_LOG("Server Listen Falied!");
            return false;
        }
        return true;
    }
    // 连接服务器
    bool Connect(const std::string &ip, uint16_t port)
    {
        struct sockaddr_in server;
        server.sin_family = AF_INET;
        server.sin_addr.s_addr = inet_addr(ip.c_str());
        server.sin_port = htons(port);
        int n = connect(_sockfd, (struct sockaddr *)&server, sizeof(server));
        if (n < 0)
        {
            ERR_LOG("Connect Server Failed!");
            return false;
        }
        return true;
    }
    int Accept()
    {
        struct sockaddr_in peer;
        socklen_t len = sizeof(peer);
        int sock = accept(_sockfd, (struct sockaddr *)&peer, &len);
        if (sock < 0)
        {
            ERR_LOG("Accept Falied");
            return -1;
        }
        return sock;
    }
    ssize_t Recv(void *buf, size_t len, int flag = 0)
    {
        ssize_t s = recv(_sockfd, buf, len, flag);
        if (s <= 0)
        {
            // EAGAIN: 当前socket接收缓冲区没有数据，非阻塞才会出现此错误
            // EINTR: 信号中断
            if (errno == EAGAIN || errno == EINTR)
            {
                return 0; // 没接收到数据
            }

            ERR_LOG("Socket Recv Failed!");
            return -1;
        }
        return s;
    }
    void NonBlockRecv(void *buf, size_t len)
    {
        Recv(buf, len, MSG_DONTWAIT); // MSG_DONTWAIT当前接收是非阻塞
    }
    ssize_t Send(const void *buf, size_t len, int flag = 0)
    {
        ssize_t s = send(_sockfd, buf, len, flag);
        if (s < 0)
        {
            ERR_LOG("Socket Send Failed!");
            return -1;
        }
        return s;
    }
    void NonBlockSend(void *buf, size_t len)
    {
        Send(buf, len, MSG_DONTWAIT); // MSG_DONTWAIT当前发送是非阻塞
    }
    void Close()
    {
        if (_sockfd != -1)
        {
            close(_sockfd);
            _sockfd = -1;
        }
    }
    bool CreateServer(uint16_t port, const std::string &ip = "0.0.0.0", bool block_flag = false)
    {
        // 1.创建套接字，设置非阻塞  2.绑定ip+port 3.设为监听状态 4.ip+port重用
        if (!Create())
            return false;
        if (block_flag) // 默认是阻塞
            NonBlock();
        if (!Bind(ip, port))
            return false;
        if (!Listen())
            return false;
        ReuseAddress();
        return true;
    }
    bool CreateClient(uint16_t port, const std::string &ip)
    {
        // 1.创建套接字 2.连接
        if (!Create())
            return false;
        if (!Connect(ip, port))
            return false;

        return true;
    }
    void ReuseAddress()
    {
        int val = 1;
        // SO_REUSEADDR：允许重用本地地址。允许新套接字绑定到相同的本地地址，即使有连接处于 TIME_WAIT 状态。
        // SO_REUSEPORT：允许多个套接字绑定到相同的地址和端口
        setsockopt(_sockfd, SOL_SOCKET, SO_REUSEADDR | SO_REUSEPORT, (void *)&val, sizeof(int));
    }
    void NonBlock()
    {
        int flag = fcntl(_sockfd, F_GETFL);
        fcntl(_sockfd, F_SETFD, flag | O_NONBLOCK);
    }
};

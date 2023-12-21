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
#include <sys/epoll.h>
#include <sys/eventfd.h>
#include <unordered_map>
#include <mutex>
#include <thread>
#include <memory>

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
class Poller;
class EventLoop;
class Channel
{
    using EventCallback = std::function<void()>;

private:
    int _fd;
    EventLoop *_loop;
    uint32_t _events;              // 当前需要监控的事件
    uint32_t _revents;             // 当前连接触发的事件 -- 即就绪事件
    EventCallback _read_callback;  // 可读事件被触发的回调
    EventCallback _write_callback; // 可写事件被触发的回调
    EventCallback _error_callback; // 错误事件被触发的回调
    EventCallback _close_callback; // 连接断开事件被触发的回调
    EventCallback _event_callback; // 任意事件被触发的回调
public:
    Channel(EventLoop *loop, int fd)
        : _fd(fd), _loop(loop), _events(0), _revents(0) {}
    int Fd() { return _fd; }
    uint32_t Events() { return _events; }
    void SetRevents(uint32_t events) { _revents = events; }
    void SetReadCallback(const EventCallback &cb) { _read_callback = cb; }
    void SetWriteCallback(const EventCallback &cb) { _write_callback = cb; }
    void SetErrorCallback(const EventCallback &cb) { _error_callback = cb; }
    void SetCloseCallback(const EventCallback &cb) { _close_callback = cb; }
    void SetEventCallback(const EventCallback &cb) { _event_callback = cb; }
    bool Readable() // 当前是否监控了可读
    {
        return _events & EPOLLIN;
    }
    bool Writeable() // 当前是否监控了可写
    {
        return _events & EPOLLOUT;
    }
    void EnableRead() // 启动读事件监控
    {
        _events |= EPOLLIN;
        // ... 待添加到EventLoop的事件监控中
        Update(); // 更新到epoll实例中
    }
    void EnableWrite() // 启动写事件监控
    {
        _events |= EPOLLOUT;
        // ... 待添加到EventLoop的事件监控中
        Update();
    }
    void DisableRead() // 关闭读事件监控
    {
        _events &= ~EPOLLIN;
        // ... 待修改到EventLoop的事件监控中
        Update();
    }
    void DisableWrite() // 关闭写事件监控
    {
        _events &= ~EPOLLOUT;
        // ... 待修改到EventLoop的事件监控中
        Update();
    }
    void DisableAll() // 关闭所有事件监控
    {
        _events = 0;
        // ... 待修改到EventLoop的事件监控中
        Update();
    }
    // 先声明，在Poller类后实现
    void Remove(); // 移除监控，把fd从RBTree删除
    // {
    //     // 调用EventLoop接口来移除监控
    //     _poller->RemoveEvent(this);// 上面只声明了类，直接调用编译器不知道类里有什么成员函数
    // }
    void Update();
    // {
    //     _poller->UpdateEvent(this);// 把Channel*设置进_poller
    // }
    void HandleEvent() // 事件处理，一旦连接触发了事件，就调用这个函数
    {
        // EPOLLRDHUP: 对方关闭了连接不会再发送数据，此时可处理读事件 -- 半关闭连接
        //             对方调用了 shutdown 或 close
        // EPOLLPRI: 紧急数据
        // 触发了任意事件都会调用的回调
        if (_event_callback)
            _event_callback();
        if ((_revents & EPOLLIN) || (_revents & EPOLLRDHUP) || (_revents & EPOLLPRI))
        {
            if (_read_callback)
                _read_callback();
        }
        /*有可能会释放连接的操作事件，一次只处理一个*/
        if (_revents & EPOLLOUT)
        {
            if (_write_callback)
                _write_callback();
        }
        else if (_revents & EPOLLERR)
        {
            if (_error_callback)
                _error_callback();
        }
        else if (_revents & EPOLLHUP) // 正常关闭和异常关闭
        {
            if (_close_callback)
                _close_callback();
        }
    }
};

#define MAX_EPOLLEVENTS 1024
class Poller
{
private:
    int _epfd;                                    // epoll实例fd
    struct epoll_event _evs[MAX_EPOLLEVENTS];     // 存放就绪队列的就绪事件
    std::unordered_map<int, Channel *> _channels; // [fd, Channel*]
private:
    // 直接操作epoll实例
    void Update(Channel *channel, int op)
    {
        int fd = channel->Fd();
        uint32_t events = channel->Events();
        struct epoll_event ev;
        ev.data.fd = fd;
        ev.events = events;
        int ret = epoll_ctl(_epfd, op, fd, &ev);
        if (ret < 0)
        {
            ERR_LOG("EpollCtl Failed!");
            // abort();// 退出程序
        }
    }
    // 判断一个Channel是否添加了事件监控，即在不在哈希表中
    bool HasChannel(Channel *channel)
    {
        return _channels.count(channel->Fd());
    }

public:
    Poller()
    {
        _epfd = epoll_create(MAX_EPOLLEVENTS);
        if (_epfd < 0)
        {
            ERR_LOG("EpollCreate Failed");
            abort(); // 退出程序
        }
    }
    // 添加或修改监控事件
    void UpdateEvent(Channel *channel)
    {
        if (HasChannel(channel))
        {
            Update(channel, EPOLL_CTL_MOD);
        }
        else
        {
            Update(channel, EPOLL_CTL_ADD);     // 不存在添加到RBTree
            _channels[channel->Fd()] = channel; // 添加到哈希表
        }
    }
    // 移除监控
    void RemoveEvent(Channel *channel)
    {
        if (HasChannel(channel))
        {
            _channels.erase(channel->Fd());
        }
        Update(channel, EPOLL_CTL_DEL);
    }
    // 开始监控，返回活跃连接
    void Poll(std::vector<Channel *> *active)
    {
        // -1阻塞
        int nfds = epoll_wait(_epfd, _evs, MAX_EPOLLEVENTS, -1);
        if (nfds < 0)
        {
            if (errno == EINTR)
                return;
            ERR_LOG("EpollWait Error: %s", strerror(errno));
            abort();
        }
        for (int i = 0; i < nfds; ++i)
        {
            auto it = _channels.find(_evs[i].data.fd);
            assert(it != _channels.end());
            it->second->SetRevents(_evs[i].events); // 设置实际就绪事件
            active->push_back(it->second);
        }
    }
};

class EventLoop
{
    using Functor = std::function<void()>;

private:
    // 不在EventLoop线程就push到任务池
    // 在就直接执行
    std::thread::id _thread_id; // 线程id
    // 因为等待描述符IO事件就绪，导致执行流流程阻塞
    // 此时任务队列中任务就得不到执行
    int _evevnt_fd;                          // eventfd唤醒IO事件监控有可能导致的阻塞
    std::unique_ptr<Channel> _event_channel; // 对_evevnt_fd的事件管理
    Poller _poller;                          // 进行所有fd的监控
    std::vector<Functor> _tasks;             // 任务池  -- 对任务池操作要加锁
    std::mutex _mtx;                         // 保证任务池安全的锁
private:
    void RunAllTask() // 执行任务池中所有任务
    {
        std::vector<Functor> functor;
        {
            // RAII：构造函数加锁  析构函数解锁
            std::unique_lock<std::mutex> lock(_mtx);
            functor.swap(_tasks);
        }
        for (auto &f : functor)
        {
            f();
        }
    }
    static int CreateEventFd()
    {
        int efd = eventfd(0, EFD_CLOEXEC | EFD_NONBLOCK);
        if (efd < 0)
        {
            ERR_LOG("CreateEventFd Failed!");
            abort();
        }
        return efd;
    }
    void ReadEventFd()
    {
        uint64_t res = 0;
        // 一次读取，eventfd就为0
        int ret = read(_evevnt_fd, &res, sizeof(res));
        if (ret < 0)
        {
            if (errno == EINTR || errno == EAGAIN)
            {
                return;
            }
            ERR_LOG("Read EventFd Faild");
            abort();
        }
    }
    void WeakUpEventFd()
    {
        uint64_t val;
        int ret = write(_evevnt_fd, &val, sizeof(val));
        if (ret < 0)
        {
            if (errno == EINTR || errno == EAGAIN)
            {
                return;
            }
            ERR_LOG("Write EventFd Faild");
            abort();
        }
    }

public:
    EventLoop()
        : _thread_id(std::this_thread::get_id()),
          _evevnt_fd(CreateEventFd()),
          _event_channel(new Channel(this, _evevnt_fd))
    {
        // 给eventfd添加可读事件回调函数，读取eventfd事件通知次数
        _event_channel->SetReadCallback(std::bind(&EventLoop::ReadEventFd, this));
        _event_channel->EnableRead(); // 启动读事件监控
    }
    // 判断将要执行的任务是否在当前线程总，若是则执行，否则push到任务池
    void RunInLoop(const Functor &cb)
    {
        if (IsInLoop())
            cb();
        else
            QueueInLoop(cb);
    }
    void QueueInLoop(const Functor &cb)
    {
        {
            std::unique_lock<std::mutex> lock(_mtx); // 加锁
            _tasks.push_back(cb);
        }
        // 没有事件就绪会epoll导致阻塞，唤醒
        // 实质是向eventfd写入一个数据，eventfd会触发可读事件 -- epoll被唤醒
        WeakUpEventFd();
    }
    bool IsInLoop() // 判断当前线程是否在EventLoop对应的线程
    {
        return std::this_thread::get_id() == _thread_id;
    }
    void UpdateEvent(Channel *channel) // 添加、修改fd的事件监控
    {
        _poller.UpdateEvent(channel);
    }
    void RemoveEvent(Channel *channel) // 移除fd的事件监控
    {
        _poller.RemoveEvent(channel);
    }
    void Start() // 事件监控 --> 就绪事件处理 --> 执行任务
    {
        // 事件监控
        std::vector<Channel *> actives;
        _poller.Poll(&actives); // 这里没有事件就绪会阻塞
        // 就绪事件处理
        for (auto &a : actives)
        {
            a->HandleEvent();
        }
        // 执行任务
        RunAllTask();
    }
};

void Channel::Remove()
{
    _loop->RemoveEvent(this);
}
void Channel::Update()
{
    _loop->UpdateEvent(this);
}
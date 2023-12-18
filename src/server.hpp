#pragma once

#include <iostream>
#include <algorithm>
#include <vector>
#include <cstring>
#include <cassert>

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

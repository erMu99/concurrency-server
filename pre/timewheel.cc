#include <iostream>
#include <vector>
#include <unordered_map>
#include <functional>
#include <memory>
#include <unistd.h>

// 时间轮
using TaskFunc = std::function<void()>;
using ReleaseFunc = std::function<void()>;
class TimerTask
{

private:
    uint64_t _id;         // 定时任务对象id
    uint32_t _timeout;    // 定时任务的超时时间
    TaskFunc _task_cb;    // 定时器对象要执行的定时任务
    ReleaseFunc _release; // 用于删除TimeWheel中保存的定时器对象信息 -- 从_timers哈希表中删除
    bool _canceled;
public:
    TimerTask(uint64_t id, uint32_t timeout, const TaskFunc &cb)
        : _id(id), _timeout(timeout), _task_cb(cb), _canceled(false)
    {
    }

    void SetRelease(const ReleaseFunc &cb)
    {
        _release = cb;
    }
    uint32_t GetTimeout()
    {
        return _timeout;
    }
    void Cancel()
    {
        _canceled = true;
    }
    ~TimerTask()
    {
        if (!_canceled)
            _task_cb();
        _release();
    }
};

class TimeWheel
{
    // weak_ptr不会增加引用计数，它可以解决shared_ptr的循环引用问题
    // 在这里是为了哈希表里不存shared_ptr，不增加引用计数
    using WeakTask = std::weak_ptr<TimerTask>;
    using PtrTask = std::shared_ptr<TimerTask>;

private:
    int _capacity;                                  // 数组最大容量，就是最大延迟时间
    int _tick;                                      // 当前秒针，走到哪里就释放到哪里，释放哪里，就相当于执行哪里的任务
    std::vector<std::vector<PtrTask>> _wheels;      // 时间轮数组
    std::unordered_map<uint64_t, WeakTask> _timers; // [id, 定时任务]

private:
    void RemoveTimer(uint64_t id)// 从哈希表中删除
    {
        auto it = _timers.find(id);
        if (it != _timers.end())
            _timers.erase(it);
    }

public:
    TimeWheel() : _capacity(60), _tick(0), _wheels(_capacity) {}
    // 添加定时任务
    void TimerAdd(uint64_t id, uint32_t delay, const TaskFunc &cb)
    {
        PtrTask ptr(new TimerTask(id, delay, cb)); // shared_ptr
        ptr->SetRelease(std::bind(&TimeWheel::RemoveTimer, this, id));

        int pos = (_tick + delay) % _capacity; // 环形
        _wheels[pos].push_back(ptr);

        _timers[id] = WeakTask(ptr); // weak_ptr
    }
    // 刷新/延迟定时任务
    void TimerRefresh(uint64_t id)
    {
        // 通过保存的weak_ptr构造新的shared_ptr，添加到时间轮_wheels
        auto it = _timers.find(id);
        if (it != _timers.end())
        {
            // lock获取weak_ptr对应的shared_ptr，新构造的shared_ptr会增加引用计数，旧的shared_ptr析构不会释放对象，因为引用计数不为0
            PtrTask ptr = it->second.lock();
            uint32_t delay = ptr->GetTimeout();
            int pos = (_tick + delay) % _capacity; // 环形
            _wheels[pos].push_back(ptr);
        }
    }
    void RunTimerTask() // 每秒执行一次，相当于秒针向后走一步
    {
        _tick = (_tick + 1) % _capacity;
        // 清空指定位置，就会把数组中的定时器对象全部释放掉
        // 如果引用计数为0，就释放哪里，就相当于执行哪里的任务
        _wheels[_tick].clear();
    }
    void TimerCancel(uint64_t id)
    {
        auto it = _timers.find(id);
        if (it != _timers.end())
        {
            PtrTask pt = it->second.lock();
            if (pt) pt->Cancel();
        }
    }
    ~TimeWheel() {}
};

class Test
{
public:
    Test() { std::cout << "构造" << std::endl; }
    ~Test() { std::cout << "析构" << std::endl; }
};
void DelTest(Test *t) { delete t; }

int main()
{
    TimeWheel tw;
    Test *t = new Test();
    uint64_t id = 888;
    uint32_t timeout = 3;
    tw.TimerAdd(id, timeout, std::bind(DelTest, t));// 定时任务是会调析构
    for (int i = 0; i < 3; ++i)
    {
        sleep(1);
        tw.TimerRefresh(id);
        tw.RunTimerTask();
        std::cout << "刷新了一下定时任务，重新需要3s后才会销毁" << std::endl;
    }
    tw.TimerCancel(id);// 取消定时任务
    while (1)
    {
        sleep(1);
        std::cout << "----------------" << std::endl;
        tw.RunTimerTask();
    }
    return 0;
}
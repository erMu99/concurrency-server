#include <iostream>
#include <cstdio>
#include <string>
#include <ctime>
#include <cstdlib>
#include <unistd.h>
#include <sys/timerfd.h>
#include <sys/select.h>
int main()
{
    /*创建⼀个定时器 */
    int timerfd = timerfd_create(CLOCK_MONOTONIC, 0);
    struct itimerspec itm;
    itm.it_value.tv_sec = 3; // 设置第⼀次超时的时间
    itm.it_value.tv_nsec = 0;
    itm.it_interval.tv_sec = 3; // 第⼀次超时后，每隔多⻓时间超时
    itm.it_interval.tv_nsec = 0;
    timerfd_settime(timerfd, 0, &itm, NULL); // 启动定时器
    /*这个定时器描述符将每隔三秒都会触发⼀次可读事件*/
    time_t start = time(NULL);
    while (1)
    {
        uint64_t tmp;
        /*需要注意的是定时器超时后,则描述符触发可读事件，必须读取8字节的数据，保存的是⾃上一次超时到现在【超时的次数】*/
        int ret = read(timerfd, &tmp, sizeof(tmp));
        if (ret < 0)
        {
            return -1;
        }
        auto t = time(NULL);
        std::cout << " 超时次数" << tmp << " "
                  << " current time: " << t
                  << " start time: " << start
                  << " 时间差：" << t - start << std::endl;
    }
    close(timerfd);
    return 0;
}
#include <iostream>
#include <unistd.h>
#include <string>
#include <vector>
#include <any>
#include <typeinfo>
#include <cassert>
class Any
{
private:
    class holder
    {
    public:
        virtual const std::type_info &type() = 0; // 获取子类对象保存的数据类型
        virtual holder *clone() = 0;              // 针对当前的对象自身，克隆出一个新的子类对象
        virtual ~holder() {}
    };
    template <class T>
    class placeholder : public holder
    {
    public:
        placeholder(const T &val) : _val(val) {}
        virtual const std::type_info &type() { return typeid(T); } // tpyeid的返回值是const&
        virtual holder *clone() { return new placeholder<T>(_val); }

    public:
        T _val;
    };
    holder *_content; // 父类指针
public:
    Any() : _content(nullptr) {}
    template <class T>
    Any(const T &val) : _content(new placeholder<T>(val)) {}
    Any(const Any &other) // 注意不能把other._content直接赋值过来，容易指针悬空
        : _content(other._content ? other._content->clone() : nullptr)
    {
    }
    ~Any() { delete _content; }
    template <class T>
    T *get() // 返回子类保存的对象的指针
    {
        // 模板参数类型与保存的类型不一致
        assert(typeid(T) == _content->type());
        // if (typeid(T) != _content->type())
        //     return nullptr;
        // _content父类指针是没有_val的，要强转为子类
        return &(((placeholder<T> *)_content)->_val);
    }
    Any &swap(Any &other)
    {
        std::swap(_content, other._content); // 交换指针
        return *this;
    }
    template <class T>
    Any &operator=(const T &val)
    {
        Any(val).swap(*this);
        // 临时对象会被销毁
        return *this; // 为了连续赋值
    }
    Any &operator=(const Any &other)
    {
        // 构造新的临时对象，不影响外部other
        Any(other).swap(*this);
        return *this;
    }
};

void TestAny()
{
    // Any类型可以保存任意类型数据
    Any a, c;
    a = 10;
    c = a;
    int *pa = a.get<int>();
    std::cout << *pa << std::endl;
    std::cout << *(c.get<int>()) << std::endl;

    a = std::string("hello");
    std::cout << *(a.get<std::string>()) << std::endl;

    Any b(10);
    *(b.get<int>()) = 20;
    std::cout << *(b.get<int>()) << std::endl;

    b = std::vector<int>(3);
    auto pb = b.get<std::vector<int>>();
    pb->push_back(56);
    for (auto e : *pb)
    {
        std::cout << e << std::endl;
    }
}
// 简单测试是否有内存泄漏
class A
{
public:
    A() { std::cout << "A()\n"; }
    A(const A &a) { std::cout << "A(const A&)\n"; }
    ~A() { std::cout << "~A()\n"; }
};
void Test()
{
    Any any;
    {
        // Any any;
        A a;
        any = a;
    }
    // A() --> A a;
    // A(const A &)  --> Any(const T &val) -->  placeholder(const T &val) : _val(val)
    // ~A()
    // ~A()
    // any = 10;
    while (1)
        sleep(1);
}
// c++17
void STDAny()
{
    std::any a;
    a = 10;
    std::cout << *std::any_cast<int>(&a) << std::endl;
    a = "hello";
    std::cout << *std::any_cast<const char *>(&a) << std::endl;
    char *s = "world"; // warning
    a = s;
    std::cout << *std::any_cast<char *>(&a) << std::endl;
}

int main()
{
    // TestAny();
    // Test();
    STDAny();
    return 0;
}
#include <iostream>
#include <string>
#include <regex>
// #include <bits/stdc++.h>

void HttpRequest()
{
    // HTTP请求行   GET /example/login?user=zhangsan&password=123456 HTTP/1.1\r\n
    std::string str = "GET /example/login?user=zhangsan&password=123456 HTTP/1.1";
    // [^?] 匹配非?字符 *0次或多次
    // (?:...) ?:表示匹配但不提取 ?表示匹配0次或1次
    std::regex re("(GET|POST|PUT|HEAD|DELETE) ([^?]*)(?:\\?(.*))? (HTTP/1\\.[01])(?:\n|\r\n)?");
    std::smatch matches;
    bool ret = std::regex_match(str, matches, re);
    if (!ret)
        return;
    for (auto &s : matches)
    {
        std::cout << s << std::endl;
    }
}

int main()
{
    // HttpRequest();
    return 0;
}
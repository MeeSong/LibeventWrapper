# LibeventWrapper

Platfrom: Windows Visual Studio 2015    
Language: C++11 or higher    
Libevent Version: 2.2.0-alpha-dev or higher ?    

这个项目是对 Libevent 的 C++ 包装，仅适用于 Windows 平台。

整个 vsprotect 文件夹拷贝到 libevent 根目录即可使用。

解决方案分为 6 个项目。

libevent, libevent_core, libevent_extra 是官方的基本的静态库，无 OpenSSL 支持。

LibeventWrapper 是依赖于 libevent_core.lib 的 VC++ 包装，同样为静态库。

UnitTest.XXXX 是服务端和客户端的示例（测试）代码，具体使用可参考 UnitTest。

LibeventWrapper 有以下几个特点：

* 如果没有更多的需求，只需要包含 LibeventWrapper.h 头文件极其 lib，无需包含 libevent 的东西。
* LibeventWrapper 对 TCP，UDP 进行了包装，支持默认的select模型和IOCP模型。

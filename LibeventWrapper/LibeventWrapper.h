#pragma once
#include <WinSock2.h>
#include <functional>

//////////////////////////////////////////////////////////////////////////

struct event;
struct event_base;
struct bufferevent;
struct evconnlistener;

class LibeventWrapper
{
public:
    enum LIBEVENT_EVENT_ENUM : UINT32
    {
        LIBEV_UNKNOWN = 0x00,

        LIBEV_READ    = 0x01,
        LIBEV_WRITE   = 0x02,
        LIBEV_SIGNAL  = 0x04,

        LIBEV_EOF     = 0x10,
        LIBEV_ERROR   = 0x20,
        LIBEV_TIMEOUT = 0x40,
    };

    struct LIBEVENT_CALLBACK_PACK
    {
        UINT8   m_IsValidOnListener : 1;
        UINT8   m_IsValidOnRead : 1;
        UINT8   m_IsValidOnWrite : 1;
        UINT8   m_IsValidOnEvent : 1;

        std::function<HRESULT(intptr_t aSocket, sockaddr *aSockaddr)>   m_OnListener;
        std::function<HRESULT(intptr_t fd, size_t aNeedReadSize)>       m_OnRead;
        std::function<HRESULT(intptr_t fd)>                             m_OnWrite;
        std::function<HRESULT(LIBEVENT_EVENT_ENUM aEvents, intptr_t fd)>    m_OnEvent;

        LIBEVENT_CALLBACK_PACK()
        {
            m_IsValidOnListener = m_IsValidOnRead = m_IsValidOnWrite =
                m_IsValidOnEvent = FALSE;
        }
    };
    LIBEVENT_CALLBACK_PACK  m_CallbackPack;
    
    bool        m_IsIOCP = false;
    bool        m_IsUDP = false;
    event_base  *m_evbase = nullptr;
    bufferevent *m_evbuf = nullptr;
    evconnlistener *m_listener = nullptr;
    event       *m_TcpTimeout = nullptr;
    event       *m_udp = nullptr;
    intptr_t    m_UdpSocket = 0;

public:
    LibeventWrapper();
    ~LibeventWrapper();

    HRESULT RegisterCallback(LIBEVENT_CALLBACK_PACK *aCallbackPack);

    HRESULT Initialize(bool aUseIOCP);
    void    Uninitialize();

    HRESULT StartDispatch();
    HRESULT StopDispatch(long aSeconds = 0);

    HRESULT Connect(
        const char *aIP, 
        short aPort,
        const long aTimeoutSec = 0,
        short aFamily = AF_INET, 
        short aSocktype = SOCK_STREAM,
        short aProtocol = 0);
    HRESULT Listen(
        const char *aIP,
        short aPort,
        const long aTimeoutSec = 0,
        short aFamily = AF_INET,
        short aSocktype = SOCK_STREAM,
        short aProtocol = 0);

    static HRESULT GetLastError();
    static const char *GetLastErrorString(HRESULT hr);

    HRESULT Send(const void *aData, size_t aSize);
    HRESULT SendTo(intptr_t fd, const void *aData, size_t aSize);

    HRESULT SendToForUDP(
        const void * aData, size_t aSize,
        const char *aIP, short aPort, short aFamily = AF_INET);

    HRESULT SendToForUDP(
        intptr_t fd, const void * aData, size_t aSize, 
        sockaddr *aTo, int aTolen);

    size_t Receive(void *aData, size_t aSize);
    size_t ReceiveFrom(intptr_t fd, void *aData, size_t aSize);
    size_t ReceiveFromForUDP(intptr_t fd, void *aData, size_t aSize, 
        sockaddr *aFrom, int *aFromlen);

    HRESULT SetTimeouts(const long aReadTimeoutSec, const long aWriteTimeoutSec);
    HRESULT SetTimeouts(void * aEvbuf, const long aReadTimeoutSec, const long aWriteTimeoutSec);

    void *RegisterSignal(int aSig);
    void UnregisterSignal(void *aSignal);

private:    
    HRESULT BuildSockaddr(
        sockaddr **sa, int *salen, 
        const char *aIP, short aPort, short aFamily);
    void FreeSockaddr(sockaddr **sa);
};


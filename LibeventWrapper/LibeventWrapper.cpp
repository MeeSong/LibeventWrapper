#include "LibeventWrapper.h"

#include <assert.h>
#include <signal.h>

#include <WinSock2.h>
#pragma comment(lib, "ws2_32.lib")

#include <Mstcpip.h>
#pragma comment(lib, "Ntdll.lib")

#include <event2/thread.h>
#include <event2/buffer.h>
#include <event2/event.h>
#include <event2/event_compat.h>
#include <event2/bufferevent.h>
#include <event2/listener.h>
#include <event2/util.h>

//////////////////////////////////////////////////////////////////////////

using NTSTATUS = LONG;

#ifndef NT_SUCCESS
#define NT_SUCCESS(Status) (((NTSTATUS)(Status)) >= 0)
#endif

//////////////////////////////////////////////////////////////////////////


LibeventWrapper::LibeventWrapper()
{ }

LibeventWrapper::~LibeventWrapper()
{ }

HRESULT LibeventWrapper::RegisterCallback(LIBEVENT_CALLBACK_PACK * aCallbackPack)
{
    HRESULT hr = S_OK;

    for (;;)
    {
        m_CallbackPack = *aCallbackPack;

        break;
    }

    return hr;
}

HRESULT LibeventWrapper::Initialize(bool aUseIOCP)
{
    HRESULT hr = S_OK;

    m_IsIOCP = aUseIOCP;

    for (;;)
    {
        WSADATA vWSAData = { 0 };
        hr = HRESULT_FROM_WIN32(WSAStartup(MAKEWORD(2, 2), &vWSAData));
        if (FAILED(hr))
        {
            break;
        }

        if (LOBYTE(vWSAData.wVersion) != 2 || HIBYTE(vWSAData.wVersion) != 2)
        {
            WSACleanup();

            hr = ERROR_NDIS_BAD_VERSION;
            break;
        }

        evthread_use_windows_threads();

        event_config *vConfig = event_config_new();
        if (nullptr == vConfig)
        {
            hr = HRESULT_FROM_WIN32(WSA_NOT_ENOUGH_MEMORY);
            break;
        }

        if (aUseIOCP)
        {
            event_config_set_flag(vConfig, EVENT_BASE_FLAG_STARTUP_IOCP);
        }

        m_evbase = event_base_new_with_config(vConfig);
        event_config_free(vConfig);
        if (nullptr == m_evbase)
        {
            hr = HRESULT_FROM_WIN32(WSA_NOT_ENOUGH_MEMORY);
            break;
        }

        break;
    }

    return hr;
}

void LibeventWrapper::Uninitialize()
{
    if (m_evbuf)
    {
        bufferevent_free(m_evbuf);
        m_evbuf = nullptr;
    }

    if (m_udp)
    {
        event_free(m_udp);
        m_udp = nullptr;
    }

    if (m_UdpSocket)
    {
        evutil_closesocket(m_UdpSocket);
        m_UdpSocket = 0;
    }

    if (m_listener)
    {
        evconnlistener_free(m_listener);
        m_listener = nullptr;
    }

    if (m_evbase)
    {
        event_base_free(m_evbase);
        m_evbase = nullptr;
    }

    WSACleanup();
}

HRESULT LibeventWrapper::StartDispatch()
{
    HRESULT hr = S_OK;

    for (;;)
    {
        assert(nullptr != m_evbase);
        if (nullptr == m_evbase)
        {
            hr = E_POINTER;
            break;
        }

        if (NOERROR != event_base_dispatch(m_evbase))
        {
            hr = GetLastError();
            hr = FAILED(hr) ? hr : E_UNEXPECTED;
            break;
        }

        break;
    }

    return hr;
}

HRESULT LibeventWrapper::StopDispatch(long aSeconds)
{
    HRESULT hr = S_OK;
    timeval vDelay = { aSeconds, 0 };

    for (;;)
    {
        assert(nullptr != m_evbase);
        if (nullptr == m_evbase)
        {
            hr = E_POINTER;
            break;
        }

        if (NOERROR != event_base_loopexit(m_evbase, &vDelay))
        {
            hr = GetLastError();
            hr = FAILED(hr) ? hr : E_UNEXPECTED;
            break;
        }

        break;
    }

    return hr;
}

static void OnRead(bufferevent *aEvbuf, void *aContext)
{
    HRESULT hr = S_OK;
    auto vThis = static_cast<LibeventWrapper*>(aContext);

    size_t vNeedReadSize = evbuffer_get_length(bufferevent_get_input(aEvbuf));

    if (vThis->m_CallbackPack.m_IsValidOnRead)
    {
        hr = vThis->m_CallbackPack.m_OnRead(reinterpret_cast<evutil_socket_t>(aEvbuf), vNeedReadSize);
    }
}

static void OnWrite(bufferevent *aEvbuf, void *aContext)
{
    HRESULT hr = S_OK;
    auto vThis = static_cast<LibeventWrapper*>(aContext);

    if (vThis->m_CallbackPack.m_IsValidOnWrite)
    {
        hr = vThis->m_CallbackPack.m_OnWrite(reinterpret_cast<evutil_socket_t>(aEvbuf));
    }
}

static void OnEvent(bufferevent *aEvbuf, short aEvents, void *aContext)
{
    HRESULT hr = S_OK;

    auto vThis = static_cast<LibeventWrapper*>(aContext);
    UINT32 vEvent = static_cast<UINT32>(LibeventWrapper::LIBEVENT_EVENT_ENUM::LIBEV_UNKNOWN);

    if (aEvents & BEV_EVENT_CONNECTED)
        return;

    if (aEvents & BEV_EVENT_READING)
        vEvent |= static_cast<UINT32>(LibeventWrapper::LIBEVENT_EVENT_ENUM::LIBEV_READ);
    else if (aEvents & BEV_EVENT_WRITING)
        vEvent |= static_cast<UINT32>(LibeventWrapper::LIBEVENT_EVENT_ENUM::LIBEV_WRITE);

    if (aEvents & BEV_EVENT_EOF)
        vEvent |= static_cast<UINT32>(LibeventWrapper::LIBEVENT_EVENT_ENUM::LIBEV_EOF);
    else if (aEvents & BEV_EVENT_ERROR)
        vEvent |= static_cast<UINT32>(LibeventWrapper::LIBEVENT_EVENT_ENUM::LIBEV_ERROR);

    hr = GetLastError();

    if ((aEvents & BEV_EVENT_EOF) ||
        (aEvents & BEV_EVENT_ERROR))
    {
        // if Server, The evbuf is Client evbuf.
        // if Client, The evbuf is Self evbuf

        if (vThis->m_listener)
        {
            bufferevent_free(aEvbuf);
        }
        else
        {
            bufferevent_free(aEvbuf);
            vThis->m_evbuf = nullptr;
        }
    }

    if (vThis->m_CallbackPack.m_IsValidOnEvent)
    {
        EVUTIL_SET_SOCKET_ERROR(hr);
        hr = vThis->m_CallbackPack.m_OnEvent(
            static_cast<LibeventWrapper::LIBEVENT_EVENT_ENUM>(vEvent), static_cast<int>(-1));
    }
}

static void OnBaseEvent(evutil_socket_t fd, short aEvents, void *aContext)
{
    HRESULT hr = S_OK;
    auto vThis = static_cast<LibeventWrapper*>(aContext);

    if (aEvents & EV_TIMEOUT)
    {
        UINT32 vEvent = static_cast<UINT32>(LibeventWrapper::LIBEVENT_EVENT_ENUM::LIBEV_TIMEOUT);

        if (aEvents & BEV_EVENT_READING)
            vEvent |= static_cast<UINT32>(LibeventWrapper::LIBEVENT_EVENT_ENUM::LIBEV_READ);
        else if (aEvents & BEV_EVENT_WRITING)
            vEvent |= static_cast<UINT32>(LibeventWrapper::LIBEVENT_EVENT_ENUM::LIBEV_WRITE);

        if (vThis->m_CallbackPack.m_IsValidOnEvent)
        {
            hr = vThis->m_CallbackPack.m_OnEvent(
                static_cast<LibeventWrapper::LIBEVENT_EVENT_ENUM>(vEvent), fd);
        }
    }
    else if (aEvents == EV_READ)
    {
        if (vThis->m_CallbackPack.m_IsValidOnRead)
        {
            hr = vThis->m_CallbackPack.m_OnRead(fd, static_cast<size_t>(-1));
        }
    }
    else if (aEvents == EV_WRITE)
    {
        if (vThis->m_CallbackPack.m_IsValidOnWrite)
        {
            hr = vThis->m_CallbackPack.m_OnWrite(fd);
        }
    }
}

static void OnSignal(evutil_socket_t fd, short aEvents, void *aContext)
{
    HRESULT hr = S_OK;

    auto vThis = static_cast<LibeventWrapper*>(aContext);
    UINT32 vEvent = static_cast<UINT32>(LibeventWrapper::LIBEVENT_EVENT_ENUM::LIBEV_SIGNAL);

    if (vThis->m_CallbackPack.m_IsValidOnEvent)
    {
        hr = vThis->m_CallbackPack.m_OnEvent(
            static_cast<LibeventWrapper::LIBEVENT_EVENT_ENUM>(vEvent), fd);
    }
}

static void OnListener(evconnlistener *aListener, evutil_socket_t aSocket,
    sockaddr *aSa, int aSocklen, void *aContext)
{
    HRESULT hr = S_OK;
    auto vThis = static_cast<LibeventWrapper*>(aContext);

    bufferevent *vEvbuf = nullptr;

    for (;;)
    {
        vEvbuf = bufferevent_socket_new(vThis->m_evbase, aSocket,
            BEV_OPT_CLOSE_ON_FREE | BEV_OPT_THREADSAFE);
        if (nullptr == vEvbuf)
        {
            event_base_loopbreak(vThis->m_evbase);
            hr = HRESULT_FROM_WIN32(WSA_NOT_ENOUGH_MEMORY);
            break;
        }

        bufferevent_setcb(vEvbuf, OnRead, OnWrite, OnEvent, (void*)vThis);
        if (NOERROR != bufferevent_enable(vEvbuf, EV_READ | EV_WRITE | EV_PERSIST))
        {
            hr = GetLastError();
            hr = hr == NOERROR ? E_UNEXPECTED : hr;
            break;
        }

        break;
    }

    if (FAILED(hr))
    {
        if (vEvbuf)
        {
            bufferevent_free(vEvbuf);
            vEvbuf = nullptr;
        }

        if (vThis->m_CallbackPack.m_IsValidOnEvent)
        {
            EVUTIL_SET_SOCKET_ERROR(hr);
            hr = vThis->m_CallbackPack.m_OnEvent(
                LibeventWrapper::LIBEVENT_EVENT_ENUM::LIBEV_ERROR,
                static_cast<evutil_socket_t>(-1));
        }

        return;
    }

    if (vThis->m_CallbackPack.m_IsValidOnListener)
    {
        hr = vThis->m_CallbackPack.m_OnListener(aSocket, aSa);
    }
}

HRESULT LibeventWrapper::Connect(
    const char * aIP, short aPort,
    const long aTimeoutSec,
    short aFamily, short aSocktype, short aProtocol)
{
    HRESULT hr = S_OK;

    evutil_socket_t s = INVALID_SOCKET;
    sockaddr *vSa = nullptr;

    for (;;)
    {
        assert(nullptr != m_evbase);
        if (nullptr == m_evbase)
        {
            hr = E_POINTER;
            break;
        }

        int vSalen = 0;
        hr = BuildSockaddr(&vSa, &vSalen, aIP, aPort, aFamily);
        if (FAILED(hr))
        {
            break;
        }

        s = ::socket(aFamily, aSocktype, aProtocol);
        if (INVALID_SOCKET == s)
        {
            hr = GetLastError();
            break;
        }

        // EVUTIL_SOCK_NONBLOCK
        if (NOERROR != evutil_make_socket_nonblocking(s))
        {
            hr = GetLastError();
            break;
        }

        timeval vTv = { aTimeoutSec, 0 };

        if (aSocktype == SOCK_DGRAM || aProtocol == IPPROTO_UDP)
        {
            m_IsUDP = true;

            m_udp = event_new(m_evbase, s, EV_READ | EV_TIMEOUT | EV_PERSIST, OnBaseEvent, (void*)this);
            if ((nullptr == m_udp) ||
                (NOERROR != event_add(m_udp, aTimeoutSec ? &vTv : nullptr)))
            {
                hr = HRESULT_FROM_WIN32(WSA_NOT_ENOUGH_MEMORY);
                break;
            }

            m_UdpSocket = s;

            hr = S_OK;
            break;
        }

        m_evbuf = bufferevent_socket_new(m_evbase, s,
            BEV_OPT_CLOSE_ON_FREE | BEV_OPT_THREADSAFE);
        if (nullptr == m_evbuf)
        {
            hr = GetLastError();
            hr = FAILED(hr) ? hr : E_UNEXPECTED;
            break;
        }

        bufferevent_setcb(m_evbuf, OnRead, OnWrite, OnEvent, (void*)this);

        if (NOERROR != bufferevent_socket_connect(m_evbuf, vSa, vSalen))
        {
            hr = GetLastError();
            hr = FAILED(hr) ? hr : E_UNEXPECTED;
            break;
        }

        if (NOERROR != bufferevent_enable(m_evbuf, EV_READ | EV_WRITE | EV_PERSIST))
        {
            hr = GetLastError();
            hr = FAILED(hr) ? hr : E_UNEXPECTED;
            break;
        }

        break;
    }

    if (vSa)
    {
        FreeSockaddr(&vSa);
    }

    if (FAILED(hr))
    {
        if (m_evbuf)
        {
            bufferevent_free(m_evbuf);
            m_evbuf = nullptr;
        }
        else if (INVALID_SOCKET != s)
        {
            evutil_closesocket(s);
            s = INVALID_SOCKET;
        }
    }

    return hr;
}

HRESULT LibeventWrapper::Listen(
    const char * aIP, short aPort,
    const long aTimeoutSec,
    short aFamily, short aSocktype, short aProtocol)
{
    HRESULT hr = S_OK;

    evutil_socket_t s = INVALID_SOCKET;
    sockaddr *vSa = nullptr;

    for (;;)
    {
        assert(nullptr != m_evbase);
        if (nullptr == m_evbase)
        {
            hr = E_POINTER;
            break;
        }

        int vSalen = 0;
        hr = BuildSockaddr(&vSa, &vSalen, aIP, aPort, aFamily);
        if (FAILED(hr))
        {
            break;
        }

        s = ::socket(aFamily, aSocktype, aProtocol);
        if (INVALID_SOCKET == s)
        {
            hr = GetLastError();
            break;
        }

        // EVUTIL_SOCK_NONBLOCK
        if (NOERROR != evutil_make_socket_nonblocking(s))
        {
            hr = GetLastError();
            break;
        }

        if (aSocktype == SOCK_STREAM || aProtocol == IPPROTO_TCP)
        {
            int vOn = 1;
            if (NOERROR != ::setsockopt(s, SOL_SOCKET, SO_KEEPALIVE,
                reinterpret_cast<const char*>(&vOn), sizeof(vOn)))
            {
                hr = GetLastError();
                break;
            }
        }

        // LEV_OPT_REUSEABLE
        if (NOERROR != evutil_make_listen_socket_reuseable(s))
        {
            hr = GetLastError();
            hr = FAILED(hr) ? hr : E_UNEXPECTED;
            break;
        }

        if (NOERROR != ::bind(s, vSa, vSalen))
        {
            hr = GetLastError();
            break;
        }

        timeval vTv = { aTimeoutSec, 0 };
        if (aSocktype == SOCK_DGRAM || aProtocol == IPPROTO_UDP)
        {
            m_IsUDP = true;

            m_udp = event_new(m_evbase, s, EV_READ | EV_TIMEOUT | EV_PERSIST, OnBaseEvent, (void*)this);
            if ((nullptr == m_udp) ||
                (NOERROR != event_add(m_udp, aTimeoutSec ? &vTv : nullptr)))
            {
                hr = HRESULT_FROM_WIN32(WSA_NOT_ENOUGH_MEMORY);
                break;
            }

            m_UdpSocket = s;

            hr = S_OK;
            break;
        }

        m_listener = evconnlistener_new(m_evbase, OnListener, (void*)this,
            LEV_OPT_CLOSE_ON_FREE | LEV_OPT_THREADSAFE, SOMAXCONN, s);
        if (nullptr == m_listener)
        {
            hr = GetLastError();
            hr = FAILED(hr) ? hr : E_UNEXPECTED;
            break;
        }

        if (aTimeoutSec)
        {
            m_TcpTimeout = event_new(m_evbase, s, EV_TIMEOUT | EV_PERSIST, OnBaseEvent, (void*)this);
            if ((nullptr == m_TcpTimeout) ||
                (NOERROR != event_add(m_TcpTimeout, &vTv)))
            {
                hr = HRESULT_FROM_WIN32(WSA_NOT_ENOUGH_MEMORY);
                break;
            }
        }

        break;
    }

    if (vSa)
    {
        FreeSockaddr(&vSa);
    }

    if (FAILED(hr))
    {
        if (INVALID_SOCKET != s)
        {
            evutil_closesocket(s);
            s = INVALID_SOCKET;
        }
    }

    return hr;
}

HRESULT LibeventWrapper::GetLastError()
{
    return HRESULT_FROM_WIN32(EVUTIL_SOCKET_ERROR());
}

const char * LibeventWrapper::GetLastErrorString(HRESULT hr)
{
    return evutil_socket_error_to_string(static_cast<int>(hr));
}

HRESULT LibeventWrapper::Send(const void * aData, size_t aSize)
{
    if (nullptr == m_evbuf)
    {
        return E_HANDLE;
    }

    return SendTo(reinterpret_cast<evutil_socket_t>(m_evbuf), aData, aSize);
}

HRESULT LibeventWrapper::SendTo(intptr_t fd, const void *aData, size_t aSize)
{
    HRESULT hr = S_OK;

    for (;;)
    {
        if (NOERROR != bufferevent_write(
            reinterpret_cast<bufferevent*>(fd), aData, aSize))
        {
            hr = HRESULT_FROM_WIN32(WSA_NOT_ENOUGH_MEMORY);
            break;
        }

        break;
    }

    return hr;
}

HRESULT LibeventWrapper::SendToForUDP(
    const void * aData, size_t aSize,
    const char *aIP, short aPort, short aFamily)
{
    HRESULT hr = S_OK;
    sockaddr *vTo = nullptr;
    int vTolen = 0;

    for (;;)
    {
        if (m_UdpSocket == 0)
        {
            hr = E_HANDLE;
            break;
        }

        hr = BuildSockaddr(&vTo, &vTolen, aIP, aPort, aFamily);
        if (FAILED(hr))
        {
            break;
        }

        if (SOCKET_ERROR == sendto(
            m_UdpSocket, static_cast<const char*>(aData), aSize, 0, vTo, vTolen))
        {
            hr = GetLastError();
            break;
        }

        break;
    }

    if (vTo)
    {
        FreeSockaddr(&vTo);
        vTo = nullptr;
    }

    return hr;
}

HRESULT LibeventWrapper::SendToForUDP(
    intptr_t fd, const void * aData, size_t aSize,
    sockaddr * aTo, int aTolen)
{
    HRESULT hr = S_OK;

    for (;;)
    {
        if (SOCKET_ERROR == sendto(
            fd, static_cast<const char*>(aData), aSize, 0, aTo, aTolen))
        {
            hr = GetLastError();
            break;
        }

        break;
    }

    return hr;
}

size_t LibeventWrapper::Receive(void * aData, size_t aSize)
{
    if (nullptr == m_evbuf)
    {
        return SOCKET_ERROR;
    }

    return ReceiveFrom(reinterpret_cast<evutil_socket_t>(m_evbuf), aData, aSize);
}

size_t LibeventWrapper::ReceiveFrom(intptr_t fd, void *aData, size_t aSize)
{
    return bufferevent_read(reinterpret_cast<bufferevent*>(fd), aData, aSize);
}

size_t LibeventWrapper::ReceiveFromForUDP(
    intptr_t fd, void * aData, size_t aSize, sockaddr *aFrom, int *aFromlen)
{
    return (size_t)recvfrom(fd, static_cast<char*>(aData), aSize, 0, aFrom, aFromlen);
}

HRESULT LibeventWrapper::SetTimeouts(
    const long aReadTimeoutSec, const long aWriteTimeoutSec)
{
    if (nullptr == m_evbuf)
    {
        return E_HANDLE;
    }

    return SetTimeouts(m_evbuf, aReadTimeoutSec, aReadTimeoutSec);
}

HRESULT LibeventWrapper::SetTimeouts(void * aEvbuf,
    const long aReadTimeoutSec, const long aWriteTimeoutSec)
{
    HRESULT hr = S_OK;

    for (;;)
    {
        timeval vReadtv = { aReadTimeoutSec, 0 };
        timeval vWritetv = { aWriteTimeoutSec, 0 };

        if (NOERROR != bufferevent_set_timeouts(
            static_cast<bufferevent*>(aEvbuf),
            &vReadtv, &vWritetv))
        {
            hr = E_FAIL;
            break;
        }

        break;
    }

    return hr;
}

void *LibeventWrapper::RegisterSignal(int aSig)
{
    event *vSignal = nullptr;

    for (;;)
    {
        assert(nullptr != m_evbase);
        if (nullptr == m_evbase)
        {
            break;
        }

        vSignal = evsignal_new(m_evbase, aSig, OnSignal, (void*)this);
        if (nullptr == vSignal)
        {
            break;
        }

        if (NOERROR != event_add(vSignal, nullptr))
        {
            event_free(vSignal);
            vSignal = nullptr;
            break;
        }

        break;
    }

    return static_cast<void*>(vSignal);
}

void LibeventWrapper::UnregisterSignal(void * aSignal)
{
    assert(nullptr != aSignal);
    if (nullptr == aSignal)
    {
        return;
    }

    event_free(static_cast<event*>(aSignal));
}

HRESULT LibeventWrapper::BuildSockaddr(
    sockaddr **sa, int *salen,
    const char * aIP, short aPort, short aFamily)
{
    HRESULT hr = S_OK;
    NTSTATUS vStatus = NOERROR;

    for (;;)
    {
        if (nullptr == sa || nullptr == salen)
        {
            hr = E_INVALIDARG;
            break;
        }

        if (AF_INET == aFamily)
        {
            sockaddr_in *vSa = new(std::nothrow) sockaddr_in;
            if (nullptr == vSa)
            {
                hr = HRESULT_FROM_WIN32(WSA_NOT_ENOUGH_MEMORY);
                break;
            }
            RtlSecureZeroMemory(vSa, sizeof(sockaddr_in));

            vSa->sin_family = aFamily;
            vSa->sin_port = htons(aPort);

            using RtlIpv4StringToAddressA$Type = LONG(NTAPI*)(PCSTR S, BOOLEAN Strict, PCSTR *Terminator, in_addr *Addr);

            auto vRtlIpv4StringToAddressA = (RtlIpv4StringToAddressA$Type)GetProcAddress(
                GetModuleHandle(L"Ntdll.dll"), "RtlIpv4StringToAddressA");
            if (vRtlIpv4StringToAddressA == nullptr)
            {
                hr = ERROR_NDIS_BAD_VERSION;
                break;
            }

            PCSTR vIPTerminator = nullptr;
            vStatus = vRtlIpv4StringToAddressA(aIP, true, &vIPTerminator,
                &vSa->sin_addr);
            if (!NT_SUCCESS(vStatus))
            {
                HRESULT_FROM_NT(vStatus);
                break;
            }

            *sa = reinterpret_cast<sockaddr*>(vSa);
            *salen = sizeof(sockaddr_in);
        }
        else if (AF_INET6 == aFamily)
        {
            sockaddr_in6 *vSa = new(std::nothrow) sockaddr_in6;
            if (nullptr == vSa)
            {
                hr = HRESULT_FROM_WIN32(WSA_NOT_ENOUGH_MEMORY);
                break;
            }
            RtlSecureZeroMemory(vSa, sizeof(sockaddr_in6));

            vSa->sin6_family = aFamily;
            vSa->sin6_port = htons(aPort);

            using RtlIpv6StringToAddressA$Type = LONG(NTAPI*)(PCSTR S, PCSTR *Terminator, in6_addr *Addr);

            auto vRtlIpv6StringToAddressA = (RtlIpv6StringToAddressA$Type)GetProcAddress(
                GetModuleHandle(L"Ntdll.dll"), "RtlIpv6StringToAddressA");
            if (vRtlIpv6StringToAddressA == nullptr)
            {
                hr = ERROR_NDIS_BAD_VERSION;
                break;
            }

            PCSTR vIPTerminator = nullptr;
            vStatus = vRtlIpv6StringToAddressA(aIP, &vIPTerminator,
                &vSa->sin6_addr);
            if (!NT_SUCCESS(vStatus))
            {
                HRESULT_FROM_NT(vStatus);
                break;
            }

            *sa = reinterpret_cast<sockaddr*>(vSa);
            *salen = sizeof(sockaddr_in6);
        }
        else
        {
            hr = E_NOTIMPL;
            break;
        }

        break;
    }

    return hr;
}

void LibeventWrapper::FreeSockaddr(sockaddr ** sa)
{
    for (;;)
    {
        if (nullptr == sa || nullptr == *sa)
        {
            break;
        }

        if ((*sa)->sa_family == AF_INET)
        {
            delete reinterpret_cast<sockaddr_in *>(*sa);
            *sa = nullptr;
        }
        else if ((*sa)->sa_family == AF_INET6)
        {
            delete reinterpret_cast<sockaddr_in6 *>(*sa);
            *sa = nullptr;
        }
        else
        {
            break;
        }

        break;
    }
}

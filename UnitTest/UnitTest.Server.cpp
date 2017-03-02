#include <signal.h>
#include <WS2tcpip.h>

#include <LibeventWrapper.h>

//////////////////////////////////////////////////////////////////////////

LibeventWrapper g_libevent;
static short SOCKET_TYPE = SOCK_STREAM;

static const char MESSAGE[] = "Hello, World!";

static HRESULT OnRead(intptr_t fd, size_t aNeedReadSize)
{
    static volatile int vCount = 0;

    if (SOCKET_TYPE == SOCK_DGRAM) // UDP
    {
        char vMsg[256] = { 0 };
        sockaddr_in vFrom = { 0 };
        int vFromlen = sizeof(vFrom);
        g_libevent.ReceiveFromForUDP(fd, vMsg, ARRAYSIZE(vMsg),
            (sockaddr*)&vFrom, &vFromlen);

        printf("[%03d] %s \n", vCount++, vMsg);

        g_libevent.SendToForUDP(fd, MESSAGE, ARRAYSIZE(MESSAGE),
            (sockaddr*)&vFrom, vFromlen);
    }
    else
    {
        char *vMsg = new(std::nothrow) char[aNeedReadSize];
        RtlSecureZeroMemory(vMsg, aNeedReadSize);

        g_libevent.ReceiveFrom(fd, vMsg, aNeedReadSize);
        printf("[%03d] %s \n", vCount++, vMsg);

        delete[] vMsg;

        g_libevent.SendTo(fd, MESSAGE, ARRAYSIZE(MESSAGE));
    }

    return S_OK;
}

static HRESULT OnEvent(LibeventWrapper::LIBEVENT_EVENT_ENUM aEvents, intptr_t fd)
{
    if (aEvents & LibeventWrapper::LIBEVENT_EVENT_ENUM::LIBEV_SIGNAL)
    {
        printf("Caught an interrupt signal; exiting cleanly in two seconds.\n");
        g_libevent.StopDispatch(2);
    }
    else if (aEvents & LibeventWrapper::LIBEVENT_EVENT_ENUM::LIBEV_EOF)
    {
        printf("Connection closed.\n");
    }

    if (aEvents & LibeventWrapper::LIBEVENT_EVENT_ENUM::LIBEV_ERROR)
    {
        fprintf(stderr, "Got an error on the connection: %s\n",
            g_libevent.GetLastErrorString(g_libevent.GetLastError()));
    }

    return S_OK;
}

static HRESULT OnListener(intptr_t aSocket, sockaddr *aSockaddr)
{
    char vClientName[NI_MAXHOST] = { 0 };
    GetNameInfoA(aSockaddr, sizeof(sockaddr_in),
        vClientName, NI_MAXHOST, nullptr, 0, NI_NUMERICSERV);

    auto vsa = (sockaddr_in*)aSockaddr;

    printf("Connect: [%s] %d.%d.%d.%d \n", vClientName,
        vsa->sin_addr.S_un.S_un_b.s_b1,
        vsa->sin_addr.S_un.S_un_b.s_b2,
        vsa->sin_addr.S_un.S_un_b.s_b3,
        vsa->sin_addr.S_un.S_un_b.s_b4);

    return S_OK;
}

//////////////////////////////////////////////////////////////////////////

int main(int argc, char *argv[])
{
    HRESULT hr = S_OK;
    LibeventWrapper::LIBEVENT_CALLBACK_PACK vEventPack;

    if (SOCKET_TYPE != SOCK_DGRAM)
    {
        vEventPack.m_IsValidOnListener = TRUE;
        vEventPack.m_OnListener = OnListener;
    }

    vEventPack.m_IsValidOnRead = TRUE;
    vEventPack.m_OnRead = OnRead;

    vEventPack.m_IsValidOnEvent = TRUE;
    vEventPack.m_OnEvent = OnEvent;

    for (;;)
    {
        hr = g_libevent.RegisterCallback(&vEventPack);
        if (FAILED(hr))
        {
            break;
        }

        hr = g_libevent.Initialize(true); // or false
        if (FAILED(hr))
        {
            break;
        }

        hr = g_libevent.Listen("127.0.0.1", 7788, 0, AF_INET, SOCKET_TYPE);
        if (FAILED(hr))
        {
            break;
        }

        void *vSignal = g_libevent.RegisterSignal(SIGINT);
        if (nullptr == vSignal)
        {
            hr = E_FAIL;
            break;
        }

        hr = g_libevent.StartDispatch();
        if (FAILED(hr))
        {
            break;
        }

        g_libevent.UnregisterSignal(vSignal);
        g_libevent.Uninitialize();

        break;
    }

    if (FAILED(hr))
    {
        fprintf(stderr, "The LibeventWrapper.Server has an error: %s \n", 
            LibeventWrapper::GetLastErrorString(hr));
        getchar();
    }

    return hr;
}

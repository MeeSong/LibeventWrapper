#include <LibeventWrapper.h>
#include <WS2tcpip.h>
#include <signal.h>

//////////////////////////////////////////////////////////////////////////

LibeventWrapper g_libevent;
static short SOCKET_TYPE = SOCK_STREAM;
static short FAMILY = AF_INET6;
static char *IP = "::1";
static short PORT = 7788;

static const char MESSAGE[] = "Hello, MeeSong!";
static int MAX_COUNT = 1000;

static HRESULT OnRead(intptr_t fd, size_t aNeedReadSize)
{
    static volatile int vCount = 0;

    if (SOCKET_TYPE == SOCK_DGRAM) // UDP
    {
        char vMsg[256] = { 0 };
        sockaddr *vFrom = { 0 };
        int vFromlen = 0;

        sockaddr_in vFrom4 = { 0 };
        sockaddr_in6 vFrom6 = { 0 };

        if (FAMILY == AF_INET)
        {
            vFrom = (sockaddr *)&vFrom4;
            vFromlen = sizeof(vFrom4);
        }
        else
        {
            vFrom = (sockaddr *)&vFrom6;
            vFromlen = sizeof(vFrom6);
        }

        g_libevent.ReceiveFromForUDP(fd, vMsg, ARRAYSIZE(vMsg), vFrom, &vFromlen);

        printf("[%03d] %s \n", vCount, vMsg);

        if (++vCount < MAX_COUNT)
        {
            g_libevent.SendToForUDP(fd, MESSAGE, ARRAYSIZE(MESSAGE), vFrom, vFromlen);
        }
    }
    else
    {
        char *vMsg = new(std::nothrow) char[aNeedReadSize];
        RtlSecureZeroMemory(vMsg, aNeedReadSize);

        g_libevent.Receive(vMsg, aNeedReadSize);
        printf("[%03d] %s \n", vCount, vMsg);

        delete[] vMsg;

        if (++vCount < MAX_COUNT)
        {
            g_libevent.Send(MESSAGE, ARRAYSIZE(MESSAGE));
        }
    }

    return S_OK;
}

static HRESULT OnEvent(intptr_t fd, LibeventWrapper::LIBEVENT_EVENT_ENUM aEvents)
{
    HRESULT hr = S_OK;

    if (aEvents & LibeventWrapper::LIBEVENT_EVENT_ENUM::LIBEV_CONNECTED)
    {
        if (SOCKET_TYPE == SOCK_DGRAM)
            hr = g_libevent.SendToForUDP(MESSAGE, ARRAYSIZE(MESSAGE), IP, PORT, FAMILY);
        else
            hr = g_libevent.Send(MESSAGE, ARRAYSIZE(MESSAGE));

        return hr;
    }

    if (aEvents & LibeventWrapper::LIBEVENT_EVENT_ENUM::LIBEV_EOF)
    {
        printf("Connection closed.\n");
    }
    else if (aEvents & LibeventWrapper::LIBEVENT_EVENT_ENUM::LIBEV_ERROR)
    {
        hr = g_libevent.GetLastError();

        fprintf(stderr, "Got an error on the connection: %s\n",
            g_libevent.GetLastErrorString(hr));

    }

    if ((hr == HRESULT_FROM_WIN32(WSAECONNREFUSED)) ||
        (hr == HRESULT_FROM_WIN32(WSAENETUNREACH)) ||
        (hr == HRESULT_FROM_WIN32(WSAETIMEDOUT)))
    {
        Sleep(100);
        hr = g_libevent.Connect(IP, PORT, 0, FAMILY, SOCKET_TYPE);

        return hr;
    }

    printf("Caught an interrupt signal; exiting cleanly in two seconds.\n");
    g_libevent.StopDispatch(2);
    return hr;
}

int main(int argc, char *argv[])
{
    HRESULT hr = S_OK;
    LibeventWrapper::LIBEVENT_CALLBACK_PACK vEventPack;

    for (;;)
    {
        vEventPack.m_IsValidOnRead = TRUE;
        vEventPack.m_OnRead = OnRead;

        vEventPack.m_IsValidOnEvent = TRUE;
        vEventPack.m_OnEvent = OnEvent;

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

        hr = g_libevent.Connect(IP, PORT, 0, FAMILY, SOCKET_TYPE);
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

        g_libevent.StartDispatch();

        g_libevent.UnregisterSignal(vSignal);
        g_libevent.Uninitialize();

        break;
    }

    if (FAILED(hr))
    {
        fprintf(stderr, "The LibeventWrapper.Client has an error: %s \n",
            LibeventWrapper::GetLastErrorString(hr));
        getchar();
    }

    return hr;
}

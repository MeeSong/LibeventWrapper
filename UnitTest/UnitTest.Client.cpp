#include <LibeventWrapper.h>
#include <signal.h>

//////////////////////////////////////////////////////////////////////////

LibeventWrapper g_libevent;
static short SOCKET_TYPE = SOCK_STREAM;

static const char MESSAGE[] = "Hello, MeeSong!";
static int MAX_COUNT = 1000;

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

        printf("[%03d] %s \n", vCount, vMsg);

        if (++vCount < MAX_COUNT)
        {
            g_libevent.SendToForUDP(fd, MESSAGE, ARRAYSIZE(MESSAGE),
                (sockaddr*)&vFrom, vFromlen);
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
            g_libevent.Send("Hello MeeSong", ARRAYSIZE("Hello MeeSong"));
        }
    }

    return S_OK;
}

static HRESULT OnEvent(LibeventWrapper::LIBEVENT_EVENT_ENUM aEvents, intptr_t fd)
{
    if (aEvents & LibeventWrapper::LIBEVENT_EVENT_ENUM::LIBEV_EOF)
    {
        printf("Connection closed.\n");
    }
    else if (aEvents & LibeventWrapper::LIBEVENT_EVENT_ENUM::LIBEV_ERROR)
    {
        fprintf(stderr, "Got an error on the connection: %s\n",
            g_libevent.GetLastErrorString(g_libevent.GetLastError()));
    }

    printf("Caught an interrupt signal; exiting cleanly in two seconds.\n");
    g_libevent.StopDispatch(2);

    return S_OK;
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

        hr = g_libevent.Connect("127.0.0.1", 7788, 0, AF_INET, SOCKET_TYPE);
        if (FAILED(hr))
        {
            break;
        }

        if (SOCKET_TYPE == SOCK_DGRAM)
            hr = g_libevent.SendToForUDP(MESSAGE, ARRAYSIZE(MESSAGE), 
                "127.0.0.1", 7788, AF_INET);
        else
            hr = g_libevent.Send(MESSAGE, ARRAYSIZE(MESSAGE));
        
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

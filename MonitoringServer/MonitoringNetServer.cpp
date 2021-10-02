#include "MonitoringNetServer.h"
#include "Protocol.h"
#include "Global.h"
#include "MonitoringServer.h"
#include "MonitorProtocol.h"
#include "PacketProcess.h"


MonitoringNetServer::MonitoringNetServer()
    :NetServer(this)
{

    wcscpy_s(m_BlackIPList[0], L"130.0.0.1");
}

void MonitoringNetServer::ServerMonitorPrint()
{

    wprintf(L"==============================\n");
    wprintf(L" // Accept Count:%lld\n // Accept TPS:%lld\n // Send TPS:%d\n // Recv TPS:%d\n // Session Count:%lu\n // MemoryPoolAlloc:%d \n // LockFreeQ Memory:%d \n // LockFreeStack Memory:%d \n"
        , GetAcceptCount()
        , GetAcceptTPS()
        , GetSendTPS()
        , GetRecvTPS()
        , GetSessionCount()
        , GetMemoryAllocCount()
        , GetSendQMeomryCount()
        , GetLockFreeStackMemoryCount());
    wprintf(L"==============================\n");

    if (GetAsyncKeyState(VK_F9))
    {
        for (int i = 0; i < m_MaxUserCount; ++i)
        {
            wprintf(L"Session[%d] SendQ UsedSize:%d\n", i, m_SessionArray[i]._SendQ.GetQCount());
        }
    }
}

bool MonitoringNetServer::OnConnectionRequest(WCHAR* ip, uint16_t port)
{
    for (int i = 0; i < MAX_IP_COUNT; ++i)
    {
        if (!wcscmp(m_BlackIPList[i], ip))
        {
            return false;
        }
    }
    return true;
}

void MonitoringNetServer::OnClientJoin(uint64_t sessionID, WCHAR* ip, uint16_t port)
{
    Client* newClient = new Client;
    wcscpy_s(newClient->_IP, ip);
    newClient->_Port = port;
    newClient->_SessionID = sessionID;

    m_ClientMapLock.Lock();
    m_ClientMap.insert(std::make_pair(sessionID, newClient));
    m_ClientMapLock.Unlock();

}

void MonitoringNetServer::OnClientLeave(uint64_t sessionID)
{
    //----------------------------------------
    // 게임이라면 유저가떠나면.. 뭔가처리를 해주고 지워준다.
    //----------------------------------------
    m_ClientMapLock.Lock();

    auto iter = m_ClientMap.find(sessionID);
    if (iter == m_ClientMap.end())
    {
        Crash();
    }

    Client* delClient = (*iter).second;

    delete delClient;
    m_ClientMap.erase(iter);
    m_ClientMapLock.Unlock();
}

void MonitoringNetServer::OnRecv(uint64_t sessionID, NetPacket* packet)
{
    PacketProc(sessionID, packet);
    
}

void MonitoringNetServer::OnError(int errorcode, WCHAR* errorMessage)
{
}

void MonitoringNetServer::SentToAllClint(NetPacket* packet)
{
    m_ClientMapLock.Lock();
    
    auto iter = m_ClientMap.begin();

    for (; iter != m_ClientMap.end(); ++iter)
    {
        Client* curClient = (*iter).second;
        packet->IncrementRefCount();
        SendPacket(curClient->_SessionID, packet);
        SendPost(curClient->_SessionID);
    }

    if (packet->DecrementRefCount() == 0)
    {
        packet->Free(packet);
    }

    m_ClientMapLock.Unlock();

}

MonitoringNetServer::Client* MonitoringNetServer::FindClient(uint64_t sessionID)
{
    Client* findClient = nullptr;
    m_ClientMapLock.Lock();

    auto iter = m_ClientMap.find(sessionID);
    if (iter == m_ClientMap.end())
    {
        return nullptr;
    }
    findClient = (*iter).second;
    m_ClientMapLock.Unlock();

    return findClient;
}

void MonitoringNetServer::OnTimeOut(uint64_t sessionID)
{
}


void MonitoringNetServer::PacketProc(uint64_t sessionID, NetPacket* packet)
{
    WORD type;
    if ((*packet).GetPayloadSize() < sizeof(WORD))
    {
        Crash();
    }

    (*packet) >> type;


    switch (type)
    {
    case en_PACKET_CS_MONITOR_TOOL_REQ_LOGIN:

        break;
    default:
#if DISCONLOG_USE ==1
        _LOG->WriteLog(L"ChattingServer", SysLog::eLogLevel::LOG_LEVEL_ERROR, L"메시지마샬링: 존재하지않는 메시지타입 [Session ID:%llu] [Type:%d]", curSession->_ID, type);
#endif
        InterlockedIncrement(&g_FreeMemoryCount);
        packet->Free(packet);
        Disconnect(sessionID);
        break;
    }
}

void MonitoringNetServer::ProcessPacket_en_PACKET_CS_MONITOR_TOOL_REQ_LOGIN(uint64_t sessionID, NetPacket* packet)
{
    Client* curClient = FindClient(sessionID);
    if (curClient == nullptr)
    {
        Crash();
    }

    (*packet).GetData(curClient->_LoginSessionKey, sizeof(char) * 32);

    packet->Clear();
    MakePacket_en_PACKET_CS_MONITOR_TOOL_RES_LOGIN(packet, en_PACKET_CS_MONITOR_TOOL_RES_LOGIN, dfMONITOR_TOOL_LOGIN_OK);
    SendUnicast(sessionID, packet);
}

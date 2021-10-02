#include "MonitoringServer.h"
#include "Protocol.h"
#include "Global.h"
#include "MonitoringServer.h"
#include "MonitorProtocol.h"
#include "MonitoringNetServer.h"
#include "PacketProcess.h"


MyMonitoringLanServer::MyMonitoringLanServer()
    :LanServer(this)
{
    
    wcscpy_s(m_BlackIPList[0], L"130.0.0.1");
}

bool MyMonitoringLanServer::MonitorServerStart(WCHAR* ip, uint16_t port, DWORD runningThread, SocketOption& option, DWORD workerThreadCount, DWORD maxUserCount, TimeOutOption& timeOutOption)
{
    ServerStart(ip, port, runningThread, option, workerThreadCount, maxUserCount, timeOutOption);
    m_NetServerLib = new MonitoringNetServer();
    m_NetServerLib->ServerStart(ip, 8888, 1, option, 1, 10, timeOutOption);

    return true;
}

void MyMonitoringLanServer::ServerMonitorPrint()
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

bool MyMonitoringLanServer::OnConnectionRequest(WCHAR* ip, uint16_t port)
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

void MyMonitoringLanServer::OnClientJoin(uint64_t sessionID, WCHAR* ip, uint16_t port)
{
    Client* newClient = new Client; 
    wcscpy_s(newClient->_IP, ip);
    newClient->_Port = port;
    newClient->_SessionID = sessionID;

    m_ClientMapLock.Lock();
    m_ClientMap.insert(std::make_pair(sessionID, newClient));
    m_ClientMapLock.Unlock();
}

void MyMonitoringLanServer::OnClientLeave(uint64_t sessionID)
{
    //----------------------------------------
    // �����̶�� ������������.. ����ó���� ���ְ� �����ش�.
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

void MyMonitoringLanServer::OnRecv(uint64_t sessionID, LanPacket* packet)
{
    PacketProc(sessionID, packet);
}

void MyMonitoringLanServer::OnError(int errorcode, WCHAR* errorMessage)
{
}

MyMonitoringLanServer::Client* MyMonitoringLanServer::FindClient(uint64_t sessionID)
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

void MyMonitoringLanServer::OnTimeOut(uint64_t sessionID)
{
}


void MyMonitoringLanServer::PacketProc(uint64_t sessionID, LanPacket* packet)
{
    WORD type;
    if ((*packet).GetPayloadSize() < sizeof(WORD))
    {
        Crash();
    }

    (*packet) >> type;

    switch (type)
    {
    case en_PACKET_SS_MONITOR_LOGIN:     

        break;
    case en_PACKET_SS_MONITOR_DATA_UPDATE:
        ProcessPacket_en_PACKET_SS_MONITOR_DATA_UPDATE(sessionID, packet);
        break;
    default:
#if DISCONLOG_USE ==1
        _LOG->WriteLog(L"ChattingServer", SysLog::eLogLevel::LOG_LEVEL_ERROR, L"�޽���������: ���������ʴ� �޽���Ÿ�� [Session ID:%llu] [Type:%d]", curSession->_ID, type);
#endif
        InterlockedIncrement(&g_FreeMemoryCount);
        packet->Free(packet);
        Disconnect(sessionID);
        break;
    }
}

void MyMonitoringLanServer::ProcessPacket_en_PACKET_SS_MONITOR_LOGIN(uint64_t sessionID, LanPacket* packet)
{
    Client* curClient = FindClient(sessionID);
    if (curClient == nullptr)
    {
        Crash();
    }

    (*packet) >> curClient->_ServerType;
    
}

void MyMonitoringLanServer::ProcessPacket_en_PACKET_SS_MONITOR_DATA_UPDATE(uint64_t sessionID, LanPacket* packet)
{
    //------------------------------------------------------------
    // ������ ����͸������� ������ ����
    // �� ������ �ڽ��� ����͸����� ��ġ�� 1�ʸ��� ����͸� ������ ����.
    //
    // ������ �ٿ� �� ��Ÿ ������ ����͸� �����Ͱ� ���޵��� ���ҋ��� ����Ͽ� TimeStamp �� �����Ѵ�.
    // �̴� ����͸� Ŭ���̾�Ʈ���� ���,�� ����Ѵ�.
    // 
    //	{
    //		WORD	Type
    //
    //		BYTE	DataType				// ����͸� ������ Type �ϴ� Define ��.
    //		int		DataValue				// �ش� ������ ��ġ.
    //		int		TimeStamp				// �ش� �����͸� ���� �ð� TIMESTAMP  (time() �Լ�)
    //										// ���� time �Լ��� time_t Ÿ�Ժ����̳� 64bit �� ���񽺷����
    //										// int �� ĳ�����Ͽ� ����. �׷��� 2038�� ������ ��밡��
    //	}
    //
    //------------------------------------------------------------
    Client* curClient = FindClient(sessionID);
    if (curClient == nullptr)
    {
        Crash();
    }

    BYTE dataType;
    int dataValue;
    int timeStamp;

    (*packet) >> dataType >> dataValue >> timeStamp;

    //-----------------------------------------------------
    // LanPacket�� ������ ��������̴� ���Ĵ� NetPacket������
    // ����͸� Ŭ���̾�Ʈ���� ������ �����͸� �������Ѵ�.
    //-----------------------------------------------------
    if (0 == (*packet).DecrementRefCount())
    {
        (*packet).Free(packet);
    }

    NetPacket* netPacket = NetPacket::Alloc();
    MakePacket_en_PACKET_CS_MONITOR_TOOL_DATA_UPDATE(netPacket, en_PACKET_CS_MONITOR_TOOL_DATA_UPDATE, curClient->_SessionID, dataType, dataValue, timeStamp);
    m_NetServerLib->SentToAllClint(netPacket);
}

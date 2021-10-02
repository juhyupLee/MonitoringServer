#include "MonitoringServer.h"
#include "Protocol.h"
#include "Global.h"
#include "MonitoringServer.h"
#include "MonitorProtocol.h"
#include "MonitoringNetServer.h"
#include "PacketProcess.h"
#include <process.h>
#include "DBConnector.h"
MyMonitoringLanServer::MyMonitoringLanServer()
    :LanServer(this)
{
    
    wcscpy_s(m_BlackIPList[0], L"130.0.0.1");
}

unsigned int __stdcall MyMonitoringLanServer::DataSaveToDBThread(LPVOID param)
{
    DWORD curTime = timeGetTime();
    MyMonitoringLanServer* monitorServer = (MyMonitoringLanServer*)param;

    while (true)
    {
        //--------------------------------------------------
        // 5분마다 각 type별 Data를 정산해서 DB에 동기로 보내기
        //--------------------------------------------------
        if (timeGetTime() - curTime> 300000)
        {
            //monitorServer->m_TLSDBCon->Query()
        }
    }
    return 0;
}

bool MyMonitoringLanServer::MonitorServerStart(WCHAR* ip, uint16_t port, DWORD runningThread, SocketOption& option, DWORD workerThreadCount, DWORD maxUserCount, TimeOutOption& timeOutOption)
{
    //--------------------------------------------------
    // DB Connector 생성
    //--------------------------------------------------
    m_TLSDBCon = new TLSDBConnector(SQL_IP, L"3306", L"root", L"tpwhd963", L"accountdb", workerThreadCount);

    m_DataSaveToDBThread = (HANDLE)_beginthreadex(NULL, 0, MyMonitoringLanServer::DataSaveToDBThread, this, 0, NULL);

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
        _LOG->WriteLog(L"ChattingServer", SysLog::eLogLevel::LOG_LEVEL_ERROR, L"메시지마샬링: 존재하지않는 메시지타입 [Session ID:%llu] [Type:%d]", curSession->_ID, type);
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
    // 서버가 모니터링서버로 데이터 전송
    // 각 서버는 자신이 모니터링중인 수치를 1초마다 모니터링 서버로 전송.
    //
    // 서버의 다운 및 기타 이유로 모니터링 데이터가 전달되지 못할떄를 대비하여 TimeStamp 를 전달한다.
    // 이는 모니터링 클라이언트에서 계산,비교 사용한다.
    // 
    //	{
    //		WORD	Type
    //
    //		BYTE	DataType				// 모니터링 데이터 Type 하단 Define 됨.
    //		int		DataValue				// 해당 데이터 수치.
    //		int		TimeStamp				// 해당 데이터를 얻은 시간 TIMESTAMP  (time() 함수)
    //										// 본래 time 함수는 time_t 타입변수이나 64bit 로 낭비스러우니
    //										// int 로 캐스팅하여 전송. 그래서 2038년 까지만 사용가능
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
    // LanPacket의 역할은 여기까지이다 이후는 NetPacket을만들어서
    // 모니터링 클라이언트한테 수집한 데이터를 보내야한다.
    //-----------------------------------------------------
    if (0 == (*packet).DecrementRefCount())
    {
        (*packet).Free(packet);
    }

    NetPacket* netPacket = NetPacket::Alloc();
    MakePacket_en_PACKET_CS_MONITOR_TOOL_DATA_UPDATE(netPacket, en_PACKET_CS_MONITOR_TOOL_DATA_UPDATE, curClient->_SessionID, dataType, dataValue, timeStamp);
    m_NetServerLib->SentToAllClint(netPacket);
}

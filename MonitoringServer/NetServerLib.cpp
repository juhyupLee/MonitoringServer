#include "NetServerLib.h"
#include "Log.h"
#include <WS2tcpip.h>
#include <process.h>
#include "Protocol.h"
#include <locale>
#include "Global.h"
#include <timeapi.h>

NetServer::NetServer(NetServer* contents)
	:m_Contents(contents)
{
	setlocale(LC_ALL, "");

	m_SessionID =0;
	m_ListenSocket = INVALID_SOCKET;
	m_WorkerThread =nullptr;
	m_AcceptThread  = INVALID_HANDLE_VALUE;
	m_MonitoringThread = INVALID_HANDLE_VALUE;

	m_ServerPort = 0;
	m_ServerIP =nullptr;

	m_WorkerThreadCount =0;

	m_IOCP = INVALID_HANDLE_VALUE;
	
	m_RecvTPS =0;
	m_SendTPS =0;
	m_AcceptTPS =0;
	m_AcceptCount =0;

	m_SessionArray = nullptr;
	m_WorkerThread = nullptr;
	m_MaxUserCount = 0;
	m_MonitorEvent = INVALID_HANDLE_VALUE;

	m_SessionCount = 0;
	m_SendFlagNo = -1;


	m_RecvTPS_To_Main = 0;
	m_SendTPS_To_Main=0;
	m_AcceptTPS_To_Main=0;

	m_SendQMemory = 0;
	m_IndexStack = nullptr;


	m_NetworkTraffic = 0;

}

NetServer::~NetServer()
{
	if (!m_SessionArray)
	{
		delete[] m_SessionArray;
	}
	if (!m_WorkerThread)
	{
		delete[] m_WorkerThread;
	}
}

bool NetServer::ServerStart(WCHAR* ip, uint16_t port, DWORD runningThread, SocketOption& option, DWORD workerThreadCount,DWORD maxUserCount, TimeOutOption& timeOutOption)
{
	timeBeginPeriod(1);

	if (!NetworkInit(ip, port, runningThread, option))
	{
		_LOG->WriteLog(L"ChattingServer", SysLog::eLogLevel::LOG_LEVEL_ERROR, L"NetworkInit Fail");
	}

	//-----------------------------------------------------
	// 세션의 Index를 관리할 락프리 스택 생성 
	// ServerStop시, delete
	//-----------------------------------------------------
	m_TimeOutOption = timeOutOption;
	m_IndexStack = new LockFreeStack<uint64_t>;

	m_SessionArray  = new Session[maxUserCount];
	m_MaxUserCount = maxUserCount;
	for (uint64_t index = 0; index < m_MaxUserCount; ++index)
	{
		m_IndexStack->Push(index);
	}

	EventInit();

	ThreadInit(workerThreadCount);

	if (0 != listen(m_ListenSocket, SOMAXCONN))
	{
		_LOG->WriteLog(L"ChattingServer", SysLog::eLogLevel::LOG_LEVEL_ERROR, L"listen() error:%d", WSAGetLastError());
		return false;
	}
	_LOG->WriteLog(L"ChattingServer", SysLog::eLogLevel::LOG_LEVEL_SYSTEM, L"IOCP Echo Server Listen......");
    return true;
}

void NetServer::ServerStop()
{
	timeEndPeriod(1);

	closesocket(m_ListenSocket);
	DWORD rtn = WaitForSingleObject(m_AcceptThread, INFINITE);

	SetEvent(m_MonitorEvent);
	WaitForSingleObject(m_MonitoringThread, INFINITE);

	if (DisconnectAllUser())
	{
		for (size_t i = 0; i < m_WorkerThreadCount; ++i)
		{
			PostQueuedCompletionStatus(m_IOCP, 0, NULL, NULL);
			CloseHandle(m_WorkerThread[i]);
		}
		WaitForMultipleObjects(m_WorkerThreadCount, m_WorkerThread, TRUE, INFINITE);

		CloseHandle(m_MonitoringThread);
	}
	

	_LOG->WriteLog(L"ChattingServer", SysLog::eLogLevel::LOG_LEVEL_SYSTEM, L"남은세션:%d", m_SessionCount);
	_LOG->WriteLog(L"ChattingServer", SysLog::eLogLevel::LOG_LEVEL_SYSTEM, L"서버 종료");



	delete m_IndexStack;
	m_IndexStack = nullptr;

	m_AcceptCount = 0;
	m_AcceptTPS = 0;
	m_RecvTPS = 0;
	m_SendTPS = 0;
	m_SendQMemory = 0;
	m_AcceptTPS_To_Main = 0;
	m_SendTPS_To_Main = 0;
	m_RecvTPS_To_Main = 0;
	
}

 int64_t NetServer::GetAcceptCount()
{
	return m_AcceptCount;
}

 LONG NetServer::GetAcceptTPS()
{
	return m_AcceptTPS_To_Main;
}

 LONG NetServer::GetSendTPS()
{
	return m_SendTPS_To_Main;
}

 LONG NetServer::GetRecvTPS()
{
	return m_RecvTPS_To_Main;
}

LONG NetServer::GetNetworkTraffic()
{
	return m_NetworkTraffic_To_Main;
}


 LONG NetServer::GetSessionCount()
{
	return m_SessionCount;
}

 int32_t NetServer::GetMemoryAllocCount()
 {
	 return NetPacket::GetMemoryPoolAllocCount();
 }

 LONG NetServer::GetSendQMeomryCount()
 {
	 return m_SendQMemory;
 }

 LONG NetServer::GetLockFreeStackMemoryCount()
 {
	 if (m_IndexStack == nullptr)
	 {
		 return 0;
	 }
	 return m_IndexStack->GetMemoryAllocCount();
 }

bool NetServer::Disconnect(uint64_t sessionID)
{
	Session* curSession = FindSession(sessionID);
	
#if MEMORYLOG_USE ==1 
	IOCP_Log log;
	log.DataSettiong(InterlockedIncrement64(&g_IOCPMemoryNo), eIOCP_LINE::DISCONNECT_CLIENT, GetCurrentThreadId(), curSession->_Socket, curSession->_IOCount, (int64_t)curSession, sessionID, (int64_t)&curSession->_RecvOL, (int64_t)&curSession->_SendOL, curSession->_SendFlag);
	g_MemoryLog_IOCP.MemoryLogging(log);
#endif

#if MEMORYLOG_USE  ==2
	IOCP_Log log;
	log.DataSettiong(InterlockedIncrement64(&g_IOCPMemoryNo), eIOCP_LINE::DISCONNECT_CLIENT, GetCurrentThreadId(), curSession->_Socket, curSession->_IOCount, (int64_t)curSession, sessionID, (int64_t)&curSession->_RecvOL, (int64_t)&curSession->_SendOL, curSession->_SendFlag);
	g_MemoryLog_IOCP.MemoryLogging(log);
	curSession->_MemoryLog_IOCP.MemoryLogging(log);
#endif

	if (AcquireSession(sessionID))
	{
		InterlockedIncrement(&g_DisconnectCount);
		Session* findSession = FindSession(sessionID);
		if (findSession == nullptr)
		{
			Crash();
			return false;
		}
		findSession->_bIOCancel = true;
		IO_Cancel(findSession);

		if (0 == InterlockedDecrement(&findSession->_IOCount))
		{
			ReleaseSession(findSession);
		}
	}
    return true;
}

void NetServer::SetTimeOut(uint64_t sessionID)
{
	Session* findSession = nullptr;

	if (AcquireSession(sessionID,&findSession))
	{
	
		findSession->_TimeOut = m_TimeOutOption._HeartBeatTimeOut;

		if (0 == InterlockedDecrement(&findSession->_IOCount))
		{
			ReleaseSession(findSession);
		}
	}
}

void NetServer::SetTimeOut(uint64_t sessionID,DWORD timeOut)
{
	Session* findSession = nullptr;
	if (AcquireSession(sessionID, &findSession))
	{
		findSession->_TimeOut = timeOut;

		if (0 == InterlockedDecrement(&findSession->_IOCount))
		{
			ReleaseSession(findSession);
		}
	}
}

void NetServer::IO_Cancel(Session* curSession)
{
	//-----------------------------------------------
	// Overlapped Pointer가 NULL일시 Send ,Recv 둘다 IO취소한다
	//-----------------------------------------------
	CancelIoEx((HANDLE)curSession->_Socket, NULL);
}

bool NetServer::SendPacket(uint64_t sessionID, NetPacket* packet)
{
	Session* curSession = nullptr;

	if (!AcquireSession(sessionID, &curSession))
	{
		//---------------------------------------
		// AcqurieSession에 실패했다면 저 패킷에대한 RefCount를 감소시켜야된다.
		//---------------------------------------
		if (packet->DecrementRefCount() == 0)
		{
			InterlockedIncrement(&g_FreeMemoryCount);
			packet->Free(packet);
		}
		return false;
	}
	

#if MEMORYLOG_USE ==1 ||   MEMORYLOG_USE ==2
	IOCP_Log log;
#endif

#if MEMORYLOG_USE ==1 
	log.DataSettiong(InterlockedIncrement64(&g_IOCPMemoryNo), eIOCP_LINE::SENDPACKET, GetCurrentThreadId(), curSession->_Socket, curSession->_IOCount, (int64_t)curSession, curSession->_ID, (int64_t)&curSession->_RecvOL, (int64_t)&curSession->_SendOL, curSession->_SendFlag);
	g_MemoryLog_IOCP.MemoryLogging(log);
#endif

#if MEMORYLOG_USE  ==2
	log.DataSettiong(InterlockedIncrement64(&g_IOCPMemoryNo), eIOCP_LINE::SENDPACKET, GetCurrentThreadId(), curSession->_Socket, curSession->_IOCount, (int64_t)curSession, curSession->_ID, (int64_t)&curSession->_RecvOL, (int64_t)&curSession->_SendOL, curSession->_SendFlag);
	g_MemoryLog_IOCP.MemoryLogging(log);
	curSession->_MemoryLog_IOCP.MemoryLogging(log);
#endif


	(*packet).HeaderSettingAndEncoding();

	if (packet->GetPayloadSize() <= 0)
	{
		Crash();
	}
	if (!curSession->_SendQ.EnQ(packet))
	{
		_LOG->WriteLog(L"ChattingServer", SysLog::eLogLevel::LOG_LEVEL_ERROR, L"SendQ 총갯수 초과(LockFreeQ Qcount 초과함)");
		Crash();
	}

	if (0 == InterlockedDecrement(&curSession->_IOCount))
	{
		ReleaseSession(curSession);
	}

    return true;
}


#if SMART_PACKET_USE== 1
bool netServer::SendPacket(uint64_t sessionID, SmartNetPacket packet)
{
	Session* curSession = FindSession(sessionID);
	if (curSession == nullptr)
	{
		Crash();
	}

	LanHeader header;
	header._Size = (*packet).GetPayloadSize();
	(*packet).SetHeader(&header);
	

	if (!curSession->_SendQ.EnQ(packet))
	{
		_LOG(LOG_LEVEL_ERROR, L"SendQ 총갯수 초과(LockFreeQ Qcount 초과함)");
		Crash();
	}

 	InterlockedIncrement(&m_SendTPS);

	return true;
}
#endif

bool NetServer::NetworkInit(WCHAR* ip, uint16_t port, DWORD runningThread, SocketOption option)
{
	WSAData wsaData;
	SOCKADDR_IN serverAddr;

	if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0)
	{
		_LOG->WriteLog(L"ChattingServer", SysLog::eLogLevel::LOG_LEVEL_ERROR, L"WSAStartUp() Error:%d", WSAGetLastError());
		return false;
	}
	m_IOCP = CreateIoCompletionPort(INVALID_HANDLE_VALUE, NULL, NULL, runningThread);

	m_ListenSocket = socket(AF_INET, SOCK_STREAM, 0);

	if (m_ListenSocket == INVALID_SOCKET)
	{
		_LOG->WriteLog(L"ChattingServer", SysLog::eLogLevel::LOG_LEVEL_ERROR, L"socket() error:%d", WSAGetLastError());
		return false;
	}

	if (option._SendBufferZero)
	{
		//----------------------------------------------------------------------------
		// 송신버퍼 Zero -->비동기 IO 유도
		//----------------------------------------------------------------------------
		int optVal = 0;
		int optLen = sizeof(optVal);

		int rtnOpt = setsockopt(m_ListenSocket, SOL_SOCKET, SO_SNDBUF, (const char*)&optVal, optLen);
		if (rtnOpt != 0)
		{
			_LOG->WriteLog(L"ChattingServer", SysLog::eLogLevel::LOG_LEVEL_ERROR, L"setsockopt() error:%d", WSAGetLastError());
		}
	}
	if (option._Linger)
	{
		linger lingerOpt;
		lingerOpt.l_onoff = 1;
		lingerOpt.l_linger = 0;

		int rtnOpt = setsockopt(m_ListenSocket, SOL_SOCKET, SO_LINGER, (const char*)&lingerOpt, sizeof(lingerOpt));
		if (rtnOpt != 0)
		{
			_LOG->WriteLog(L"ChattingServer", SysLog::eLogLevel::LOG_LEVEL_ERROR, L"setsockopt() error:%d", WSAGetLastError());
		}

	}
	if (option._TCPNoDelay)
	{
		BOOL tcpNodelayOpt = true;

		int rtnOpt = setsockopt(m_ListenSocket, IPPROTO_TCP, TCP_NODELAY, (const char*)&tcpNodelayOpt, sizeof(tcpNodelayOpt));
		if (rtnOpt != 0)
		{
			_LOG->WriteLog(L"ChattingServer", SysLog::eLogLevel::LOG_LEVEL_ERROR, L"setsockopt() error:%d", WSAGetLastError());
		}
	}

	if (option._KeepAliveOption.onoff)
	{
		DWORD recvByte = 0;

		if (0 != WSAIoctl(m_ListenSocket, SIO_KEEPALIVE_VALS, &option._KeepAliveOption, sizeof(tcp_keepalive), NULL, 0, &recvByte, NULL, NULL))
		{
			_LOG->WriteLog(L"ChattingServer", SysLog::eLogLevel::LOG_LEVEL_ERROR, L"setsockopt() error:%d", WSAGetLastError());
		}
	}
	ZeroMemory(&serverAddr, sizeof(SOCKADDR_IN));
	serverAddr.sin_family = AF_INET;
	serverAddr.sin_port = htons(port);

	if (ip == nullptr)
	{
		serverAddr.sin_addr.S_un.S_addr = htonl(INADDR_ANY);
	}
	else
	{
		InetPton(AF_INET, ip, &serverAddr.sin_addr.S_un.S_addr);
	}

	if (0 != bind(m_ListenSocket, (sockaddr*)&serverAddr, sizeof(serverAddr)))
	{
		_LOG->WriteLog(L"ChattingServer", SysLog::eLogLevel::LOG_LEVEL_ERROR, L"bind() error:%d", WSAGetLastError());
		return false;
	}

	return true;
}

bool NetServer::ThreadInit(DWORD workerThreadCount)
{
	m_WorkerThreadCount = workerThreadCount;
	m_WorkerThread = new HANDLE[workerThreadCount];

	for (size_t i= 0; i < m_WorkerThreadCount; ++i)
	{
		m_WorkerThread[i] = (HANDLE)_beginthreadex(NULL, 0, NetServer::WorkerThread, this, 0, NULL);
	}

	m_AcceptThread = (HANDLE)_beginthreadex(NULL, 0, NetServer::AcceptThread, this, 0, NULL);
	m_MonitoringThread = (HANDLE)_beginthreadex(NULL, 0, NetServer::MonitorThread, this, 0, NULL);
	
	return true;
}

bool NetServer::EventInit()
{
	m_MonitorEvent = CreateEvent(NULL, TRUE, FALSE, NULL);

	return true;
}

void NetServer::Crash()
{
	int* temp = nullptr;
	*temp = 10;

}

unsigned  __stdcall NetServer::AcceptThread(LPVOID param)
{

	NetServer* netServer = (NetServer*)param;

	while (true)
	{
		SOCKADDR_IN clientAddr;
		ZeroMemory(&clientAddr, sizeof(clientAddr));
		int addrLen = sizeof(clientAddr);
		SOCKET clientSocket;

		clientSocket = accept(netServer->m_ListenSocket, (sockaddr*)&clientAddr, &addrLen);
		if (clientSocket == INVALID_SOCKET)
		{
			_LOG->WriteLog(L"ChattingServer", SysLog::eLogLevel::LOG_LEVEL_ERROR, L"accept () error:%d", WSAGetLastError());
			break;
		}
		netServer->m_AcceptCount++;

		WCHAR tempIP[17] = { 0, };
		uint16_t tempPort = ntohs(clientAddr.sin_port);
		InetNtop(AF_INET, &clientAddr.sin_addr.S_un.S_addr, tempIP, 17);

		//----------------------------------------------------//
		//Black IP 차단 및 White IP 세션생성 
		//----------------------------------------------------//
		if (!netServer->m_Contents->OnConnectionRequest(tempIP, tempPort))
		{
			closesocket(clientSocket);
			continue;
		}
		netServer->AcceptUser(clientSocket, tempIP,tempPort);

		netServer->m_AcceptTPS++;

	}

	_LOG->WriteLog(L"ChattingServer", SysLog::eLogLevel::LOG_LEVEL_SYSTEM, L"Accpt Thread[%d] 종료", GetCurrentThreadId());
	return 0;
}

unsigned  __stdcall NetServer::WorkerThread(LPVOID param)
{ 
	NetServer* netServer = (NetServer*)param;

	while (true)
	{
		DWORD transferByte = 0;
		OVERLAPPED* curOverlap = nullptr;
		Session* curSession = nullptr;
		BOOL gqcsRtn = GetQueuedCompletionStatus(netServer->m_IOCP, &transferByte, (PULONG_PTR)&curSession, (LPOVERLAPPED*)&curOverlap, INFINITE);

		int errorCode;

		if (gqcsRtn == FALSE)
		{
			errorCode = WSAGetLastError();
			if (errorCode == ERROR_OPERATION_ABORTED)
			{
				//netServer->Crash();

				//wprintf(L"ErrorCode : %d\n", errorCode);
			}
			curSession->_ErrorCode = errorCode;
		}
		
#if MEMORYLOG_USE ==1 ||   MEMORYLOG_USE ==2
		IOCP_Log log;
#endif

		//SendFlag_Log sendFlagLog;

		//----------------------------------------------
		// GQCS에서 나온 오버랩이 nullptr이면, 타임아웃, IOCP자체가 에러 , 아니면 postQ로 NULL을 넣었을때이다
		//----------------------------------------------
		if (curOverlap == NULL && curSession == NULL && transferByte == 0)
		{
			break;
		}
		//-----------------------------------------------------------------
		// 외부스레드에서 Send할때, PQCS(Transfer=-1, SessionID) 옴
		// 이때 SendPost하고 Continue;
		//-----------------------------------------------------------------
		if (transferByte == -1 && curSession==NULL)
		{
			curSession = netServer->FindSession((uint64_t)curOverlap);
#if MEMORYLOG_USE ==1 
			log.DataSettiong(InterlockedIncrement64(&g_IOCPMemoryNo), eIOCP_LINE::GQCS, GetCurrentThreadId(), curSession->_Socket, curSession->_IOCount, (int64_t)curSession, curSession->_ID, (int64_t)&curSession->_RecvOL, (int64_t)&curSession->_SendOL, curSession->_SendFlag);
			g_MemoryLog_IOCP.MemoryLogging(log);
#endif

#if MEMORYLOG_USE  ==2
			
			log.DataSettiong(InterlockedIncrement64(&g_IOCPMemoryNo), eIOCP_LINE::PQCS_SENDPOST, GetCurrentThreadId(), curSession->_Socket, curSession->_IOCount, (int64_t)curSession, curSession->_ID, (int64_t)&curSession->_RecvOL, (int64_t)&curSession->_SendOL, curSession->_SendFlag);
			g_MemoryLog_IOCP.MemoryLogging(log);
			curSession->_MemoryLog_IOCP.MemoryLogging(log);
#endif

			netServer->SendPost((uint64_t)curOverlap);
			continue;
		}
		do
		{
			
			if (curSession == nullptr)
			{
				netServer->Crash();
			}
			if (curSession->_IOCount == 0)
			{
				netServer->Crash();
			}
			curSession->_GQCSRtn = gqcsRtn;
#if MEMORYLOG_USE ==1 
			log.DataSettiong(InterlockedIncrement64(&g_IOCPMemoryNo), eIOCP_LINE::GQCS, GetCurrentThreadId(), curSession->_Socket, curSession->_IOCount, (int64_t)curSession, curSession->_ID, (int64_t)&curSession->_RecvOL, (int64_t)&curSession->_SendOL, curSession->_SendFlag);
			g_MemoryLog_IOCP.MemoryLogging(log);
#endif

#if MEMORYLOG_USE  ==2
			log.DataSettiong(InterlockedIncrement64(&g_IOCPMemoryNo), eIOCP_LINE::GQCS, GetCurrentThreadId(), curSession->_Socket, curSession->_IOCount, (int64_t)curSession, curSession->_ID, (int64_t)&curSession->_RecvOL, (int64_t)&curSession->_SendOL, curSession->_SendFlag);
			g_MemoryLog_IOCP.MemoryLogging(log);
			curSession->_MemoryLog_IOCP.MemoryLogging(log);
#endif


		/*	sendFlagLog.DataSettiong(InterlockedIncrement64(&netServer->m_SendFlagNo), eSendFlag::GQCS_SENDFLAG, GetCurrentThreadId(), curSession->_Socket, (int64_t)curSession, (int64_t)&curSession->_RecvOL, (int64_t)&curSession->_SendOL, curSession->_SendFlag, curSession->_SendQ.GetQCount());
			curSession->_MemoryLog_SendFlag.MemoryLogging(sendFlagLog);
		*/

			uint64_t sessionID = curSession->_ID;

			if (transferByte == 0)
			{
				if (curOverlap == &curSession->_RecvOL)
				{

					curSession->_TransferZero = 5;
					
					//_LOG(LOG_LEVEL_ERROR, L"TransferByZero Recv  ErrorCode:%d, GQCS Rtn:%d RecvRingQ Size:%d, SendQRingQSize :%d",curSession->_ErrorCode, gqcsRtn, curSession->_RecvRingQ.GetUsedSize(),curSession->_SendQ.m_Count);
#if MEMORYLOG_USE ==1 
					log.DataSettiong(InterlockedIncrement64(&g_IOCPMemoryNo), eIOCP_LINE::TRANSFER_ZERO_RECV, GetCurrentThreadId(), curSession->_Socket, curSession->_IOCount, (int64_t)curSession, sessionID, (int64_t)&curSession->_RecvOL, (int64_t)&curSession->_SendOL, curSession->_SendFlag);
					g_MemoryLog_IOCP.MemoryLogging(log);
#endif

#if MEMORYLOG_USE  ==2
					log.DataSettiong(InterlockedIncrement64(&g_IOCPMemoryNo), eIOCP_LINE::TRANSFER_ZERO_RECV, GetCurrentThreadId(), curSession->_Socket, curSession->_IOCount, (int64_t)curSession, curSession->_ID, (int64_t)&curSession->_RecvOL, (int64_t)&curSession->_SendOL, curSession->_SendFlag);
					g_MemoryLog_IOCP.MemoryLogging(log);
					curSession->_MemoryLog_IOCP.MemoryLogging(log);
#endif
				}
				else if (curOverlap == &curSession->_SendOL)
				{
					if (gqcsRtn == TRUE)
					{
						netServer->Crash();
					}
					curSession->_TransferZero = 6;
#if MEMORYLOG_USE ==1 
					log.DataSettiong(InterlockedIncrement64(&g_IOCPMemoryNo), eIOCP_LINE::TRANSFER_ZERO_SEND, GetCurrentThreadId(), curSession->_Socket, curSession->_IOCount, (int64_t)curSession, sessionID, (int64_t)&curSession->_RecvOL, (int64_t)&curSession->_SendOL, curSession->_SendFlag);
					g_MemoryLog_IOCP.MemoryLogging(log);
#endif

#if MEMORYLOG_USE  ==2
					log.DataSettiong(InterlockedIncrement64(&g_IOCPMemoryNo), eIOCP_LINE::TRANSFER_ZERO_SEND, GetCurrentThreadId(), curSession->_Socket, curSession->_IOCount, (int64_t)curSession, curSession->_ID, (int64_t)&curSession->_RecvOL, (int64_t)&curSession->_SendOL, curSession->_SendFlag);
					g_MemoryLog_IOCP.MemoryLogging(log);
					curSession->_MemoryLog_IOCP.MemoryLogging(log);
#endif
				}
				//----------------------------------------------
				// 작업 실패시 close socket을 해준다
				//----------------------------------------------
				break;
			}
			
			if (curOverlap == &curSession->_RecvOL)
			{

#if MEMORYLOG_USE ==1 
				log.DataSettiong(InterlockedIncrement64(&g_IOCPMemoryNo), eIOCP_LINE::RECV_COMPLETE, GetCurrentThreadId(), curSession->_Socket, curSession->_IOCount, (int64_t)curSession, sessionID, (int64_t)&curSession->_RecvOL, (int64_t)&curSession->_SendOL, curSession->_SendFlag);
				g_MemoryLog_IOCP.MemoryLogging(log);
#endif

#if MEMORYLOG_USE  ==2
				log.DataSettiong(InterlockedIncrement64(&g_IOCPMemoryNo), eIOCP_LINE::RECV_COMPLETE, GetCurrentThreadId(), curSession->_Socket, curSession->_IOCount, (int64_t)curSession, curSession->_ID, (int64_t)&curSession->_RecvOL, (int64_t)&curSession->_SendOL, curSession->_SendFlag);
				g_MemoryLog_IOCP.MemoryLogging(log);
				curSession->_MemoryLog_IOCP.MemoryLogging(log);
#endif

				if (curSession->_IOCount <= 0)
				{
					netServer->Crash();
				}
				netServer->RecvPacket(curSession, transferByte);
			}
			else if (curOverlap == &curSession->_SendOL)
			{
				/*		if (curSession->_SendFlag != 1)
						{
							Crash();
						}*/


#if MEMORYLOG_USE ==1 
				log.DataSettiong(InterlockedIncrement64(&g_IOCPMemoryNo), eIOCP_LINE::SEND_COMPLETE, GetCurrentThreadId(), curSession->_Socket, curSession->_IOCount, (int64_t)curSession, sessionID, (int64_t)&curSession->_RecvOL, (int64_t)&curSession->_SendOL, curSession->_SendFlag);
				g_MemoryLog_IOCP.MemoryLogging(log);
#endif

#if MEMORYLOG_USE  ==2
				log.DataSettiong(InterlockedIncrement64(&g_IOCPMemoryNo), eIOCP_LINE::SEND_COMPLETE, GetCurrentThreadId(), curSession->_Socket, curSession->_IOCount, (int64_t)curSession, curSession->_ID, (int64_t)&curSession->_RecvOL, (int64_t)&curSession->_SendOL, curSession->_SendFlag);
				g_MemoryLog_IOCP.MemoryLogging(log);
				curSession->_MemoryLog_IOCP.MemoryLogging(log);
#endif
				if (curSession->_IOCount <= 0)
				{
					netServer->Crash();
				}

				if (curSession->_SendByte != transferByte)
				{
					netServer->Crash();
				}

				netServer->DeQPacket(curSession);
				curSession->_SendByte = 0;

				////-------------------------------------------------------
				//// send 완료통지가 왔기때문에 SendFlag를 바꿔준다. 그리고 혹시 그사이에 SendQ에 쌓여있을지모르니 Send Post를 한다
				////-------------------------------------------------------
				InterlockedExchange(&curSession->_SendFlag, 0);
				//g_MemoryLog_SendFlag.MemoryLogging(SEND_COMPLETE_AFTER, GetCurrentThreadId(), curSession->_ID, curSession->_SendFlag, curSession->_SendRingQ.GetUsedSize(),(int64_t)curSession->_Socket, (int64_t)curSession, (int64_t)&curSession->_RecvOL, (int64_t)&curSession->_SendOL);
				//sendFlagLog.DataSettiong(InterlockedIncrement64(&netServer->m_SendFlagNo), eSendFlag::SEND_COMPLETE_AFTER, GetCurrentThreadId(), curSession->_Socket, (int64_t)curSession, (int64_t)&curSession->_RecvOL, (int64_t)&curSession->_SendOL, curSession->_SendFlag, curSession->_SendQ.GetQCount());

				//curSession->_MemoryLog_SendFlag.MemoryLogging(sendFlagLog);
				netServer->SendPost(curSession->_ID);

				//netServer->m_NetworkTraffic

				InterlockedAdd(&netServer->m_NetworkTraffic, transferByte + 40);


			}
			else
			{
				// 오버랩 들어온거비교
				netServer->m_MemoryLog_Overlap.MemoryLogging(ELSE_OVERLAP, GetCurrentThreadId(), curSession->_ID, (int64_t)&curSession->_RecvOL, (int64_t)&curSession->_SendOL, (int64_t)curOverlap, (int64_t)curSession->_Socket, (int64_t)curSession);
				netServer->Crash();
			}

		} while (0);


		if (curSession->_IOCount <= 0)
		{
			netServer->Crash();
		}

		//-------------------------------------------------------------
		// 완료통지로 인한 IO 차감
		//-------------------------------------------------------------

#if MEMORYLOG_USE ==1 
		log.DataSettiong(InterlockedIncrement64(&g_IOCPMemoryNo), eIOCP_LINE::LAST_COMPLETE, GetCurrentThreadId(), curSession->_Socket, curSession->_IOCount, (int64_t)curSession, curSession->_ID, (int64_t)&curSession->_RecvOL, (int64_t)&curSession->_SendOL, curSession->_SendFlag);
		g_MemoryLog_IOCP.MemoryLogging(log);
#endif

#if MEMORYLOG_USE  ==2
		log.DataSettiong(InterlockedIncrement64(&g_IOCPMemoryNo), eIOCP_LINE::LAST_COMPLETE, GetCurrentThreadId(), curSession->_Socket, curSession->_IOCount, (int64_t)curSession, curSession->_ID, (int64_t)&curSession->_RecvOL, (int64_t)&curSession->_SendOL, curSession->_SendFlag);
		g_MemoryLog_IOCP.MemoryLogging(log);
		curSession->_MemoryLog_IOCP.MemoryLogging(log);
#endif

		int tempIOCount = InterlockedDecrement(&curSession->_IOCount);
#if IOCOUNT_CHECK ==1
		InterlockedIncrement(&g_IOCompleteCount);
		InterlockedDecrement(&g_IOIncDecCount);
#endif
		if (0 == tempIOCount)
		{
			netServer->ReleaseSession(curSession);
		}
	/*	else if (tempIOCount < 0)
		{
			netServer->Crash();
		}*/

	}
	_LOG->WriteLog(L"ChattingServer", SysLog::eLogLevel::LOG_LEVEL_SYSTEM, L"WorkerThread[%d] 종료", GetCurrentThreadId());
	
	return 0;
}

unsigned int __stdcall NetServer::MonitorThread(LPVOID param)
{

	NetServer* netServer = (NetServer*)param;

	while (true)
	{
		DWORD rtn = WaitForSingleObject(netServer->m_MonitorEvent, 999);
		if (rtn != WAIT_TIMEOUT)
		{
			break;
		}

		//-------------------------------------------------------------
		// 모든 세션의 락프리큐 노드 합산
		//-------------------------------------------------------------
		netServer->m_SendQMemory = 0;

		for (DWORD i = 0; i < netServer->m_MaxUserCount; ++i)
		{
			Session* curSession = &netServer->m_SessionArray[i];

			netServer->m_SendQMemory += curSession->_SendQ.GetMemoryPoolAllocCount();
		}

		if (netServer->m_TimeOutOption._OptionOn)
		{
			for (DWORD i = 0; i < netServer->m_MaxUserCount; ++i)
			{
				Session* curSession = &netServer->m_SessionArray[i];

				if (curSession->_USED)
				{
					if (netServer->AcquireSession(curSession->_ID))
					{
						if (timeGetTime() - curSession->_LastRecvTime > curSession->_TimeOut)
						{
							netServer->OnTimeOut(curSession->_ID);
						}
						if (0 == InterlockedDecrement(&curSession->_IOCount))
						{
							netServer->ReleaseSession(curSession);
						}
					}

				}
			}
		}

#if IOCOUNT_CHECK ==1
		wprintf(L"IO Post Count:%d\n", g_IOPostCount);
		wprintf(L"IO IncDec Count:%d\n", g_IOIncDecCount);
		wprintf(L"IO Compelete Count:%d\n", g_IOCompleteCount);
		wprintf(L"IO Sum Count:%d\n", g_IOPostCount - (g_IOIncDecCount+ g_IOCompleteCount));
#endif
		netServer->m_RecvTPS_To_Main = netServer->m_RecvTPS;
		netServer->m_SendTPS_To_Main = netServer->m_SendTPS;
		netServer->m_AcceptTPS_To_Main = netServer->m_AcceptTPS;
		netServer->m_NetworkTraffic_To_Main = netServer->m_NetworkTraffic;

		InterlockedExchange(&netServer->m_RecvTPS, 0);
		InterlockedExchange(&netServer->m_SendTPS, 0);
		InterlockedExchange(&netServer->m_AcceptTPS, 0);
		InterlockedExchange(&netServer->m_NetworkTraffic, 0);

	}
	
	_LOG->WriteLog(L"ChattingServer", SysLog::eLogLevel::LOG_LEVEL_SYSTEM, L"MonitorThread 종료");
	
	return 0;
}

void NetServer::ReleaseSession(Session * delSession)
{
	if (0 != InterlockedCompareExchange(&delSession->_IOCount, 0x80000000, 0))
	{
		return;
	}

	if (delSession == nullptr)
	{
		_LOG->WriteLog(L"ChattingServer", SysLog::eLogLevel::LOG_LEVEL_ERROR, L"delte Session이 널이다");
		return;
	}

	uint64_t tempSessionID = delSession->_ID;

	if (delSession->_ErrorCode == ERROR_SEM_TIMEOUT)
	{
		InterlockedIncrement(&g_TCPTimeoutReleaseCnt);
	}

	//---------------------------------------
	// CAS로 IOCOUNT의 최상위비트를  ReleaseFlag로 쓴다
	//---------------------------------------
	
	ReleaseSocket(delSession);

#if MEMORYLOG_USE ==1 ||   MEMORYLOG_USE ==2
	IOCP_Log log;
#endif

#if MEMORYLOG_USE ==1 
	log.DataSettiong(InterlockedIncrement64(&g_IOCPMemoryNo), eIOCP_LINE::RELEASE_SESSION, GetCurrentThreadId(), delSession->_Socket, delSession->_IOCount, (int64_t)delSession, delSession->_ID, (int64_t)&delSession->_RecvOL, (int64_t)&delSession->_SendOL, delSession->_SendFlag);
	g_MemoryLog_IOCP.MemoryLogging(log);
#endif

#if MEMORYLOG_USE  ==2
	log.DataSettiong(InterlockedIncrement64(&g_IOCPMemoryNo), eIOCP_LINE::RELEASE_SESSION, GetCurrentThreadId(), delSession->_Socket, delSession->_IOCount, (int64_t)delSession, delSession->_ID, (int64_t)&delSession->_RecvOL, (int64_t)&delSession->_SendOL, delSession->_SendFlag,-1,-1,eRecvMessageType::NOTHING,-1,delSession->_AccountNo);
	g_MemoryLog_IOCP.MemoryLogging(log);
	delSession->_MemoryLog_IOCP.MemoryLogging(log);
#endif

	if (delSession->_IOCount < 0x80000000)
	{
		Crash();
	}

	ReleasePacket(delSession);
	if (delSession->_SendQ.GetQCount() > 0)
	{
		Crash();
	}


	if (delSession->_DeQPacketArray[0] != nullptr)
	{
		Crash();
	}
	
	SessionClear(delSession);


	OnClientLeave(tempSessionID);
	InterlockedDecrement(&m_SessionCount);
}

bool NetServer::DisconnectAllUser()
{
	for (size_t i = 0; i < m_MaxUserCount; ++i)
	{
		if (m_SessionArray[i]._USED)
		{
			ReleaseSocket(&m_SessionArray[i]);
		}
	}
	bool bAllUserRelease = true;
	while (true)
	{
		bAllUserRelease = true;
		for (size_t i = 0; i < m_MaxUserCount; ++i)
		{
			if (m_SessionArray[i]._USED)
			{
				bAllUserRelease = false;
			}
		}
		if (bAllUserRelease)
		{
			delete[] m_SessionArray;
			return true;
		}
	}

	return bAllUserRelease;
}


bool NetServer::RecvPacket(Session* curSession, DWORD transferByte)
{
	//-----------------------------------------
	// Enqueue 확정
	//-----------------------------------------
	if ((int)transferByte > curSession->_RecvRingQ.GetFreeSize())
	{
#if DISCONLOG_USE ==1
		_LOG->WriteLog(L"ChattingServer", SysLog::eLogLevel::LOG_LEVEL_ERROR, L"RecvRingQ 초과사이즈 들어옴 [Session ID:%llu] [transferByte:%d]", curSession->_ID, transferByte);
#endif
		Disconnect(curSession->_ID);

		return false;
	}
	curSession->_RecvRingQ.MoveRear(transferByte);

	curSession->_LastRecvTime = timeGetTime();

	
	while (true)
	{
		NetHeader header;
		NetPacket* packet;

		int usedSize = curSession->_RecvRingQ.GetUsedSize();

		if (usedSize < sizeof(NetHeader))
		{
			break;
		}
		int peekRtn = curSession->_RecvRingQ.Peek((char*)&header, sizeof(NetHeader));

		if (header._Code != dfPACKET_CODE)
		{
			//----------------------------------------
			// 헤더의 코드가 다를 경우 유저를 끊는다.
			//----------------------------------------
#if DISCONLOG_USE ==1
			_LOG->WriteLog(L"ChattingServer", SysLog::eLogLevel::LOG_LEVEL_ERROR, L"헤더 코드가 다름 [Session ID:%llu] [Code:%d]", curSession->_ID, header._Code);
			_LOG->WriteLogHex(L"ChattingServer", SysLog::eLogLevel::LOG_LEVEL_ERROR, L"Recv RingQ Hex",(BYTE*) curSession->_RecvRingQ.GetFrontBufferPtr(), curSession->_RecvRingQ.GetDirectDequeueSize());
#endif
			Disconnect(curSession->_ID);
			return false;
		}
		if (header._Len <= 0)
		{
			//----------------------------------------
			// 헤더안에 표기된 Len이 0보다 같거나 작으면 역시 끊는다.
			//----------------------------------------
#if DISCONLOG_USE ==1
			_LOG->WriteLog(L"ChattingServer", SysLog::eLogLevel::LOG_LEVEL_ERROR, L"헤더의 Len :0  [Session ID:%llu] [Code:%d]", curSession->_ID, header._Len);
#endif
			Disconnect(curSession->_ID);
			return false;
		}
		if (usedSize - sizeof(NetHeader) < header._Len)
		{
			//-------------------------------------
			// 들어오려고하는패킷이, 내 링버퍼 현재 여유사이즈보다 크면 말이안되기때문에,
			// 그런 Session은 연결을 끊는다.
			//-------------------------------------
			if (header._Len > curSession->_RecvRingQ.GetFreeSize())
			{
#if DISCONLOG_USE ==1
				_LOG->WriteLog(L"ChattingServer", SysLog::eLogLevel::LOG_LEVEL_ERROR, L"RingQ사이즈보다 큰 패킷이들어옴 [Session ID:%llu] [Header Len:%d] [My RingQ FreeSize:%d]", curSession->_ID, header._Len, curSession->_RecvRingQ.GetFreeSize());
#endif
				Disconnect(curSession->_ID);
				return false;
			}
			break;
		}
		curSession->_RecvRingQ.MoveFront(sizeof(header));


		packet = NetPacket::Alloc();
		InterlockedIncrement(&g_AllocMemoryCount);

		int deQRtn = curSession->_RecvRingQ.Dequeue((*packet).GetPayloadPtr(), header._Len);


		if (deQRtn != header._Len)
		{
			Crash();
		}
		(*packet).MoveWritePos(deQRtn);

		if ((*packet).GetPayloadSize() == 0)
		{
			Crash();
		}

		if (!packet->Decoding(&header))
		{
			//-----------------------------------
			// Decoding 실패시, 유저를 끊는다.
			//-----------------------------------
#if DISCONLOG_USE ==1
			_LOG->WriteLog(L"ChattingServer", SysLog::eLogLevel::LOG_LEVEL_ERROR, L"Decoding 실패 [Session ID:%llu] ", curSession->_ID);
#endif
			InterlockedIncrement(&g_FreeMemoryCount);
			packet->Free(packet);
			Disconnect(curSession->_ID);
			return false;
		}
		InterlockedIncrement((LONG*)&m_RecvTPS);
		OnRecv(curSession->_ID, packet);

	}

	RecvPost(curSession);

	return true;
}

bool NetServer::RecvPost(Session* curSession)
{
#if MEMORYLOG_USE ==1 ||   MEMORYLOG_USE ==2
	IOCP_Log log;
#endif

	//SendFlag_Log sendFlagLog;
	//-------------------------------------------------------------
	// Recv 걸기
	//-------------------------------------------------------------
	if (curSession->_RecvRingQ.GetFreeSize() <= 0)
	{
#if DISCONLOG_USE ==1
		_LOG->WriteLog(L"ChattingServer", SysLog::eLogLevel::LOG_LEVEL_ERROR, L"RecvPost시, 링버퍼 여유사이즈없음 [Session ID:%llu] ", curSession->_ID);
#endif
#if MEMORYLOG_USE ==1 
		log.DataSettiong(InterlockedIncrement64(&g_IOCPMemoryNo), eIOCP_LINE::RECVPOST_FREESIZE_NON, GetCurrentThreadId(), curSession->_Socket, curSession->_IOCount, (int64_t)curSession, curSession->_ID, (int64_t)&curSession->_RecvOL, (int64_t)&curSession->_SendOL, curSession->_SendFlag);
		g_MemoryLog_IOCP.MemoryLogging(log);
#endif

#if MEMORYLOG_USE  ==2
		log.DataSettiong(InterlockedIncrement64(&g_IOCPMemoryNo), eIOCP_LINE::RECVPOST_FREESIZE_NON, GetCurrentThreadId(), curSession->_Socket, curSession->_IOCount, (int64_t)curSession, curSession->_ID, (int64_t)&curSession->_RecvOL, (int64_t)&curSession->_SendOL, curSession->_SendFlag);
		g_MemoryLog_IOCP.MemoryLogging(log);
		curSession->_MemoryLog_IOCP.MemoryLogging(log);
#endif

		Disconnect(curSession->_ID);

		return false;
	}
	if (curSession->_IOCount <= 0)
	{
		Crash();
	}

	//sendFlagLog.DataSettiong(InterlockedIncrement64(&m_SendFlagNo), eSendFlag::RECVPOST, GetCurrentThreadId(), curSession->_Socket, (int64_t)curSession, (int64_t)&curSession->_RecvOL, (int64_t)&curSession->_SendOL, curSession->_SendFlag, curSession->_SendQ.GetQCount());
	//curSession->_MemoryLog_SendFlag.MemoryLogging(sendFlagLog);

	DirectData directData;
	int bufCount = 0;

	curSession->_RecvRingQ.GetDirectEnQData(&directData);

	WSABUF wsaRecvBuf[2];

	wsaRecvBuf[0].buf = directData.bufferPtr1;
	wsaRecvBuf[0].len = directData._Direct1;
	bufCount = 1;

	if (directData._Direct2 != 0)
	{
		bufCount = 2;
		wsaRecvBuf[1].buf = directData.bufferPtr2;
		wsaRecvBuf[1].len = directData._Direct2;
	}

	if (directData._Direct1 <0 || directData._Direct2<0 || directData._Direct1> RingQ::RING_BUFFER_SIZE || directData._Direct2> RingQ::RING_BUFFER_SIZE)
	{
		Crash();
	}
	DWORD flag = 0;

	if (curSession->_IOCount <= 0)
	{
		Crash();
	}

	InterlockedIncrement(&curSession->_IOCount);
#if IOCOUNT_CHECK ==1
	InterlockedIncrement(&g_IOPostCount);
	InterlockedIncrement(&g_IOIncDecCount);
#endif


#if MEMORYLOG_USE ==1 
	log.DataSettiong(InterlockedIncrement64(&g_IOCPMemoryNo), eIOCP_LINE::RECVPOST_BEFORE_RECV, GetCurrentThreadId(), curSession->_Socket, curSession->_IOCount, (int64_t)curSession, curSession->_ID, (int64_t)&curSession->_RecvOL, (int64_t)&curSession->_SendOL, curSession->_SendFlag);
	g_MemoryLog_IOCP.MemoryLogging(log);
#endif

#if MEMORYLOG_USE  ==2
	log.DataSettiong(InterlockedIncrement64(&g_IOCPMemoryNo), eIOCP_LINE::RECVPOST_BEFORE_RECV, GetCurrentThreadId(), curSession->_Socket, curSession->_IOCount, (int64_t)curSession, curSession->_ID, (int64_t)&curSession->_RecvOL, (int64_t)&curSession->_SendOL, curSession->_SendFlag);
	g_MemoryLog_IOCP.MemoryLogging(log);
	curSession->_MemoryLog_IOCP.MemoryLogging(log);
#endif


	ZeroMemory(&curSession->_RecvOL, sizeof(curSession->_RecvOL));

	if (curSession->_IOCount <= 0)
	{
	
		Crash();
	}

	//------------------------------------------------------------------
	// 	   IO Cancel 이 실행됬다면, 입출력을 걸지않고, IOCount를 낮추고 Return한다
	// 
	//------------------------------------------------------------------
	if (curSession->_bIOCancel)
	{
		if (0 == InterlockedDecrement(&curSession->_IOCount))
		{
			ReleaseSession(curSession);
		}

		return false;
	}
	int recvRtn = WSARecv(curSession->_Socket, wsaRecvBuf, bufCount, NULL, &flag, &curSession->_RecvOL, NULL);
	if (curSession->_IOCount <= 0)
	{
		Crash();
	}
	//g_MemoryLog_IOCount.MemoryLogging(RECVPOST_RECVRTN, GetCurrentThreadId(), curSession->_ID, curSession->_IOCount, (int64_t)curSession->_Socket, (int64_t)curSession, (int64_t)&curSession->_RecvOL, (int64_t)&curSession->_SendOL);

	if (recvRtn == SOCKET_ERROR)
	{
		int errorCode = WSAGetLastError();
		if (errorCode != WSA_IO_PENDING)
		{
			curSession->_IOFail = true;
			int tempIOCount = InterlockedDecrement(&curSession->_IOCount);

#if MEMORYLOG_USE ==1 
			log.DataSettiong(InterlockedIncrement64(&g_IOCPMemoryNo), eIOCP_LINE::RECVPOST_AFTER_RECV, GetCurrentThreadId(), curSession->_Socket, curSession->_IOCount, (int64_t)curSession, curSession->_ID, (int64_t)&curSession->_RecvOL, (int64_t)&curSession->_SendOL, curSession->_SendFlag);
			g_MemoryLog_IOCP.MemoryLogging(log);
#endif

#if MEMORYLOG_USE  ==2
			log.DataSettiong(InterlockedIncrement64(&g_IOCPMemoryNo), eIOCP_LINE::RECVPOST_AFTER_RECV, GetCurrentThreadId(), curSession->_Socket, curSession->_IOCount, (int64_t)curSession, curSession->_ID, (int64_t)&curSession->_RecvOL, (int64_t)&curSession->_SendOL, curSession->_SendFlag);
			g_MemoryLog_IOCP.MemoryLogging(log);
			curSession->_MemoryLog_IOCP.MemoryLogging(log);
#endif
			SpecialErrorCodeCheck(errorCode);
			if (0 == tempIOCount)
			{
				Crash();
				ReleaseSession(curSession);
			}
			if (tempIOCount < 0)
			{
				Crash();
			}
			return false;
		}
	}
	return true;
}

void NetServer::SendUnicast(uint64_t sessionID, NetPacket* packet)
{
	packet->IncrementRefCount();

	if (!SendPacket(sessionID, packet))
	{
		if (packet->DecrementRefCount() == 0)
		{
			InterlockedIncrement(&g_FreeMemoryCount);
			packet->Free(packet);
		}

		return;
	}
	PostQueuedCompletionStatus(m_IOCP, -1, NULL, (LPOVERLAPPED)sessionID);

	if (packet->DecrementRefCount() == 0)
	{
		InterlockedIncrement(&g_FreeMemoryCount);
		packet->Free(packet);
	}
}

bool NetServer::SendPost(uint64_t  sessionID)
{
	Session* curSession = nullptr;
	int loopCount = 0;

	//sendFlagLog.DataSettiong(InterlockedIncrement64(&m_SendFlagNo), eSendFlag::SENDPOST_ENTRY, GetCurrentThreadId(), curSession->_Socket, (int64_t)curSession, (int64_t)&curSession->_RecvOL, (int64_t)&curSession->_SendOL, curSession->_SendFlag, curSession->_SendQ.GetQCount());
	//curSession->_MemoryLog_SendFlag.MemoryLogging(sendFlagLog);

	if (!AcquireSession(sessionID,&curSession))
	{
		//-------------------------------------------------
		// SendFlag진입전 미리 AcquireSession을 얻는다.
		// 실패하면 내부적으로 알아서 IOCount를 차감한다.
		// 성공한 후, SendFlag에 진입을 실패하면  현재 함수 루프밖에서 IOCount를 감소시킨다.
		//-------------------------------------------------
		return false;
	}
	do
	{
		loopCount++;

		if (0 == InterlockedExchange(&curSession->_SendFlag, 1))
		{                  
			//--------------------------------------------------------
			// Echo Count가 증가한 범인
			//--------------------------------------------------------
			if (curSession->_SendQ.GetQCount() <= 0)
			{
				//sendFlagLog.DataSettiong(InterlockedIncrement64(&m_SendFlagNo), eSendFlag::SIZE_SHORT, GetCurrentThreadId(), curSession->_Socket, (int64_t)curSession, (int64_t)&curSession->_RecvOL, (int64_t)&curSession->_SendOL, curSession->_SendFlag, curSession->_SendQ.GetQCount());
				//curSession->_MemoryLog_SendFlag.MemoryLogging(sendFlagLog);
				InterlockedExchange(&curSession->_SendFlag, 0);
				continue;
			}
			//--------------------------------------------------
			// IOCount와 이세션이 WSARecv or WSASend 이후 로그를 위해 Session에 접근할수있기 때문에
			// 참조카운트용으로 하나 더 증가시킨다.
			//--------------------------------------------------
			InterlockedIncrement(&curSession->_IOCount);
			//--------------------------------------------------

			if (curSession->_IOCount <= 0)
			{
				Crash();
			}
#if IOCOUNT_CHECK==1
			InterlockedIncrement(&g_IOPostCount);
			InterlockedIncrement(&g_IOIncDecCount);
#endif
#if MEMORYLOG_USE ==1 ||   MEMORYLOG_USE ==2
			IOCP_Log log;
#endif

#if MEMORYLOG_USE ==1 
			log.DataSettiong(InterlockedIncrement64(&g_IOCPMemoryNo), eIOCP_LINE::SENDPOST_BEFORE_SEND, GetCurrentThreadId(), curSession->_Socket, curSession->_IOCount, (int64_t)curSession, curSession->_ID, (int64_t)&curSession->_RecvOL, (int64_t)&curSession->_SendOL, curSession->_SendFlag);
			g_MemoryLog_IOCP.MemoryLogging(log);
#endif

#if MEMORYLOG_USE  ==2
			log.DataSettiong(InterlockedIncrement64(&g_IOCPMemoryNo), eIOCP_LINE::SENDPOST_BEFORE_SEND, GetCurrentThreadId(), curSession->_Socket, curSession->_IOCount, (int64_t)curSession, curSession->_ID, (int64_t)&curSession->_RecvOL, (int64_t)&curSession->_SendOL, curSession->_SendFlag);
			g_MemoryLog_IOCP.MemoryLogging(log);
			curSession->_MemoryLog_IOCP.MemoryLogging(log);
#endif
			////-----------------------------------------------------------------------------------------------------------------------
			//// SendQ에 있는 LanPackt* 포인터들을 뽑아서 WSABUF를 세팅해준다
			////-----------------------------------------------------------------------------------------------------------------------
			WSABUF wsaSendBuf[Session::DEQ_PACKET_ARRAY_SIZE];

			int bufCount = 0;

#if SMART_PACKET_USE== 0

			NetPacket* deQPacket = nullptr;

			if (curSession->_DeQArraySize > 0)
			{
				Crash();

			}
			while (curSession->_SendQ.DeQ(&deQPacket))
			{
				if (deQPacket == nullptr)
				{
					Crash();
				}
				if (curSession->_DeQArraySize > Session::DEQ_PACKET_ARRAY_SIZE - 1)
				{
					Crash();
				}
				NetHeader tempHeader;
				memcpy(&tempHeader, deQPacket->GetBufferPtr(), sizeof(NetHeader));

				if (tempHeader._Code != dfPACKET_CODE)
				{
					Crash();
				}
				
				if (deQPacket->GetPayloadSize() <= 0)
				{
					Crash();
				}
				wsaSendBuf[curSession->_DeQArraySize].buf = deQPacket->GetBufferPtr();
				wsaSendBuf[curSession->_DeQArraySize].len = deQPacket->GetFullPacketSize();
				curSession->_SendByte += wsaSendBuf[curSession->_DeQArraySize].len;

				curSession->_DeQPacketArray[curSession->_DeQArraySize] = deQPacket;
				curSession->_DeQArraySize++;
			}
#endif
			//------------------------------------------------------
			//   Send 송신바이트 체크하기
			//------------------------------------------------------
			if (curSession->_SendByte <= 0)
			{
				Crash();
			}

			ZeroMemory(&curSession->_SendOL, sizeof(curSession->_SendOL));

			if (curSession->_IOCount <= 0)
			{
				Crash();
			}

			//sendFlagLog.DataSettiong(InterlockedIncrement64(&m_SendFlagNo), eSendFlag::BEFORE_SEND, GetCurrentThreadId(), curSession->_Socket, (int64_t)curSession, (int64_t)&curSession->_RecvOL, (int64_t)&curSession->_SendOL, curSession->_SendFlag, curSession->_SendQ.GetQCount());
			//curSession->_MemoryLog_SendFlag.MemoryLogging(sendFlagLog);


			//------------------------------------------------------------------
			// 	IO Cancel 이 실행됬다면, 입출력을 걸지않고, IOCount를 낮추고 Return한다
			//  Acquire IOCount +1 , 로그를위한 IOCount +1
			//------------------------------------------------------------------
			if (curSession->_bIOCancel)
			{
				for (int i = 0; i < 2; ++i)
				{
					if (0 == InterlockedDecrement(&curSession->_IOCount))
					{
						ReleaseSession(curSession);
					}
				}
				return false;
			}


			int sendRtn = WSASend(curSession->_Socket, wsaSendBuf, curSession->_DeQArraySize, NULL, 0, &curSession->_SendOL, NULL);
			
#if MEMORYLOG_USE ==1 
			log.DataSettiong(InterlockedIncrement64(&g_IOCPMemoryNo), eIOCP_LINE::SENDPOST_SENDRTN, GetCurrentThreadId(), curSession->_Socket, curSession->_IOCount, (int64_t)curSession, curSession->_ID, (int64_t)&curSession->_RecvOL, (int64_t)&curSession->_SendOL, curSession->_SendFlag);
			g_MemoryLog_IOCP.MemoryLogging(log);
#endif

#if MEMORYLOG_USE  ==2
			log.DataSettiong(InterlockedIncrement64(&g_IOCPMemoryNo), eIOCP_LINE::SENDPOST_SENDRTN, GetCurrentThreadId(), curSession->_Socket, curSession->_IOCount, (int64_t)curSession, curSession->_ID, (int64_t)&curSession->_RecvOL, (int64_t)&curSession->_SendOL, curSession->_SendFlag);
			g_MemoryLog_IOCP.MemoryLogging(log);
			curSession->_MemoryLog_IOCP.MemoryLogging(log);
#endif

			//sendFlagLog.DataSettiong(InterlockedIncrement64(&m_SendFlagNo), eSendFlag::AFTER_SEND, GetCurrentThreadId(), curSession->_Socket, (int64_t)curSession, (int64_t)&curSession->_RecvOL, (int64_t)&curSession->_SendOL, curSession->_SendFlag, curSession->_SendQ.GetQCount());
			//curSession->_MemoryLog_SendFlag.MemoryLogging(sendFlagLog);


			if (curSession->_IOCount <= 0)
			{
				Crash();
			}

			if (sendRtn == SOCKET_ERROR)
			{
				int errorCode = WSAGetLastError();

				if (errorCode != WSA_IO_PENDING)
				{
					curSession->_IOFail = true;
					SpecialErrorCodeCheck(errorCode);
					//InterlockedExchange(&curSession->_SendFlag, 0);

					//---------------------------------------------------------
					// Acquire에서 얻은 IOCount를 감소시킨다.
					//---------------------------------------------------------
					int tempIOCount = InterlockedDecrement(&curSession->_IOCount);

#if MEMORYLOG_USE ==1 
					log.DataSettiong(InterlockedIncrement64(&g_IOCPMemoryNo), eIOCP_LINE::SENDPOST_AFTER_SEND, GetCurrentThreadId(), curSession->_Socket, curSession->_IOCount, (int64_t)curSession, curSession->_ID, (int64_t)&curSession->_RecvOL, (int64_t)&curSession->_SendOL, curSession->_SendFlag);
					g_MemoryLog_IOCP.MemoryLogging(log);
#endif
#if MEMORYLOG_USE  ==2
					log.DataSettiong(InterlockedIncrement64(&g_IOCPMemoryNo), eIOCP_LINE::SENDPOST_AFTER_SEND, GetCurrentThreadId(), curSession->_Socket, curSession->_IOCount, (int64_t)curSession, curSession->_ID, (int64_t)&curSession->_RecvOL, (int64_t)&curSession->_SendOL, curSession->_SendFlag);
					g_MemoryLog_IOCP.MemoryLogging(log);
					curSession->_MemoryLog_IOCP.MemoryLogging(log);
#endif

					if (0 == tempIOCount)
					{
						ReleaseSession(curSession);
					}

				}
				else
				{
					/*	sendFlagLog.DataSettiong(InterlockedIncrement64(&m_SendFlagNo), eSendFlag::IO_PENDING, GetCurrentThreadId(), curSession->_Socket, (int64_t)curSession, (int64_t)&curSession->_RecvOL, (int64_t)&curSession->_SendOL, curSession->_SendFlag, curSession->_SendQ.GetQCount());
						curSession->_MemoryLog_SendFlag.MemoryLogging(sendFlagLog);*/
				}
			}

			//---------------------------------------------------------
			// Log를 위해 올렷던 IOCount를 감소시키고 끝낸다. (Return)
			//---------------------------------------------------------
			int tempIOCount = InterlockedDecrement(&curSession->_IOCount);
			if (0 == tempIOCount)
			{
				ReleaseSession(curSession);
			}
			return true;
		}
		else
		{
			break;
		}
	} while (curSession->_SendQ.GetQCount() > 0);


	//-------------------------------------------------------------
	// 	   Acquire을 얻었지만, SendFlag를 못얻은경우 
	// 	   or
	// 	   Acquire도 얻고, SendFlag도 얻었지만 SendQ에 내용물이 없는경우
	//-------------------------------------------------------------
	int tempIOCount = InterlockedDecrement(&curSession->_IOCount);
	if (0 == tempIOCount)
	{
		ReleaseSession(curSession);
	}

	return true;

}

void NetServer::SendNDiscon(uint64_t sessionID, NetPacket* packet)
{
	//---------------------------------------
   // 보낸 뒤, TimeOut 시간을 2초로 조정한다
   // Client에서 주는 프로토콜이 없다면 끊길것이다.
   //---------------------------------------
	SendUnicast(sessionID, packet);
	SetTimeOut(sessionID, 2000);

}

void NetServer::SpecialErrorCodeCheck(int32_t errorCode)
{
	if (errorCode != WSAECONNRESET && errorCode != WSAECONNABORTED && errorCode != WSAENOTSOCK && errorCode != WSAEINTR)
	{
		_LOG->WriteLog(L"ChattingServer", SysLog::eLogLevel::LOG_LEVEL_ERROR, L"Special ErrorCode : %d", errorCode);
		Crash();
	}
}

void NetServer::ReleaseSocket(Session* session)
{
#if MEMORYLOG_USE ==1 ||   MEMORYLOG_USE ==2
	IOCP_Log log;
#endif

	if (0 == InterlockedExchange(&session->_CloseFlag, 1))
	{
		//------------------------------------------------------
		// socket을 지우기전에, 먼저 session에있는 소켓부터 InvalidSocket으로 치환한다.
		//------------------------------------------------------

#if MEMORYLOG_USE ==1 
		log.DataSettiong(InterlockedIncrement64(&g_IOCPMemoryNo), eIOCP_LINE::CLOSE_SOCKET_ENTRY, GetCurrentThreadId(), session->_Socket, session->_IOCount, (int64_t)session, session->_ID, (int64_t)&session->_RecvOL, (int64_t)&session->_SendOL, session->_SendFlag);
		g_MemoryLog_IOCP.MemoryLogging(log);
#endif

#if MEMORYLOG_USE  ==2
		log.DataSettiong(InterlockedIncrement64(&g_IOCPMemoryNo), eIOCP_LINE::CLOSE_SOCKET_ENTRY, GetCurrentThreadId(), session->_Socket, session->_IOCount, (int64_t)session, session->_ID, (int64_t)&session->_RecvOL, (int64_t)&session->_SendOL, session->_SendFlag);
		g_MemoryLog_IOCP.MemoryLogging(log);
		session->_MemoryLog_IOCP.MemoryLogging(log);
#endif

		SOCKET temp = session->_Socket;

		session->_Socket = INVALID_SOCKET;

#if MEMORYLOG_USE ==1 
		log.DataSettiong(InterlockedIncrement64(&g_IOCPMemoryNo), eIOCP_LINE::CLOSE_SOCKET_INVALIDSOCKET, GetCurrentThreadId(), session->_Socket, session->_IOCount, (int64_t)session, session->_ID, (int64_t)&session->_RecvOL, (int64_t)&session->_SendOL, session->_SendFlag);
		g_MemoryLog_IOCP.MemoryLogging(log);
#endif

#if MEMORYLOG_USE  ==2
		log.DataSettiong(InterlockedIncrement64(&g_IOCPMemoryNo), eIOCP_LINE::CLOSE_SOCKET_INVALIDSOCKET, GetCurrentThreadId(), session->_Socket, session->_IOCount, (int64_t)session, session->_ID, (int64_t)&session->_RecvOL, (int64_t)&session->_SendOL, session->_SendFlag);
		g_MemoryLog_IOCP.MemoryLogging(log);
		session->_MemoryLog_IOCP.MemoryLogging(log);
#endif

		/*WSABUF wsaTempBuf;
		wsaTempBuf.buf = session->_RecvRingQ.GetRearBufferPtr();
		wsaTempBuf.len = session->_RecvRingQ.GetDirectEnqueueSize();

		DWORD flag = 0;


		int recvRtn = WSARecv(temp, &wsaTempBuf, 1, NULL, &flag, &session->_RecvOL, NULL);
		
		if (recvRtn == SOCKET_ERROR)
		{
			int errorCode = WSAGetLastError();
			if (errorCode == WSA_IO_PENDING)
			{
				Crash();
			}
		}
		else
		{
			Crash();
		}*/
		closesocket(temp);

#if MEMORYLOG_USE ==1 
		log.DataSettiong(InterlockedIncrement64(&g_IOCPMemoryNo), eIOCP_LINE::CLOSE_SOCKET_CLOSESOCKET, GetCurrentThreadId(), session->_Socket, session->_IOCount, (int64_t)session, session->_ID, (int64_t)&session->_RecvOL, (int64_t)&session->_SendOL, session->_SendFlag);
		g_MemoryLog_IOCP.MemoryLogging(log);
#endif

#if MEMORYLOG_USE  ==2
		log.DataSettiong(InterlockedIncrement64(&g_IOCPMemoryNo), eIOCP_LINE::CLOSE_SOCKET_CLOSESOCKET, GetCurrentThreadId(), session->_Socket, session->_IOCount, (int64_t)session, session->_ID, (int64_t)&session->_RecvOL, (int64_t)&session->_SendOL, session->_SendFlag);
		g_MemoryLog_IOCP.MemoryLogging(log);
		session->_MemoryLog_IOCP.MemoryLogging(log);
#endif
	}
}

void NetServer::AcceptUser(SOCKET socket, WCHAR* ip, uint16_t port)
{
#if MEMORYLOG_USE ==1 ||   MEMORYLOG_USE ==2
	IOCP_Log log;
#endif
	NetServer::Session* newSession = nullptr;
	uint64_t index = 0;

	while (!m_IndexStack->Pop(&index))
	{

	}

	newSession = &m_SessionArray[index];

	if (newSession == nullptr)
	{
		_LOG->WriteLog(L"ChattingServer", SysLog::eLogLevel::LOG_LEVEL_ERROR, L"Session이 모두 사용중입니다");
		Crash();
	}
	//-----------------------------------------------------------------
	// Accept한 유저의 기본 IOCount는 1이다
	//-----------------------------------------------------------------

	newSession->_Index = index;
	newSession->_Socket = socket;

	//-----------------------------------------------------------------
	// SessionID를 갱신 한후, 그때 RelaseFlag를 초기화해준다.
	//-----------------------------------------------------------------
	newSession->_ID = GetSessionID(index);


#if MEMORYLOG_USE ==1 
	log.DataSettiong(InterlockedIncrement64(&g_IOCPMemoryNo), eIOCP_LINE::ACCEPT_NEW_USER, GetCurrentThreadId(), newSession->_Socket, newSession->_IOCount, (int64_t)newSession, newSession->_ID, (int64_t)&newSession->_RecvOL, (int64_t)&newSession->_SendOL, newSession->_SendFlag);
	g_MemoryLog_IOCP.MemoryLogging(log);
#endif

#if MEMORYLOG_USE  ==2
	log.DataSettiong(InterlockedIncrement64(&g_IOCPMemoryNo), eIOCP_LINE::ACCEPT_NEW_USER, GetCurrentThreadId(), newSession->_Socket, newSession->_IOCount, (int64_t)newSession, newSession->_ID, (int64_t)&newSession->_RecvOL, (int64_t)&newSession->_SendOL, newSession->_SendFlag);
	g_MemoryLog_IOCP.MemoryLogging(log);
	newSession->_MemoryLog_IOCP.MemoryLogging(log);
#endif

	InterlockedAnd((LONG*)(&newSession->_IOCount), 0x7fffffff);

	wcscpy_s(newSession->_IP, ip);
	newSession->_Port = port;
	newSession->_USED = true;
	//--------------------------------------------
	if (m_TimeOutOption._OptionOn)
	{
		newSession->_TimeOut = m_TimeOutOption._LoginTimeOut;
	}
	newSession->_LastRecvTime = timeGetTime();

	//--------------------------------------------

	//------------------------------------------
	// For Debug
	//------------------------------------------
	uint64_t tempOrderIndex = (newSession->_OrderIndex++) % 3;
	newSession->_LastSessionID[tempOrderIndex][0] = newSession->_SessionOrder++;
	newSession->_LastSessionID[tempOrderIndex][1] = newSession->_ID;
	newSession->_LastSessionID[tempOrderIndex][2] = socket;


	DWORD tempTransfer;
	DWORD tempFlag = 0;
	BOOL bResult = WSAGetOverlappedResult(socket, &newSession->_RecvOL, &tempTransfer, FALSE, &tempFlag);
	if (!bResult)
	{
		int error = WSAGetLastError();
		if (error == WSA_IO_INCOMPLETE)
		{
			Crash();
		}
	}

	if (m_SessionID > UINT64_MAX - 1)
	{
		Crash();
	}

	if (NULL == CreateIoCompletionPort((HANDLE)newSession->_Socket, m_IOCP, (ULONG_PTR)newSession, 0))
	{
		_LOG->WriteLog(L"ChattingServer", SysLog::eLogLevel::LOG_LEVEL_ERROR, L"소켓과 IOCP 연결실패:%d", GetLastError());
		newSession->_USED = false;
		return;
	}

	//_LOG(LOG_LEVEL_DEBUG, L"Sesssion[%d]이 연결되었습니다", newSession->_ID);

	InterlockedIncrement(&m_SessionCount);//m_SessionCount

	m_Contents->OnClientJoin(newSession->_ID, newSession->_IP, newSession->_Port);


	WSABUF tempBuffer[1];
	DWORD flag = 0;

	tempBuffer[0].buf = newSession->_RecvRingQ.GetRearBufferPtr();
	tempBuffer[0].len = newSession->_RecvRingQ.GetDirectEnqueueSize();

	if (tempBuffer[0].len <= 0)
	{
		Crash();
	}

#if IOCOUNT_CHECK ==1
	InterlockedIncrement(&g_IOPostCount);
	InterlockedIncrement(&g_IOIncDecCount);
#endif



#if MEMORYLOG_USE ==1 
	log.DataSettiong(InterlockedIncrement64(&g_IOCPMemoryNo), eIOCP_LINE::ACCEPT_BEFORE_RECV, GetCurrentThreadId(), newSession->_Socket, newSession->_IOCount, (int64_t)newSession, newSession->_ID, (int64_t)&newSession->_RecvOL, (int64_t)&newSession->_SendOL, newSession->_SendFlag);
	g_MemoryLog_IOCP.MemoryLogging(log);
#endif

#if MEMORYLOG_USE  ==2
	log.DataSettiong(InterlockedIncrement64(&g_IOCPMemoryNo), eIOCP_LINE::ACCEPT_BEFORE_RECV, GetCurrentThreadId(), newSession->_Socket, newSession->_IOCount, (int64_t)newSession, newSession->_ID, (int64_t)&newSession->_RecvOL, (int64_t)&newSession->_SendOL, newSession->_SendFlag);
	g_MemoryLog_IOCP.MemoryLogging(log);
	newSession->_MemoryLog_IOCP.MemoryLogging(log);
#endif


	int recvRtn = WSARecv(newSession->_Socket, tempBuffer, 1, NULL, &flag, &newSession->_RecvOL, NULL);
	//g_MemoryLog_IOCount.MemoryLogging(ACCEPT_RECVRTN, GetCurrentThreadId(), newSession->_ID, newSession->_IOCount, (int64_t)newSession->_Socket, (int64_t)newSession, (int64_t)&newSession->_RecvOL, (int64_t)&newSession->_SendOL);
	if (recvRtn == SOCKET_ERROR)
	{
		int errorCode = WSAGetLastError();
		if (errorCode != WSA_IO_PENDING)
		{
			newSession->_IOFail = true;

			DWORD tempIOCount = InterlockedDecrement(&newSession->_IOCount);

#if MEMORYLOG_USE ==1 
			log.DataSettiong(InterlockedIncrement64(&g_IOCPMemoryNo), eIOCP_LINE::ACCEPT_AFTER_RECV, GetCurrentThreadId(), newSession->_Socket, newSession->_IOCount, (int64_t)newSession, newSession->_ID, (int64_t)&newSession->_RecvOL, (int64_t)&newSession->_SendOL, newSession->_SendFlag);
			g_MemoryLog_IOCP.MemoryLogging(log);
#endif

#if MEMORYLOG_USE  ==2
			log.DataSettiong(InterlockedIncrement64(&g_IOCPMemoryNo), eIOCP_LINE::ACCEPT_AFTER_RECV, GetCurrentThreadId(), newSession->_Socket, newSession->_IOCount, (int64_t)newSession, newSession->_ID, (int64_t)&newSession->_RecvOL, (int64_t)&newSession->_SendOL, newSession->_SendFlag);
			g_MemoryLog_IOCP.MemoryLogging(log);
			newSession->_MemoryLog_IOCP.MemoryLogging(log);
#endif

			SpecialErrorCodeCheck(errorCode);
			if (0 == tempIOCount)
			{
				//Crash();
				ReleaseSession(newSession);
			}
		}
	}

}

NetServer::Session* NetServer::FindSession(uint64_t sessionID)
{
	Session* findSession = nullptr;

	return &m_SessionArray[GetSessionIndex(sessionID)];
}

uint64_t NetServer::GetSessionID(uint64_t index)
{
	return (index << 48) | (++m_SessionID);
}

uint16_t NetServer::GetSessionIndex(uint64_t sessionID)
{
	uint16_t tempIndex = (uint16_t)((0xffff000000000000 & sessionID) >> 48);

	if (tempIndex <0 || tempIndex >m_MaxUserCount - 1)
	{
		Crash();
	}
	return tempIndex;
}

void NetServer::SessionClear(NetServer::Session* session)
{
#if MEMORYLOG_USE ==1 ||   MEMORYLOG_USE ==2
	IOCP_Log log;
#endif

	session->_Socket = INVALID_SOCKET;
	int64_t tempID = session->_ID;
	session->_CloseFlag = 0;

	//-------------------------------------------------
	// Release Flag 초기화 및 Accept 시 WSARecv에 걸 IOCount 미리 증가시킴
	//-------------------------------------------------
	InterlockedIncrement(&session->_IOCount);

#if MEMORYLOG_USE ==1 
	log.DataSettiong(InterlockedIncrement64(&g_IOCPMemoryNo), eIOCP_LINE::RELASE_SESSION_CLEAR_IO_INC, GetCurrentThreadId(), session->_Socket, session->_IOCount, (int64_t)session, session->_ID, (int64_t)&session->_RecvOL, (int64_t)&session->_SendOL, session->_SendFlag);
	g_MemoryLog_IOCP.MemoryLogging(log);
#endif

#if MEMORYLOG_USE  ==2
	log.DataSettiong(InterlockedIncrement64(&g_IOCPMemoryNo), eIOCP_LINE::RELASE_SESSION_CLEAR_IO_INC, GetCurrentThreadId(), session->_Socket, session->_IOCount, (int64_t)session, session->_ID, (int64_t)&session->_RecvOL, (int64_t)&session->_SendOL, session->_SendFlag);
	g_MemoryLog_IOCP.MemoryLogging(log);
	session->_MemoryLog_IOCP.MemoryLogging(log);
#endif

	session->_SendFlag = 0;
	if (session->_SendQ.GetQCount() > 0)
	{
		Crash();
	}

	session->_RecvRingQ.ClearBuffer();

	ZeroMemory(&session->_RecvOL, sizeof(session->_RecvOL));
	ZeroMemory(&session->_SendOL, sizeof(session->_SendOL));


	ZeroMemory(session->_IP, sizeof(session->_IP));
	session->_Port = 0;

	//-----------------------------------------
	// For Debug
	//-----------------------------------------
	//session->_MemoryLog_IOCP.Clear();
	//session->_MemoryLog_SendFlag.Clear();
	session->_SendByte = 0;
	session->_USED = false;
	session->_ErrorCode = 0;
	session->_GQCSRtn = TRUE;
	session->_TransferZero = 0;
	session->_IOFail = false;

	session->_bIOCancel = false;
	session->_bReserveDiscon = false;
	//-----------------------------------------------
	// Session Index 반환
	//-----------------------------------------------
	m_IndexStack->Push(GetSessionIndex(tempID));
}

void NetServer::DeQPacket(Session* session)
{
#if SMART_PACKET_USE ==1

	for (int i = 0; i < session->_DeQArraySize; ++i)
	{
		IOCP_Log log;
		log.DataSettiong(InterlockedIncrement64(&g_IOCPMemoryNo), eIOCP_LINE::DEQPACKET_ENTRY, GetCurrentThreadId(), session->_Socket, session->_IOCount, (int64_t)session, (int64_t)&session->_RecvOL, (int64_t)&session->_SendOL, session->_SendFlag, (int64_t)session->_DeQPacketArray[i].GetPtr(), *(session->_DeQPacketArray[i].m_RefCount));
		g_MemoryLog_IOCP.MemoryLogging(log);
		session->_MemoryLog_IOCP.MemoryLogging(log);


		session->_DeQPacketArray[i].~SmartNetPacket();

		log.DataSettiong(InterlockedIncrement64(&g_IOCPMemoryNo), eIOCP_LINE::DEQPACKET_LAST, GetCurrentThreadId(), session->_Socket, session->_IOCount, (int64_t)session, (int64_t)&session->_RecvOL, (int64_t)&session->_SendOL, session->_SendFlag, (int64_t)session->_DeQPacketArray[i].GetPtr(), *(session->_DeQPacketArray[i].m_RefCount));
		g_MemoryLog_IOCP.MemoryLogging(log);
		session->_MemoryLog_IOCP.MemoryLogging(log);
	}

	//----------------------------------------
	// Size 0으로 초기화
	//----------------------------------------
	session->_DeQArraySize = 0;

	if (session->_DeQArraySize > 1)
	{
		Crash();
	}
#endif
#if SMART_PACKET_USE ==0

	//wprintf(L"DeQArraySize :%d\n", session->_DeQArraySize);

	for (int i = 0; i < session->_DeQArraySize; ++i)
	{
		NetPacket* delNetPacket = session->_DeQPacketArray[i];
		session->_DeQPacketArray[i] = nullptr;

		//_LOG(LOG_LEVEL_ERROR, L"DeQPacket Func Packet Decremetnt(%d->%d)", delNetPacket->m_RefCount, delNetPacket->m_RefCount - 1);
		if (0 == delNetPacket->DecrementRefCount())
		{
			delNetPacket->Free(delNetPacket);
		
			InterlockedIncrement(&g_FreeMemoryCount);

			/*IOCP_Log log;
			log.DataSettiong(InterlockedIncrement64(&g_IOCPMemoryNo), eIOCP_LINE::DEQPACKET_FREE, GetCurrentThreadId(), session->_Socket, session->_IOCount, (int64_t)session, session->_ID,(int64_t)&session->_RecvOL, (int64_t)&session->_SendOL, session->_SendFlag, (int64_t)delNetPacket);
			g_MemoryLog_IOCP.MemoryLogging(log);
			session->_MemoryLog_IOCP.MemoryLogging(log);*/
		}

		//----------------------------------------------
		// Send 완료통지 후 처리되는것을 TPS로 카운팅 한다.
		//----------------------------------------------
		InterlockedIncrement(&m_SendTPS);
	}
	session->_DeQArraySize = 0;
#endif

}

void NetServer::ReleasePacket(Session* session)
{

#if SMART_PACKET_USE == 1

	while (true)
	{

		SmartNetPacket delNetPacket;
		if (!session->_SendQ.DeQ(&delNetPacket))
		{
			break;
		}
		IOCP_Log log;
		log.DataSettiong(InterlockedIncrement64(&g_IOCPMemoryNo), eIOCP_LINE::RELEASEPACKET_FREE, GetCurrentThreadId(), -1, -1, (int64_t)session, -1, -1, -1, (int64_t)delNetPacket.GetPtr());
		g_MemoryLog_IOCP.MemoryLogging(log);
	}

	if (session->_DeQArraySize > 0)
	{
		DeQPacket(session);
	}
#endif 

#if SMART_PACKET_USE == 0
	NetPacket* delNetPacket = nullptr;

	while (session->_SendQ.DeQ(&delNetPacket))
	{
		//_LOG(LOG_LEVEL_ERROR, L"Release  Packet Decremetnt(%d->%d)", delNetPacket->m_RefCount, delNetPacket->m_RefCount - 1);
		if (0 == delNetPacket->DecrementRefCount())
		{
			delNetPacket->Free(delNetPacket);
			InterlockedIncrement(&g_FreeMemoryCount);
			//IOCP_Log log;
			//log.DataSettiong(InterlockedIncrement64(&g_IOCPMemoryNo), eIOCP_LINE::RELEASEPACKET_FREE, GetCurrentThreadId(), -1, -1, (int64_t)session, -1, -1, -1, session->_SendFlag);
			//g_MemoryLog_IOCP.MemoryLogging(log);

		}
	}

	//-----------------------------------------------------
	// SendPacket 후, SendPost까지했는데
	// 상대방이 연결을 끊은 경우 Send 완료통지가 오지않은경우 남아있을 수 있따
	// 이를 위해 처리를 해줘야된다.
	//-----------------------------------------------------
	if (session->_DeQArraySize > 0)
	{
		DeQPacket(session);
	}
#endif 
}


bool NetServer::AcquireSession(uint64_t sessionID)
{
	Session* curSession = FindSession(sessionID);
	if (curSession == nullptr)
	{
		_LOG->WriteLog(L"ChattingServer", SysLog::eLogLevel::LOG_LEVEL_ERROR, L"Find Session이 널이다");
		Crash();

		return false;;
	}

	InterlockedIncrement(&curSession->_IOCount);

	//----------------------------------------------------------
	// Release Flag가 true이거나 인자로 들어온 세션아이디와 현재 세션ID가다르면
	// IO를 하면 안된다.
	//----------------------------------------------------------
	if ((curSession->_IOCount & 0x80000000) || (curSession->_ID != sessionID))
	{
#if MEMORYLOG_USE ==1 ||   MEMORYLOG_USE ==2
		IOCP_Log log;
#endif
#if MEMORYLOG_USE ==1 
		log.DataSettiong(InterlockedIncrement64(&g_IOCPMemoryNo), eIOCP_LINE::ACQUIRE_SESSION_FAIL, GetCurrentThreadId(), curSession->_Socket, curSession->_IOCount, (int64_t)curSession, curSession->_ID, (int64_t)&curSession->_RecvOL, (int64_t)&curSession->_SendOL, curSession->_SendFlag);
		g_MemoryLog_IOCP.MemoryLogging(log);
#endif

#if MEMORYLOG_USE  ==2
		log.DataSettiong(InterlockedIncrement64(&g_IOCPMemoryNo), eIOCP_LINE::ACQUIRE_SESSION_FAIL, GetCurrentThreadId(), curSession->_Socket, curSession->_IOCount, (int64_t)curSession, curSession->_ID, (int64_t)&curSession->_RecvOL, (int64_t)&curSession->_SendOL, curSession->_SendFlag);
		g_MemoryLog_IOCP.MemoryLogging(log);
		curSession->_MemoryLog_IOCP.MemoryLogging(log);
#endif

		if (0 == InterlockedDecrement(&curSession->_IOCount))
		{
			ReleaseSession(curSession);
		}

		return false;
	}

#if MEMORYLOG_USE ==1 ||   MEMORYLOG_USE ==2
	IOCP_Log log;
#endif
#if MEMORYLOG_USE == 1 
	log.DataSettiong(InterlockedIncrement64(&g_IOCPMemoryNo), eIOCP_LINE::ACQUIRE_SESSION_SUC, GetCurrentThreadId(), curSession->_Socket, curSession->_IOCount, (int64_t)curSession, curSession->_ID, (int64_t)&curSession->_RecvOL, (int64_t)&curSession->_SendOL, curSession->_SendFlag);
	g_MemoryLog_IOCP.MemoryLogging(log);
#endif

#if MEMORYLOG_USE  == 2
	log.DataSettiong(InterlockedIncrement64(&g_IOCPMemoryNo), eIOCP_LINE::ACQUIRE_SESSION_SUC, GetCurrentThreadId(), curSession->_Socket, curSession->_IOCount, (int64_t)curSession, curSession->_ID, (int64_t)&curSession->_RecvOL, (int64_t)&curSession->_SendOL, curSession->_SendFlag);
	g_MemoryLog_IOCP.MemoryLogging(log);
	curSession->_MemoryLog_IOCP.MemoryLogging(log);
#endif

	return true;
}

bool NetServer::AcquireSession(uint64_t sessionID, Session** outSession)
{
	Session* curSession = FindSession(sessionID);
	*outSession = curSession;
	if (curSession == nullptr)
	{
		_LOG->WriteLog(L"ChattingServer", SysLog::eLogLevel::LOG_LEVEL_ERROR, L"Find Session이 널이다");
		Crash();
		
		return false;;
	}

	InterlockedIncrement(&curSession->_IOCount);

	//----------------------------------------------------------
	// Release Flag가 true이거나 인자로 들어온 세션아이디와 현재 세션ID가다르면
	// IO를 하면 안된다.
	//----------------------------------------------------------
	if ((curSession->_IOCount & 0x80000000) || (curSession->_ID != sessionID))
	{
#if MEMORYLOG_USE ==1 ||   MEMORYLOG_USE ==2
		IOCP_Log log;
#endif
		#if MEMORYLOG_USE ==1 
				log.DataSettiong(InterlockedIncrement64(&g_IOCPMemoryNo), eIOCP_LINE::ACQUIRE_SESSION_FAIL, GetCurrentThreadId(), curSession->_Socket, curSession->_IOCount, (int64_t)curSession, curSession->_ID, (int64_t)&curSession->_RecvOL, (int64_t)&curSession->_SendOL, curSession->_SendFlag);
				g_MemoryLog_IOCP.MemoryLogging(log);
		#endif
		
		#if MEMORYLOG_USE  ==2
				log.DataSettiong(InterlockedIncrement64(&g_IOCPMemoryNo), eIOCP_LINE::ACQUIRE_SESSION_FAIL, GetCurrentThreadId(), curSession->_Socket, curSession->_IOCount, (int64_t)curSession, curSession->_ID, (int64_t)&curSession->_RecvOL, (int64_t)&curSession->_SendOL, curSession->_SendFlag);
				g_MemoryLog_IOCP.MemoryLogging(log);
				curSession->_MemoryLog_IOCP.MemoryLogging(log);
		#endif

		if (0 == InterlockedDecrement(&curSession->_IOCount))
		{
			ReleaseSession(curSession);
		}

		return false;
	}

#if MEMORYLOG_USE ==1 ||   MEMORYLOG_USE ==2
	IOCP_Log log;
#endif
	#if MEMORYLOG_USE == 1 
		log.DataSettiong(InterlockedIncrement64(&g_IOCPMemoryNo), eIOCP_LINE::ACQUIRE_SESSION_SUC, GetCurrentThreadId(), curSession->_Socket, curSession->_IOCount, (int64_t)curSession, curSession->_ID, (int64_t)&curSession->_RecvOL, (int64_t)&curSession->_SendOL, curSession->_SendFlag);
		g_MemoryLog_IOCP.MemoryLogging(log);
	#endif
	
	#if MEMORYLOG_USE  == 2
		log.DataSettiong(InterlockedIncrement64(&g_IOCPMemoryNo), eIOCP_LINE::ACQUIRE_SESSION_SUC, GetCurrentThreadId(), curSession->_Socket, curSession->_IOCount, (int64_t)curSession, curSession->_ID, (int64_t)&curSession->_RecvOL, (int64_t)&curSession->_SendOL, curSession->_SendFlag);
		g_MemoryLog_IOCP.MemoryLogging(log);
		curSession->_MemoryLog_IOCP.MemoryLogging(log);
	#endif
	
		

	return true;
}
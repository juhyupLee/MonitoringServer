#pragma once
//------------------------------------------------------
// 0 : Non Log
// 1 : g_Log
// 2 : Session Log && g_Log 
#define MEMORYLOG_USE 2
//------------------------------------------------------
//------------------------------------------------------
// 0 : Non Log
// 1 : 로그 사용
#define DISCONLOG_USE 0
//------------------------------------------------------
#include "Option.h"
#include "RingBuffer.h"
#include "SerializeBuffer.h"
#include "MemoryLog.h"
#include <unordered_map>
#include "LockFreeStack.h"
#include "LockFreeQ.h"

#define IOCOUNT_CHECK 0

enum
{
	RECV_RINGQ_FREESIZE,
	SEND_RINGQ_FREESIZE,
	SEND_RINGQ_MOVEFRONT,
	SEND_WSABUF_TOTAL
};




class NetServer
{

public:
	
	NetServer(NetServer* contents);
	virtual ~NetServer();
public:
	bool ServerStart(WCHAR* ip, uint16_t port, DWORD runningThread, SocketOption& option, DWORD workerThreadCount, DWORD maxUserCount, TimeOutOption& timeOutOption);
	void ServerStop();
	bool Disconnect(uint64_t sessionID);
	void SetTimeOut(uint64_t sessionID);
	void SetTimeOut(uint64_t sessionID, DWORD timeOut);

	bool SendPacket(uint64_t sessionID, NetPacket* packet);

	void SendUnicast(uint64_t sessionID, NetPacket* packet);
	void SendNDiscon(uint64_t sessionID, NetPacket* packet);

//#if SMART_PACKET_USE== 1
//	bool SendPacket(uint64_t sessionID, SmartNetPacket packet);
//#endif
	bool SendPost(uint64_t  sessionID);

protected:
	//------------------------------------------
	// Getter
	//------------------------------------------
	int64_t GetAcceptCount();
	LONG GetAcceptTPS();
	LONG GetSendTPS();
	LONG GetRecvTPS();
	LONG GetNetworkTraffic();
	LONG GetSessionCount();
	int32_t GetMemoryAllocCount();
	LONG GetSendQMeomryCount();
	LONG GetLockFreeStackMemoryCount();

public:
	//------------------------------------------
	// Contentes
	//------------------------------------------
	virtual bool OnConnectionRequest(WCHAR* ip, uint16_t port)=0;
	virtual void OnClientJoin(uint64_t sessionID, WCHAR* ip, uint16_t port) = 0;
	virtual void OnClientLeave(uint64_t sessionID) = 0;
	virtual void OnRecv(uint64_t sessionID, NetPacket* packet) = 0;
	virtual void OnError(int errorcode, WCHAR* errorMessage)=0;
	virtual void OnTimeOut(uint64_t sessionID) = 0;

	//For Debug 임시 Pulbic
public:
	struct Session
	{
		enum
		{
			//------------------------------------
			// 한 세션이 한 섹터를 기준으로 몇개의 메시지를 받는지 추정해서 
			// 세팅해야된다.
			//------------------------------------
			DEQ_PACKET_ARRAY_SIZE = 100

		};
		Session()
			:_SendQ(5000) //SendQ QCount 일단 오천개로 제한.
		{
			_Socket = 0;
			_IOCount = 1;
			_ID = 0;
			ZeroMemory(&_RecvOL, sizeof(WSAOVERLAPPED));
			ZeroMemory(&_SendOL, sizeof(WSAOVERLAPPED));
			_SendByte = 0;
			_USED = false;

			_SessionOrder = 0;
			_OrderIndex = 0;
			_Index = -1;
			_DeQArraySize = 0;
			_LastRecvTime = 0;
			//-------------------------------------------
			// Accept 전  timeOut 3초  Accept 후 timeOut 1분~5분
			//-------------------------------------------
			_TimeOut = 3000;
			_AccountNo = -1;
			_TransferZero = 0;

			_IOFail = false;
			_bIOCancel = false;
			_bReserveDiscon = false;

		}
		SOCKET _Socket;
		uint64_t _ID;
		int64_t _Index;
		bool _USED;
		RingQ _RecvRingQ;
#if SMART_PACKET_USE ==0
		LockFreeQ<NetPacket*> _SendQ;
		NetPacket* _DeQPacketArray[DEQ_PACKET_ARRAY_SIZE];
#endif
#if SMART_PACKET_USE ==1
		LockFreeQ<SmartNetPacket> _SendQ;
		SmartNetPacket _DeQPacketArray[DEQ_PACKET_ARRAY_SIZE];
#endif
		int32_t _DeQArraySize;
		WSAOVERLAPPED _RecvOL;
		WSAOVERLAPPED _SendOL;
		DWORD _IOCount;
		DWORD _CloseFlag;
		LONG _SendFlag;
		bool _bIOCancel;

		DWORD _SendByte;
		DWORD _LastRecvTime;
		DWORD _TimeOut;
		WCHAR _IP[17];
		uint16_t _Port;

		//------------------------------------------
		// For Debug
		//------------------------------------------
		bool _bReserveDiscon; // 더미클라이언트에선, 끊기전에 특정메시지를 보낸다 만약 이메시지를 보내지않았는데 Release를 하면 잘못된것
		uint64_t _SessionOrder;
		uint64_t _OrderIndex;
		uint64_t _LastSessionID[3][3];
		int _ErrorCode;
		BOOL _GQCSRtn;
		bool _IOFail;

		int _TransferZero;  //5 Recv 0  6 Send 0

		int64_t _AccountNo;
		

#if MEMORYLOG_USE == 2
		MemoryLogging_New<IOCP_Log,1500> _MemoryLog_IOCP;
#endif
		//MemoryLogging_New<SendFlag_Log> _MemoryLog_SendFlag;
	};

protected:
	void Crash();
private:
	static unsigned int __stdcall AcceptThread(LPVOID param);
	static unsigned int __stdcall WorkerThread(LPVOID param);
	static unsigned int __stdcall MonitorThread(LPVOID param);

	bool NetworkInit(WCHAR* ip, uint16_t port, DWORD runningThread, SocketOption option);
	bool ThreadInit(DWORD workerThreadCount);
	bool EventInit();

	void AcceptUser(SOCKET socket, WCHAR* ip, uint16_t port);
	
	bool RecvPacket(Session* curSession, DWORD transferByte);
	bool RecvPost(Session* curSession);


	void SpecialErrorCodeCheck(int32_t errorCode);
	void ReleaseSocket(Session* session);

protected:
	void ReleaseSession(Session* delSession);
	
	bool DisconnectAllUser();

	void IO_Cancel(Session* curSession);
	
	//--------------for Debug 임시 public:
public:
	Session* FindSession(uint64_t sessionID);

public:
	uint64_t GetSessionID(uint64_t index);
	uint16_t GetSessionIndex(uint64_t sessionID);

	void SessionClear(Session* session);
	void DeQPacket(Session* session);
	void ReleasePacket(Session* session);
	bool AcquireSession(uint64_t sessionID,Session** outSession);
	bool AcquireSession(uint64_t sessionID);
protected:
	LockFreeStack<uint64_t>* m_IndexStack;

	TimeOutOption m_TimeOutOption;

	uint64_t m_SessionID;
	DWORD m_MaxUserCount;
	LONG m_SessionCount;
	Session* m_SessionArray;


	SOCKET m_ListenSocket;
	HANDLE* m_WorkerThread;
	HANDLE m_AcceptThread;
	HANDLE m_MonitoringThread;


	uint16_t m_ServerPort;
	WCHAR* m_ServerIP;

	
	DWORD m_WorkerThreadCount;

	HANDLE m_IOCP;
	 

	//------------------------------------------------
	// For Debugging
	//------------------------------------------------
	LONG m_NetworkTraffic;

	MyMemoryLog<int64_t> m_MemoryLog_Overlap;

	int64_t m_SendFlagNo;

	LONG m_RecvTPS;
	LONG m_SendTPS;
	LONG m_AcceptTPS;


	LONG m_RecvTPS_To_Main;
	LONG m_SendTPS_To_Main;
	LONG m_AcceptTPS_To_Main;
	LONG m_NetworkTraffic_To_Main;

	LONG m_SendQMemory;


	int64_t m_AcceptCount;

	NetServer* m_Contents;

	HANDLE m_MonitorEvent;

	MyLock m_PrintLock;



};

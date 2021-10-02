#pragma once
//------------------------------------------------------
// 0 : Non Log
// 1 : g_Log
// 2 : Session Log && g_Log 
#define MEMORYLOG_USE 0
//------------------------------------------------------
//------------------------------------------------------
// 0 : Non Log
// 1 : �α� ���
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

class LanClient
{
	struct Session
	{
		enum
		{
			//------------------------------------
			// �� ������ �� ���͸� �������� ��� �޽����� �޴��� �����ؼ� 
			// �����ؾߵȴ�.
			//------------------------------------
			DEQ_PACKET_ARRAY_SIZE = 100

		};
		Session()
			:_SendQ(5000) //SendQ QCount �ϴ� ��õ���� ����.
		{
			_Socket = 0;
			_IOCount = 1;
			ZeroMemory(&_RecvOL, sizeof(WSAOVERLAPPED));
			ZeroMemory(&_SendOL, sizeof(WSAOVERLAPPED));
			_SendByte = 0;
			_DeQArraySize = 0;
			_LastRecvTime = 0;
			_TransferZero = 0;

			_IOFail = false;
			_bIOCancel = false;

		}
		SOCKET _Socket;
		RingQ _RecvRingQ;
		LockFreeQ<LanPacket*> _SendQ;
		LanPacket* _DeQPacketArray[DEQ_PACKET_ARRAY_SIZE];

		int32_t _DeQArraySize;
		WSAOVERLAPPED _RecvOL;
		WSAOVERLAPPED _SendOL;
		DWORD _IOCount;
		DWORD _CloseFlag;
		LONG _SendFlag;
		bool _bIOCancel;

		DWORD _SendByte;
		DWORD _LastRecvTime;

		//------------------------------------------
		// For Debug
		//------------------------------------------
		int _ErrorCode;
		BOOL _GQCSRtn;
		bool _IOFail;
		int _TransferZero;  //5 Recv 0  6 Send 0


#if MEMORYLOG_USE == 2
		MemoryLogging_New<IOCP_Log, 1500> _MemoryLog_IOCP;
#endif
	};
public:
	LanClient(LanClient* contents);
	virtual ~LanClient();
public:
	bool Connect(WCHAR* ip, uint16_t port, DWORD runningThread, SocketOption& option, DWORD workerThreadCount);
	void ClientStop();

	bool Disconnect(LanClient::Session* curSession);
	bool SendPacket(LanClient::Session* curSession, LanPacket* packet);
	bool SendPost(LanClient::Session* curSession);

protected:
	//------------------------------------------
	// Getter
	//------------------------------------------
	LONG GetSendTPS();
	LONG GetRecvTPS();
	LONG GetNetworkTraffic();
	int32_t GetMemoryAllocCount();
	LONG GetSendQMeomryCount();

public:
	//------------------------------------------
	// Contentes
	//------------------------------------------
	virtual void OnEnterJoinServer() = 0;
	virtual void OnLeaveServer() = 0;
	virtual void OnRecv(LanPacket* packet) = 0;
	virtual void OnError(int errorcode, WCHAR* errorMessage)=0;

	//For Debug �ӽ� Pulbic
public:


protected:
	static void Crash();
private:
	static unsigned int __stdcall WorkerThread(LPVOID param);
	static unsigned int __stdcall MonitorThread(LPVOID param);

	bool NetworkInit(WCHAR* ip, uint16_t port, DWORD runningThread, SocketOption option);
	bool ThreadInit(DWORD workerThreadCount);
	bool EventInit();

	
	bool RecvPacket(LanClient::Session* curSession, DWORD transferByte);
	bool RecvPost(LanClient::Session* curSession);


	void SpecialErrorCodeCheck(int32_t errorCode);
	void ReleaseSocket(LanClient::Session* session);

protected:

	void ReleaseSession(LanClient::Session* delSession);
	
	bool DisconnectAllUser();

	void IO_Cancel(LanClient::Session* curSession);
	
	//--------------for Debug �ӽ� public:
public:


	void SessionClear(LanClient::Session* session);
	void DeQPacket(LanClient::Session* session);
	void ReleasePacket(LanClient::Session* session);
	bool AcquireSession(LanClient::Session* curSession);
protected:

	Session* m_Session;
	SOCKADDR_IN m_ServerAddr;

	HANDLE* m_WorkerThread;
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


	LONG m_RecvTPS_To_Main;
	LONG m_SendTPS_To_Main;
	LONG m_NetworkTraffic_To_Main;

	LONG m_SendQMemory;

	LanClient* m_Contents;

	HANDLE m_MonitorEvent;

	MyLock m_PrintLock;

};

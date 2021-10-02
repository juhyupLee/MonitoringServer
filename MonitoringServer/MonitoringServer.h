#pragma once
#include "LanServerLib.h"
#include <unordered_map>
#define SQL_IP L"10.0.2.2"
//#define SQL_IP L"127.0.0.1"

class TLSDBConnector;
class MonitoringNetServer;

class MyMonitoringLanServer : public LanServer
{
	enum
	{
		MAX_IP_COUNT = 100
	};
	enum
	{
		GAME_SERVER_TYPE ,
		CHATTING_SERVER_TYPE,
		LOGIN_SERVER_TYPE
	};

	struct MonitorDataToDB
	{

		MonitorDataToDB()
		{
			_DataType = 0;
			_ServerType = -1;
			_Sum = 0;
			_Count = 0;
			_Min = INT32_MAX;
			_Max = INT32_MIN;
			_LastData = 0;
		}
		void Clear()
		{
			_DataType = 0;
			_ServerType = -1;
			_Sum = 0;
			_Count = 0;
			_Min = INT32_MAX;
			_Max = INT32_MIN;
			_LastData = 0;
		}
	
		BYTE _DataType;
		int _ServerType;
		int _Sum;
		int _Count;
		int _Min;
		int _Max;
		int _LastData;
	};
	struct Client
	{
		uint64_t _SessionID;
		WCHAR _IP[17];
		uint16_t _Port;
		int32_t _ServerType;
	};

	struct Black_IP
	{
		WCHAR _IP[17];
		uint16_t _Port;
	};
	struct White_IP
	{
		WCHAR _IP[17];
		uint16_t _Port;
	};


private:
	static unsigned int __stdcall DataSaveToDBThread(LPVOID param);

public:
	bool MonitorServerStart(WCHAR* ip, uint16_t port, DWORD runningThread, SocketOption& option, DWORD workerThreadCount, DWORD maxUserCount, TimeOutOption& timeOutOption);
	MyMonitoringLanServer();
	void ServerMonitorPrint();
public:
	
	//------------------------------------------
	// Contentes
	//------------------------------------------
	virtual bool OnConnectionRequest(WCHAR* ip, uint16_t port);
	virtual void OnClientJoin(uint64_t sessionID, WCHAR* ip, uint16_t port);
	virtual void OnClientLeave(uint64_t sessionID);
	virtual void OnRecv(uint64_t sessionID, LanPacket* packet);
	virtual void OnError(int errorcode, WCHAR* errorMessage) ;
	virtual void OnTimeOut(uint64_t sessionID);

	void PacketProc(uint64_t sessionID,LanPacket* packet);
	void ProcessPacket_en_PACKET_SS_MONITOR_LOGIN(uint64_t sessionID, LanPacket* packet);
	void ProcessPacket_en_PACKET_SS_MONITOR_DATA_UPDATE(uint64_t sessionID, LanPacket* packet);


	int16_t GetDataKey(int32_t serverType, BYTE dataType);

private:
	Client* FindClient(uint64_t sessionID);
private:
	MyLock m_MapLock;
	std::unordered_map<int16_t, MonitorDataToDB*> m_MonitorDataMap;

	TLSDBConnector* m_DBCon;
	HANDLE m_DataSaveToDBThread;
	MonitoringNetServer* m_NetServerLib;
	std::unordered_map<uint64_t, Client*> m_ClientMap;
	MyLock m_ClientMapLock;
	WCHAR m_BlackIPList[MAX_IP_COUNT][17];
};


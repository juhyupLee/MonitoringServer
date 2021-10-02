#pragma once
#include "LanServerLib.h"

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

private:
	Client* FindClient(uint64_t sessionID);
private:
	MonitoringNetServer* m_NetServerLib;
	std::unordered_map<uint64_t, Client*> m_ClientMap;
	MyLock m_ClientMapLock;
	WCHAR m_BlackIPList[MAX_IP_COUNT][17];
};


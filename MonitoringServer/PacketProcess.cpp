#include "PacketProcess.h"
#include "SerializeBuffer.h"


void MakePacket_en_PACKET_CS_MONITOR_TOOL_RES_LOGIN(NetPacket* packet, uint16_t type, BYTE status)
{
	//------------------------------------------------------------
	// 모니터링 클라이언트(툴) 모니터링 서버로 로그인 응답
	//
	//	{
	//		WORD	Type
	//
	//		BYTE	Status					// 로그인 결과 0 / 1 / 2 ... 하단 Define
	//	}
	//
	//------------------------------------------------------------
	//en_PACKET_CS_MONITOR_TOOL_RES_LOGIN,

	(*packet) << type << status;
}

void MakePacket_en_PACKET_CS_MONITOR_TOOL_DATA_UPDATE(NetPacket* packet, uint16_t type, BYTE serverNo, BYTE dataType, int32_t dataValue, int32_t timeStamp)
{
	//------------------------------------------------------------
	// 모니터링 서버가 모니터링 클라이언트(툴) 에게 모니터링 데이터 전송
	// 
	// 통합 모니터링 방식을 사용 중이므로, 모니터링 서버는 모든 모니터링 클라이언트에게
	// 수집되는 모든 데이터를 바로 전송시켜 준다.
	// 
	//
	// 데이터를 절약하기 위해서는 초단위로 모든 데이터를 묶어서 30~40개의 모니터링 데이터를 하나의 패킷으로 만드는게
	// 좋으나  여러가지 생각할 문제가 많으므로 그냥 각각의 모니터링 데이터를 개별적으로 전송처리 한다.
	//
	//	{
	//		WORD	Type
	//		
	//		BYTE	ServerNo				// 서버 No
	//		BYTE	DataType				// 모니터링 데이터 Type 하단 Define 됨.
	//		int		DataValue				// 해당 데이터 수치.
	//		int		TimeStamp				// 해당 데이터를 얻은 시간 TIMESTAMP  (time() 함수)
	//										// 본래 time 함수는 time_t 타입변수이나 64bit 로 낭비스러우니
	//										// int 로 캐스팅하여 전송. 그래서 2038년 까지만 사용가능
	//	}
	//
	//------------------------------------------------------------
	//en_PACKET_CS_MONITOR_TOOL_DATA_UPDATE,

	(*packet) << type << serverNo<<dataType<<dataValue<<timeStamp;
}

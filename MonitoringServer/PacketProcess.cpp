#include "PacketProcess.h"
#include "SerializeBuffer.h"


void MakePacket_en_PACKET_CS_MONITOR_TOOL_RES_LOGIN(NetPacket* packet, uint16_t type, BYTE status)
{
	//------------------------------------------------------------
	// ����͸� Ŭ���̾�Ʈ(��) ����͸� ������ �α��� ����
	//
	//	{
	//		WORD	Type
	//
	//		BYTE	Status					// �α��� ��� 0 / 1 / 2 ... �ϴ� Define
	//	}
	//
	//------------------------------------------------------------
	//en_PACKET_CS_MONITOR_TOOL_RES_LOGIN,

	(*packet) << type << status;
}

void MakePacket_en_PACKET_CS_MONITOR_TOOL_DATA_UPDATE(NetPacket* packet, uint16_t type, BYTE serverNo, BYTE dataType, int32_t dataValue, int32_t timeStamp)
{
	//------------------------------------------------------------
	// ����͸� ������ ����͸� Ŭ���̾�Ʈ(��) ���� ����͸� ������ ����
	// 
	// ���� ����͸� ����� ��� ���̹Ƿ�, ����͸� ������ ��� ����͸� Ŭ���̾�Ʈ����
	// �����Ǵ� ��� �����͸� �ٷ� ���۽��� �ش�.
	// 
	//
	// �����͸� �����ϱ� ���ؼ��� �ʴ����� ��� �����͸� ��� 30~40���� ����͸� �����͸� �ϳ��� ��Ŷ���� ����°�
	// ������  �������� ������ ������ �����Ƿ� �׳� ������ ����͸� �����͸� ���������� ����ó�� �Ѵ�.
	//
	//	{
	//		WORD	Type
	//		
	//		BYTE	ServerNo				// ���� No
	//		BYTE	DataType				// ����͸� ������ Type �ϴ� Define ��.
	//		int		DataValue				// �ش� ������ ��ġ.
	//		int		TimeStamp				// �ش� �����͸� ���� �ð� TIMESTAMP  (time() �Լ�)
	//										// ���� time �Լ��� time_t Ÿ�Ժ����̳� 64bit �� ���񽺷����
	//										// int �� ĳ�����Ͽ� ����. �׷��� 2038�� ������ ��밡��
	//	}
	//
	//------------------------------------------------------------
	//en_PACKET_CS_MONITOR_TOOL_DATA_UPDATE,

	(*packet) << type << serverNo<<dataType<<dataValue<<timeStamp;
}

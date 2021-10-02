#pragma once

#include <iostream>
#include <Windows.h>

class NetPacket;
class LanPacket;






void MakePacket_en_PACKET_CS_MONITOR_TOOL_RES_LOGIN(NetPacket* packet, uint16_t type, BYTE status);


void MakePacket_en_PACKET_CS_MONITOR_TOOL_DATA_UPDATE(NetPacket* packet, uint16_t type, BYTE serverNo,BYTE dataType, int32_t dataValue, int32_t timeStamp);
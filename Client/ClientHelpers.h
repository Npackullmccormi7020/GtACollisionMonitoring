#pragma once
#include <windows.networking.sockets.h>
#pragma comment(lib, "Ws2_32.lib")
#include "../Server/Packet.h"
#include "../Server/Constants.h"
#include "../Server/Logging.h"
#include "State.h"

using namespace std;

bool sendPacket(SOCKET sock, Packet& packet);
bool recvPacket(SOCKET sock, Packet& outPacket);
bool sendLargeData(SOCKET sock, const char* data, int totalSize);
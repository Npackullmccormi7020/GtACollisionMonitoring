#pragma once
#include <windows.networking.sockets.h>
#pragma comment(lib, "Ws2_32.lib")
#include "Packet.h"
#include "State.h"
#include "Constants.h"

using namespace std;

// Defined in ServerHelpers.cpp — extern here so all files that include this header can access them
extern mutex consoleMutex;
extern atomic<bool> serverRunning;
extern const string START_PASSKEY;

// Function declarations
void inputMonitor(SOCKET ServerSocket);
bool recvPacket(SOCKET sock, Packet& outPacket);
bool sendPacket(SOCKET sock, Packet& packet);
void handleClient(SOCKET ConnectionSocket, int clientID);

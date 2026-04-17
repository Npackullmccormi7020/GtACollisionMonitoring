#pragma once
#include <windows.networking.sockets.h>
#pragma comment(lib, "Ws2_32.lib")
#include "Packet.h"
#include "State.h"
#include "Constants.h"
#include "Logging.h"
#include "Coordinate.h"
#include <vector>
#include <map>
#include <mutex>

using namespace std;

// Defined in ServerHelpers.cpp � extern here so all files that include this header can access them
extern atomic<bool> serverRunning;
extern const string START_PASSKEY;
extern map<int, Coordinate> activePlanes;
extern mutex planesMutex;

// Function declarations
void inputMonitor(SOCKET ServerSocket);
bool recvPacket(SOCKET sock, Packet& outPacket);
bool sendPacket(SOCKET sock, Packet& packet);
void handleClient(SOCKET ConnectionSocket, int clientID);
vector<char> recvLargeData(SOCKET sock, int clientID);
bool writeBinaryFile(const string& filePath, const vector<char>& data);


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
#include <array>

using namespace std;

struct ClientAversionState
{
    Coordinate previousPosition;
    Coordinate currentPosition;
    bool hasCurrentPosition = false;
    bool hasPreviousPosition = false;
    bool pendingCollisionAlert = false;
    int pairedClientID = -1;
    int avoidanceStepsRemaining = 0;
    vector<Coordinate> pendingAversionCoordinates;
};

// Defined in ServerHelpers.cpp � extern here so all files that include this header can access them
extern atomic<bool> serverRunning;
extern const string START_PASSKEY;
extern map<int, Coordinate> activePlanes;
extern map<int, ClientAversionState> clientAversionStates;
extern mutex planesMutex;

// Function declarations
void inputMonitor(SOCKET ServerSocket);
bool recvPacket(SOCKET sock, Packet& outPacket);
bool sendPacket(SOCKET sock, Packet& packet);
void handleClient(SOCKET ConnectionSocket, int clientID);
vector<char> recvLargeData(SOCKET sock, int clientID);
bool writeBinaryFile(const string& filePath, const vector<char>& data);
vector<Coordinate> buildCollisionAversionPath(const Coordinate& previousPosition, const Coordinate& currentPosition, const Coordinate& otherPosition, int lateralDirectionSign);
bool buildCollisionAversionPacket(const vector<Coordinate>& coordinates, Packet& outPacket);


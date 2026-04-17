#pragma once
#include <windows.networking.sockets.h>
#pragma comment(lib, "Ws2_32.lib")
#include "../Server/Packet.h"
#include "../Server/Constants.h"
#include "../Server/Logging.h"
#include "../Server/Coordinate.h"
#include "State.h"
#include <string>
#include <vector>

using namespace std;

bool sendPacket(SOCKET sock, Packet& packet);
bool recvPacket(SOCKET sock, Packet& outPacket);
bool sendLargeData(SOCKET sock, const char* data, int totalSize);
ClientState getNextClientState(ClientState currentState, unsigned char instruction);
bool sendFlightAlertResponsePacket(SOCKET sock);
bool tryParseFlightPathLine(const string& line, Coordinate& outCoordinate);
bool loadFlightPathCoordinates(const string& flightPathFile, vector<Coordinate>& outCoordinates);
bool loadBinaryFile(const string& filePath, vector<char>& outBytes);
vector<vector<char>> splitLargeDataChunks(const char* data, int totalSize, int maxChunkSize = 254);
bool tryDeserializeCollisionAversionCoordinates(const Packet& packet, vector<Coordinate>& outCoordinates);
size_t advanceFlightPathIndex(size_t currentIndex, size_t skipCount, size_t totalCoordinates);

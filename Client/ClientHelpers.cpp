#include "ClientHelpers.h"
#include <algorithm>
#include <fstream>

// sendPacket() - serializes a Packet and sends all bytes to the server, looping until fully delivered
// Returns true on success, false on send error
bool sendPacket(SOCKET sock, Packet& packet)
{
    // Serialize the packet into a flat byte buffer
    int totalSize = 0;
    char* buffer = packet.SerializeData(totalSize);

    if (buffer == nullptr || totalSize <= 0)
        return false;

    // Loop until all bytes are sent - TCP may split large sends
    int totalSent = 0;
    while (totalSent < totalSize)
    {
        int sent = send(sock,
            buffer + totalSent,
            totalSize - totalSent,
            0);
        if (sent == SOCKET_ERROR)
            return false;
        totalSent += sent;
    }
    return true;
}

// recvPacket() - reads one full packet from the server into a Packet object, following the Header + Data layout defined in Packet.h
// Returns true on success, false if the connection dropped or a receive error occurred
bool recvPacket(SOCKET sock, Packet& outPacket)
{
    // Receive the fixed-size header (4 bytes = EmptyPktSize)
    char headerBuf[EmptyPktSize] = {};
    int totalReceived = 0;
    while (totalReceived < EmptyPktSize)
    {
        int received = recv(sock,
            headerBuf + totalReceived,
            EmptyPktSize - totalReceived,
            0);
        if (received <= 0)
            return false;   // Connection closed or recv error
        totalReceived += received;
    }

    // The 4th header byte is BodyLength - read that many data bytes
    unsigned char bodyLength = static_cast<unsigned char>(headerBuf[3]);

    // Allocate a full packet buffer: header + body
    int totalSize = EmptyPktSize + bodyLength;
    char* fullBuffer = new char[totalSize];
    memcpy(fullBuffer, headerBuf, EmptyPktSize);    // Copy header into buffer

    // Receive body bytes if any exist
    if (bodyLength > 0)
    {
        int bodyReceived = 0;
        while (bodyReceived < bodyLength)
        {
            int received = recv(sock,
                fullBuffer + EmptyPktSize + bodyReceived,
                bodyLength - bodyReceived,
                0);
            if (received <= 0)
            {
                delete[] fullBuffer;
                return false;
            }
            bodyReceived += received;
        }
    }

    // Reconstruct the Packet object from the raw buffer
    outPacket = Packet(fullBuffer);

    delete[] fullBuffer;
    return true;
}

// Function to send large amounts of data across the connection
bool sendLargeData(SOCKET sock, const char* data, int totalSize)
{
    // Initialize Logger
    Logger logger;

    // First send a "start" packet with the total size (4 bytes) so the server knows how much data is coming
    // Build a 5-byte buffer: [instruction][4 bytes of total size]
    string message = "[Client] Sending DATA_START Packet.\n";
    logger.Log(message);
    char startBuffer[5];
    startBuffer[0] = static_cast<char>(DATA_START);  // first byte = instruction
    memcpy(startBuffer + 1, &totalSize, 4);           // remaining 4 bytes = file size
    Packet startPacket;
    startPacket.SetData(startBuffer, 5);
    sendPacket(sock, startPacket);

    // Log data being sent to server
    logger.LogSend(string(1, startPacket.getInstruction()));

    vector<vector<char>> chunks = splitLargeDataChunks(data, totalSize);
    for (const vector<char>& chunkPayload : chunks)
    {
        message = "[Client] Sending DATA_CHUNK Packet.\n";
        logger.Log(message);
        int chunkSize = static_cast<int>(chunkPayload.size());

        // Build buffer: [DATA_CHUNK instruction][chunk bytes]
        char* chunkBuffer = new char[chunkSize + 1];
        chunkBuffer[0] = static_cast<char>(DATA_CHUNK);        // first byte = instruction
        memcpy(chunkBuffer + 1, chunkPayload.data(), chunkSize);

        Packet chunk;
        chunk.SetData(chunkBuffer, chunkSize + 1);
        sendPacket(sock, chunk);

        // Log data being sent to server
        logger.LogSend(string(1, chunk.getInstruction()));

        delete[] chunkBuffer;
    }

    message = "[Client] Sent all large data transfer chunks!\n\n";
    logger.Log(message);
    return true;
}

ClientState getNextClientState(ClientState currentState, unsigned char instruction)
{
    if (currentState == ClientState::Flying && instruction == COLLISION_ALERT)
        return ClientState::DivertCourse;

    return currentState;
}

bool sendFlightAlertResponsePacket(SOCKET sock)
{
    Packet packet;
    char responseInstruction = static_cast<char>(FLIGHT_ALERT_RESPONSE);
    packet.SetData(&responseInstruction, sizeof(responseInstruction));
    return sendPacket(sock, packet);
}

bool tryParseFlightPathLine(const string& line, Coordinate& outCoordinate)
{
    if (line.empty() || line[0] == '#')
        return false;

    size_t firstComma = line.find(',');
    size_t secondComma = line.find(',', firstComma == string::npos ? firstComma : firstComma + 1);
    if (firstComma == string::npos || secondComma == string::npos)
        return false;

    try
    {
        double x = stod(line.substr(0, firstComma));
        double y = stod(line.substr(firstComma + 1, secondComma - firstComma - 1));
        double z = stod(line.substr(secondComma + 1));
        outCoordinate = Coordinate(x, y, z);
        return true;
    }
    catch (const exception&)
    {
        return false;
    }
}

bool loadFlightPathCoordinates(const string& flightPathFile, vector<Coordinate>& outCoordinates)
{
    ifstream flightFile(flightPathFile);
    if (!flightFile.is_open())
        return false;

    outCoordinates.clear();

    string line;
    while (getline(flightFile, line))
    {
        if (line.empty() || line[0] == '#')
            continue;

        Coordinate coordinate;
        if (!tryParseFlightPathLine(line, coordinate))
            return false;

        outCoordinates.push_back(coordinate);
    }

    return true;
}

bool loadBinaryFile(const string& filePath, vector<char>& outBytes)
{
    ifstream input(filePath, ios::binary | ios::ate);
    if (!input.is_open())
        return false;

    streamsize size = input.tellg();
    if (size < 0)
        return false;

    input.seekg(0, ios::beg);
    outBytes.resize(static_cast<size_t>(size));
    if (size == 0)
        return true;

    return input.read(outBytes.data(), size).good();
}

vector<vector<char>> splitLargeDataChunks(const char* data, int totalSize, int maxChunkSize)
{
    vector<vector<char>> chunks;
    if (data == nullptr || totalSize <= 0 || maxChunkSize <= 0)
        return chunks;

    int offset = 0;
    while (offset < totalSize)
    {
        int chunkSize = min(maxChunkSize, totalSize - offset);
        chunks.emplace_back(data + offset, data + offset + chunkSize);
        offset += chunkSize;
    }

    return chunks;
}

bool tryDeserializeCollisionAversionCoordinates(const Packet& packet, vector<Coordinate>& outCoordinates)
{
    const int expectedBodyLength = static_cast<int>(sizeof(double) * 3 * COLLISION_AVERSION_COORDINATE_COUNT);
    if (packet.getInstruction() != COLLISION_AVERSION_INSTRUCTIONS)
        return false;

    if (packet.getBodyLength() != expectedBodyLength || packet.getData() == nullptr)
        return false;

    outCoordinates.clear();
    outCoordinates.reserve(COLLISION_AVERSION_COORDINATE_COUNT);

    const char* buffer = packet.getData();
    for (int index = 0; index < COLLISION_AVERSION_COORDINATE_COUNT; ++index)
    {
        Coordinate coordinate;
        coordinate.copy_from_Buffer(const_cast<char*>(buffer + (sizeof(double) * 3 * index)));
        outCoordinates.push_back(coordinate);
    }

    return true;
}

size_t advanceFlightPathIndex(size_t currentIndex, size_t skipCount, size_t totalCoordinates)
{
    const size_t advancedIndex = currentIndex + skipCount;
    return advancedIndex < totalCoordinates ? advancedIndex : totalCoordinates;
}

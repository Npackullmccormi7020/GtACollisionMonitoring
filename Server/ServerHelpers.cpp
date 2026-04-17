#include "ServerHelpers.h"
#include <array>

// Variable definitions - owned here, declared extern in ServerHelpers.h
const string START_PASSKEY = "Grp8_StartGroundControl";
atomic<bool> serverRunning(false);
Logger logger;

// Shared maps for collision detection and aversion state - declared extern in ServerHelpers.h
map<int, Coordinate> activePlanes;
map<int, ClientAversionState> clientAversionStates;
mutex planesMutex;

namespace
{
    struct Vector3
    {
        double x;
        double y;
        double z;
    };

    Vector3 ToVector(const Coordinate& coordinate)
    {
        return { coordinate.get_X(), coordinate.get_Y(), coordinate.get_Z() };
    }

    Coordinate ToCoordinate(const Vector3& vector)
    {
        return Coordinate(vector.x, vector.y, vector.z);
    }

    Vector3 Add(const Vector3& left, const Vector3& right)
    {
        return { left.x + right.x, left.y + right.y, left.z + right.z };
    }

    Vector3 Subtract(const Vector3& left, const Vector3& right)
    {
        return { left.x - right.x, left.y - right.y, left.z - right.z };
    }

    Vector3 Scale(const Vector3& vector, double scale)
    {
        return { vector.x * scale, vector.y * scale, vector.z * scale };
    }

    double Magnitude(const Vector3& vector)
    {
        return sqrt(vector.x * vector.x + vector.y * vector.y + vector.z * vector.z);
    }

    Vector3 Normalize(const Vector3& vector)
    {
        const double length = Magnitude(vector);
        if (length == 0.0)
            return { 0.0, 0.0, 0.0 };

        return Scale(vector, 1.0 / length);
    }

    Vector3 BuildForwardVector(const Coordinate& previousPosition, const Coordinate& currentPosition, const Coordinate& otherPosition)
    {
        Vector3 forward = Normalize(Subtract(ToVector(currentPosition), ToVector(previousPosition)));
        if (Magnitude(forward) == 0.0)
            forward = Normalize(Subtract(ToVector(otherPosition), ToVector(currentPosition)));
        if (Magnitude(forward) == 0.0)
            forward = { 1.0, 0.0, 0.0 };

        return forward;
    }

    Vector3 BuildLateralVector(const Coordinate& previousPosition, const Coordinate& currentPosition, const Coordinate& otherPosition)
    {
        const Vector3 forward = BuildForwardVector(previousPosition, currentPosition, otherPosition);

        Vector3 lateral = { -forward.y, forward.x, 0.0 };
        if (Magnitude(lateral) == 0.0)
            lateral = { 0.0, -forward.z, forward.y };
        if (Magnitude(lateral) == 0.0)
            lateral = { 0.0, 1.0, 0.0 };

        return Normalize(lateral);
    }

    void MarkPairForCollisionAversion(int clientID, int otherClientID)
    {
        ClientAversionState& thisPlane = clientAversionStates[clientID];
        ClientAversionState& otherPlane = clientAversionStates[otherClientID];

        const Coordinate thisPrevious = thisPlane.hasPreviousPosition ? thisPlane.previousPosition : thisPlane.currentPosition;
        const Coordinate otherPrevious = otherPlane.hasPreviousPosition ? otherPlane.previousPosition : otherPlane.currentPosition;

        thisPlane.pendingCollisionAlert = true;
        otherPlane.pendingCollisionAlert = true;
        thisPlane.pairedClientID = otherClientID;
        otherPlane.pairedClientID = clientID;
        thisPlane.avoidanceStepsRemaining = COLLISION_AVERSION_COORDINATE_COUNT;
        otherPlane.avoidanceStepsRemaining = COLLISION_AVERSION_COORDINATE_COUNT;
        thisPlane.pendingAversionCoordinates = buildCollisionAversionPath(thisPrevious, thisPlane.currentPosition, otherPlane.currentPosition, 1);
        otherPlane.pendingAversionCoordinates = buildCollisionAversionPath(otherPrevious, otherPlane.currentPosition, thisPlane.currentPosition, -1);
    }

    bool shouldCheckForCollision(const ClientAversionState& planeState)
    {
        return planeState.hasCurrentPosition
            && !planeState.pendingCollisionAlert
            && planeState.avoidanceStepsRemaining == 0;
    }
}

// Input monitor thread function - runs concurrently with the accept loop.
// Waits for the user to type "x" then sets serverRunning to false, which causes the accept loop to exit on its next iteration.
// Also calls closesocket() on ServerSocket to unblock the blocking accept() call.
void inputMonitor(SOCKET ServerSocket)
{
    string input;
    while (serverRunning)
    {
        cin >> input;
        if (input == "x")
        {
            logger.Log("\n[Server] Shutdown command received. Stopping server...\n");
            serverRunning = false;
            closesocket(ServerSocket);
            break;
        }
    }
}

// Reads one packet from the socket into a Packet object, following the Header + Data layout defined in Packet.h.
// Returns true on success, false if the connection dropped or a receive error occurred.
bool recvPacket(SOCKET sock, Packet& outPacket)
{
    char headerBuf[EmptyPktSize] = {};
    int totalReceived = 0;
    while (totalReceived < EmptyPktSize)
    {
        int received = recv(sock, headerBuf + totalReceived, EmptyPktSize - totalReceived, 0);
        if (received <= 0)
            return false;
        totalReceived += received;
    }

    unsigned char bodyLength = static_cast<unsigned char>(headerBuf[3]);
    const int totalSize = EmptyPktSize + bodyLength;
    char* fullBuffer = new char[totalSize];
    memcpy(fullBuffer, headerBuf, EmptyPktSize);

    if (bodyLength > 0)
    {
        int bodyReceived = 0;
        while (bodyReceived < bodyLength)
        {
            int received = recv(sock, fullBuffer + EmptyPktSize + bodyReceived, bodyLength - bodyReceived, 0);
            if (received <= 0)
            {
                delete[] fullBuffer;
                return false;
            }
            bodyReceived += received;
        }
    }

    outPacket = Packet(fullBuffer);
    delete[] fullBuffer;
    return true;
}

// Serializes a Packet and sends all bytes to the socket, looping until fully delivered.
// Returns true on success, false on send error.
bool sendPacket(SOCKET sock, Packet& packet)
{
    int totalSize = 0;
    char* buffer = packet.SerializeData(totalSize);

    if (buffer == nullptr || totalSize <= 0)
        return false;

    int totalSent = 0;
    while (totalSent < totalSize)
    {
        int sent = send(sock, buffer + totalSent, totalSize - totalSent, 0);
        if (sent == SOCKET_ERROR)
            return false;
        totalSent += sent;
    }

    return true;
}

// Per-client handler function - each accepted connection runs this on its own thread.
void handleClient(SOCKET ConnectionSocket, int clientID)
{
    string message = "[Client" + to_string(clientID) + "] Connection Established\n";
    logger.Log(message);

    ServerState serverState = ServerState::Listening;
    bool flightActive = true;
    Packet rxPacket;
    Packet txPacket;

    while (flightActive)
    {
        switch (serverState)
        {
        case ServerState::Listening:
        {
            if (!recvPacket(ConnectionSocket, rxPacket))
            {
                logger.Log("[Client" + to_string(clientID) + "] Connection lost during receive.\n\n");
                flightActive = false;
                break;
            }

            if (rxPacket.getInstruction() == FLIGHT_DONE)
            {
                logger.LogReceive(string(1, rxPacket.getInstruction()));
                logger.Log("[Client" + to_string(clientID) + "] FLIGHT_DONE received.");

                char ackData = static_cast<char>(ACK);
                txPacket = Packet();
                txPacket.SetData(&ackData, 1);
                sendPacket(ConnectionSocket, txPacket);
                logger.Log("[Client" + to_string(clientID) + "] Sending ACK.");
                logger.LogSend(string(1, txPacket.getInstruction()));

                flightActive = false;
                break;
            }

            if (rxPacket.getInstruction() != FLIGHT_ACTIVE)
                break;

            double x = 0.0;
            double y = 0.0;
            double z = 0.0;
            memcpy(&x, rxPacket.getData(), sizeof(double));
            memcpy(&y, rxPacket.getData() + sizeof(double), sizeof(double));
            memcpy(&z, rxPacket.getData() + sizeof(double) * 2, sizeof(double));

            const Coordinate currentPosition(x, y, z);
            logger.LogReceive(string(1, rxPacket.getInstruction()) + " | " + to_string(x) + ", " + to_string(y) + ", " + to_string(z));
            logger.Log("[Client" + to_string(clientID) + "] FLIGHT_ACTIVE received.");

            bool collisionDetected = false;
            int otherClientID = -1;
            double collisionDistance = 0.0;

            {
                lock_guard<mutex> lock(planesMutex);

                ClientAversionState& thisPlane = clientAversionStates[clientID];
                if (thisPlane.hasCurrentPosition)
                {
                    thisPlane.previousPosition = thisPlane.currentPosition;
                    thisPlane.hasPreviousPosition = true;
                }

                thisPlane.currentPosition = currentPosition;
                thisPlane.hasCurrentPosition = true;
                activePlanes[clientID] = currentPosition;

                if (thisPlane.pendingCollisionAlert)
                {
                    collisionDetected = true;
                }
                else if (shouldCheckForCollision(thisPlane))
                {
                    for (auto& entry : activePlanes)
                    {
                        if (entry.first == clientID)
                            continue;

                        ClientAversionState& otherPlaneState = clientAversionStates[entry.first];
                        if (!shouldCheckForCollision(otherPlaneState))
                            continue;

                        collisionDistance = currentPosition.get_distance(entry.second);
                        if (collisionDistance < COLLISION_DISTANCE_THRESHOLD)
                        {
                            collisionDetected = true;
                            otherClientID = entry.first;
                            MarkPairForCollisionAversion(clientID, otherClientID);
                            break;
                        }
                    }
                }

                if (!thisPlane.pendingCollisionAlert && thisPlane.avoidanceStepsRemaining > 0)
                {
                    thisPlane.avoidanceStepsRemaining--;
                    if (thisPlane.avoidanceStepsRemaining == 0)
                        thisPlane.pairedClientID = -1;
                }
            }

            if (otherClientID != -1)
            {
                logger.Log("[Client" + to_string(clientID) + "] COLLISION DETECTED with Client" + to_string(otherClientID)
                    + " | Distance: " + to_string(collisionDistance));
            }

            if (collisionDetected)
            {
                char alertData = static_cast<char>(COLLISION_ALERT);
                txPacket = Packet();
                txPacket.SetData(&alertData, 1);
                sendPacket(ConnectionSocket, txPacket);
                logger.Log("[Client" + to_string(clientID) + "] Sending COLLISION_ALERT.");
                logger.LogSend(string(1, txPacket.getInstruction()));
                serverState = ServerState::Alert;
            }
            else
            {
                char ackData = static_cast<char>(ACK);
                txPacket = Packet();
                txPacket.SetData(&ackData, 1);
                sendPacket(ConnectionSocket, txPacket);
                logger.Log("[Client" + to_string(clientID) + "] Sending ACK.");
                logger.LogSend(string(1, txPacket.getInstruction()));
            }
            break;
        }

        case ServerState::Alert:
        {
            if (!recvPacket(ConnectionSocket, rxPacket))
            {
                logger.Log("[Client" + to_string(clientID) + "] Connection lost during receive.\n\n");
                flightActive = false;
                break;
            }

            logger.LogReceive(string(1, rxPacket.getInstruction()));

            if (rxPacket.getInstruction() == FLIGHT_ALERT_RESPONSE)
            {
                char ackData = static_cast<char>(ACK);
                txPacket = Packet();
                txPacket.SetData(&ackData, 1);
                sendPacket(ConnectionSocket, txPacket);
                logger.Log("[Client" + to_string(clientID) + "] Sending FLIGHT_ALERT_RESPONSE 'ACK' Packet. Starting Large Data Transfer process\n\n");
                logger.LogSend(string(1, txPacket.getInstruction()));

                vector<char> imageData = recvLargeData(ConnectionSocket, clientID);
                writeBinaryFile("received_image.png", imageData);

                vector<Coordinate> aversionCoordinates;
                {
                    lock_guard<mutex> lock(planesMutex);
                    aversionCoordinates = clientAversionStates[clientID].pendingAversionCoordinates;
                }

                if (!aversionCoordinates.empty() && buildCollisionAversionPacket(aversionCoordinates, txPacket))
                {
                    logger.Log("[Client" + to_string(clientID) + "] Sending collision aversion instructions.");
                    if (!sendPacket(ConnectionSocket, txPacket))
                    {
                        logger.Log("[Server] Failed to send collision aversion instructions.\n");
                        flightActive = false;
                        break;
                    }

                    logger.LogSend(string(1, txPacket.getInstruction()));

                    Packet confirmationPacket;
                    if (!recvPacket(ConnectionSocket, confirmationPacket))
                    {
                        logger.Log("[Server] No ACK received for collision aversion instructions.\n");
                        flightActive = false;
                        break;
                    }

                    logger.LogReceive(string(1, confirmationPacket.getInstruction()));
                    if (confirmationPacket.getInstruction() == ACK)
                    {
                        lock_guard<mutex> lock(planesMutex);
                        clientAversionStates[clientID].pendingCollisionAlert = false;
                        clientAversionStates[clientID].pendingAversionCoordinates.clear();
                    }
                }

                break;
            }

            if (rxPacket.getInstruction() == FLIGHT_ACTIVE)
            {
                {
                    double x = 0.0;
                    double y = 0.0;
                    double z = 0.0;
                    memcpy(&x, rxPacket.getData(), sizeof(double));
                    memcpy(&y, rxPacket.getData() + sizeof(double), sizeof(double));
                    memcpy(&z, rxPacket.getData() + sizeof(double) * 2, sizeof(double));

                    lock_guard<mutex> lock(planesMutex);
                    ClientAversionState& thisPlane = clientAversionStates[clientID];
                    if (thisPlane.hasCurrentPosition)
                    {
                        thisPlane.previousPosition = thisPlane.currentPosition;
                        thisPlane.hasPreviousPosition = true;
                    }

                    thisPlane.currentPosition = Coordinate(x, y, z);
                    thisPlane.hasCurrentPosition = true;
                    activePlanes[clientID] = thisPlane.currentPosition;

                    if (thisPlane.avoidanceStepsRemaining > 0)
                    {
                        thisPlane.avoidanceStepsRemaining--;
                        if (thisPlane.avoidanceStepsRemaining == 0)
                            thisPlane.pairedClientID = -1;
                    }
                }

                char ackData = static_cast<char>(ACK);
                txPacket = Packet();
                txPacket.SetData(&ackData, 1);
                sendPacket(ConnectionSocket, txPacket);
                logger.Log("[Client" + to_string(clientID) + "] Sending ACK for collision aversion step.");
                logger.LogSend(string(1, txPacket.getInstruction()));

                {
                    lock_guard<mutex> lock(planesMutex);
                    if (clientAversionStates[clientID].avoidanceStepsRemaining == 0)
                        serverState = ServerState::Listening;
                }

                break;
            }

            char ackData = static_cast<char>(ACK);
            txPacket = Packet();
            txPacket.SetData(&ackData, 1);
            sendPacket(ConnectionSocket, txPacket);
            logger.LogSend(string(1, txPacket.getInstruction()));
            logger.Log("[Client" + to_string(clientID) + "] Received unexpected packet while in Alert.\n\n");
            break;
        }

        default:
            break;
        }
    }

    {
        lock_guard<mutex> lock(planesMutex);
        activePlanes.erase(clientID);
        clientAversionStates.erase(clientID);
    }

    closesocket(ConnectionSocket);
    logger.Log("[Client" + to_string(clientID) + "] Disconnected\n\n");
}

vector<char> recvLargeData(SOCKET sock, int clientID)
{
    Packet startPacket;
    recvPacket(sock, startPacket);
    logger.LogReceive(string(1, startPacket.getInstruction()));

    int totalSize = 0;
    memcpy(&totalSize, startPacket.getData(), 4);

    vector<char> buffer;
    buffer.reserve(totalSize);
    logger.Log("[Client" + to_string(clientID) + "] Received DATA_START packet, receiving large data transfer chunks...\n\n");

    while (static_cast<int>(buffer.size()) < totalSize)
    {
        Packet chunk;
        recvPacket(sock, chunk);
        logger.LogReceive(string(1, chunk.getInstruction()));

        char* chunkData = chunk.getData();
        int chunkLen = chunk.getBodyLength();
        buffer.insert(buffer.end(), chunkData, chunkData + chunkLen);
    }

    logger.Log("[Client" + to_string(clientID) + "] Received all large data transfer chunks!\n\n");
    return buffer;
}

bool writeBinaryFile(const string& filePath, const vector<char>& data)
{
    ofstream outFile(filePath, ios::binary | ios::trunc);
    if (!outFile.is_open())
        return false;

    if (!data.empty())
        outFile.write(data.data(), static_cast<streamsize>(data.size()));

    return outFile.good();
}

vector<Coordinate> buildCollisionAversionPath(const Coordinate& previousPosition, const Coordinate& currentPosition, const Coordinate& otherPosition, int lateralDirectionSign)
{
    vector<Coordinate> aversionCoordinates;
    aversionCoordinates.reserve(COLLISION_AVERSION_COORDINATE_COUNT);

    const Vector3 current = ToVector(currentPosition);
    const Vector3 forward = BuildForwardVector(previousPosition, currentPosition, otherPosition);
    const Vector3 lateral = Scale(BuildLateralVector(previousPosition, currentPosition, otherPosition), static_cast<double>(lateralDirectionSign));
    const array<double, COLLISION_AVERSION_COORDINATE_COUNT> lateralMultipliers = { 1.0, 1.5, 1.5, 0.75, 0.0 };
    const double lateralOffsetDistance = COLLISION_DISTANCE_THRESHOLD * 1.25;

    for (int step = 0; step < COLLISION_AVERSION_COORDINATE_COUNT; ++step)
    {
        const Vector3 projectedTrackPoint = Add(current, Scale(forward, static_cast<double>(step + 1)));
        const Vector3 adjustedPoint = Add(projectedTrackPoint, Scale(lateral, lateralOffsetDistance * lateralMultipliers[step]));
        aversionCoordinates.push_back(ToCoordinate(adjustedPoint));
    }

    return aversionCoordinates;
}

bool buildCollisionAversionPacket(const vector<Coordinate>& coordinates, Packet& outPacket)
{
    if (coordinates.size() != COLLISION_AVERSION_COORDINATE_COUNT)
        return false;

    vector<char> payload(1 + static_cast<int>(coordinates.size() * sizeof(double) * 3));
    payload[0] = static_cast<char>(COLLISION_AVERSION_INSTRUCTIONS);

    char* destination = payload.data() + 1;
    for (const Coordinate& coordinate : coordinates)
    {
        coordinate.copy_to_Buffer(destination);
        destination += sizeof(double) * 3;
    }

    outPacket = Packet();
    outPacket.SetData(payload.data(), static_cast<int>(payload.size()));
    return true;
}

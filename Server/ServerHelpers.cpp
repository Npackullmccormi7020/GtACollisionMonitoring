#include "ServerHelpers.h"

// Variable definitions — owned here, declared extern in ServerHelpers.h
const string START_PASSKEY = "Grp8_StartGroundControl";
atomic<bool> serverRunning(false);
Logger logger;

// Input monitor thread function — runs concurrently with the accept loop.
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
            serverRunning = false;      // Signal the accept loop to stop
            closesocket(ServerSocket);  // Unblocks accept() so the loop can check the flag
            break;
        }
    }
}

// Reads one packet from the socket into a Packet object, following the Header + Data layout defined in Packet.h.
// Returns true on success, false if the connection dropped or a receive error occurred.
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
            return false;   // Connection closed or error
        totalReceived += received;
    }

    // The 4th header byte is BodyLength — read that many data bytes
    unsigned char bodyLength = static_cast<unsigned char>(headerBuf[3]);

    // Allocate a full packet buffer: header + body
    int totalSize = EmptyPktSize + bodyLength;
    char* fullBuffer = new char[totalSize];
    memcpy(fullBuffer, headerBuf, EmptyPktSize);    // Copy header into buffer

    // Receive the body bytes if any exist
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

// Serializes a Packet and sends all bytes to the socket, looping until fully delivered.
// Returns true on success, false on send error.
bool sendPacket(SOCKET sock, Packet& packet)
{
    // Serialize the packet into a flat byte buffer
    int totalSize = 0;
    char* buffer = packet.SerializeData(totalSize);

    if (buffer == nullptr || totalSize <= 0)
        return false;

    // Loop until all bytes are sent (TCP may split large sends)
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

// Per-client handler function — each accepted connection runs this on its own thread.
// Essentially acting as the main loop that's isolated per client.
void handleClient(SOCKET ConnectionSocket, int clientID)
{
    string message = "[Client" + to_string(clientID) + "] Connection Established\n";
    logger.Log(message);

    ServerState serverState = ServerState::Listening;

    // flightActive controls the while loop — stays true until the client sends a FLIGHT_DONE packet
    bool flightActive = true;

    // Packet objects reused each iteration — one for receiving, one for sending
    Packet rxPacket;
    Packet txPacket;


    // ================================================
    // =============== Main Server Loop ===============
    // ================================================

    // The loop exits when a FLIGHT_DONE packet is received
    while (flightActive)
    {
        // Check Server State every loop
        switch (serverState)
        {


        // Normal State for Listening to Active Flights and detecting potential collisions
        case ServerState::Listening:

            // Receive one packet from this client. If the connection drops, recvPacket() returns false and we exit the loop
            if (!recvPacket(ConnectionSocket, rxPacket))
            {
                string message = "[Client" + to_string(clientID) + "] Connection lost during receive.\n\n";
                logger.Log(message);
                flightActive = false;   // Exit loop on dropped connection
                break;
            }

            // Check the Instruction byte for a FLIGHT_DONE packet
            if (rxPacket.getInstruction() == FLIGHT_DONE)
            {
                logger.LogReceive(string(1, rxPacket.getInstruction()));
                string message = "[Client" + to_string(clientID) + "] FLIGHT_DONE received.";
                logger.Log(message);

                // Build and send a one-byte ACK packet back to the client before closing the loop
                message = "[Client" + to_string(clientID) + "] Sending ACK.";
                logger.Log(message);

                char ackData = static_cast<char>(ACK);
                txPacket = Packet();
                txPacket.SetData(&ackData, 1);
                sendPacket(ConnectionSocket, txPacket);

                // Log data being sent to client
                logger.LogSend(string(1, txPacket.getInstruction()));

                flightActive = false;   // Exit the loop after ACK is sent
            }
            else if (rxPacket.getInstruction() == FLIGHT_ACTIVE) // Check the Instruction byte for a FLIGHT_ACTIVE packet
            {

                // Log the instruction byte and the 3 double values in a readable format
                double x, y, z;
                memcpy(&x, rxPacket.getData(), sizeof(double));
                memcpy(&y, rxPacket.getData() + sizeof(double), sizeof(double));
                memcpy(&z, rxPacket.getData() + sizeof(double) * 2, sizeof(double));

                // Log data received from client
                message = string(1, rxPacket.getInstruction()) + " | " + to_string(x) + ", " + to_string(y) + ", " + to_string(z);
                logger.LogReceive(message);
                string message = "[Client" + to_string(clientID) + "] FLIGHT_ACTIVE received.";
                logger.Log(message);

                // act
                // To-Do - Create Logic for determining if a collision is imminent            <-------------

                // If(collisionDetected)

                // Build and send a COLLISION_ALERT packet back to the client if collision is detected and switch state to Alert
                /* char ackData = static_cast<char>(COLLISION_ALERT);
                txPacket = Packet();
                txPacket.SetData(&ackData, 1);
                sendPacket(ConnectionSocket, txPacket);

                // Log data being sent to client
                Logger::LogSend(string(1, txPacket.getInstruction()));

                serverState = ServerState::Alert;*/

                // else

                // Build and send a one-byte ACK packet back to the client if no collision detected
                message = "[Client" + to_string(clientID) + "] Sending ACK.";
                logger.Log(message);

                char ackData = static_cast<char>(ACK);
                txPacket = Packet();
                txPacket.SetData(&ackData, 1);
                sendPacket(ConnectionSocket, txPacket);

                // Log data being sent to client
                logger.LogSend(string(1, txPacket.getInstruction()));
            }
            break;


        // Alert state to send Collision Aversion Instructions to Client + Large Data Transfer
        case ServerState::Alert:

            // receive
            // Receive one packet from this client. If the connection drops, recvPacket() returns false and we exit the loop
            if (!recvPacket(ConnectionSocket, rxPacket))
            {
                string message = "[Client" + to_string(clientID) + "] Connection lost during receive.\n\n";
                logger.Log(message);
                flightActive = false;   // Exit loop on dropped connection
                break;
            }

            // Log data received from client
            logger.LogReceive(string(1, rxPacket.getInstruction()));


            // act
            // Check the Instruction byte for a FLIGHT_ALERT_RESPONSE packet
            if (rxPacket.getInstruction() == FLIGHT_ALERT_RESPONSE)
            {
                // Send an acknowledgement of packet and then start large data transfer receive function
                string message = "[Client" + to_string(clientID) + "] Sending FLIGHT_ALERT_RESPONSE 'ACK' Packet. Starting Large Data Transfer process\n\n";
                logger.Log(message);

                char ackData = static_cast<char>(ACK);
                txPacket = Packet();
                txPacket.SetData(&ackData, 1);
                sendPacket(ConnectionSocket, txPacket);

                // Log data being sent to client
                logger.LogSend(string(1, txPacket.getInstruction()));

                // Start Large Data Transfer Process
                std::vector<char> imageData = recvLargeData(ConnectionSocket, clientID);

                // Write to file to verify it arrived intact
                std::ofstream outFile("received_image.png", std::ios::binary);
                outFile.write(imageData.data(), imageData.size());
                outFile.close();



                // To-Do: Collision Aversion Instructions Logic + Send information to Client                      <------------------



                // Set Server state to Listening once Collision Aversion instructions are done
                serverState = ServerState::Listening;
            }
            else
            {
                // Send an acknowledgement of packet but don't switch to anything - may want to add another flag for unexpected packet
                char ackData = static_cast<char>(ACK);
                txPacket = Packet();
                txPacket.SetData(&ackData, 1);
                sendPacket(ConnectionSocket, txPacket);

                // Log data being sent to client
                logger.LogSend(string(1, txPacket.getInstruction()));

                string message = "[Client" + to_string(clientID) + "] Received unexpected packet, retry start packet.\n\n";
                logger.Log(message);
            }
            break;
        default:
            break;
        }
    }

    // Clean up this client's socket when its loop exits
    closesocket(ConnectionSocket);
    message = "[Client" + to_string(clientID) + "] Disconnected\n\n";
    logger.Log(message);
}

vector<char> recvLargeData(SOCKET sock, int clientID)
{
    // First packet contains total expected size
    Packet startPacket;
    recvPacket(sock, startPacket);

    // Log data received from client
    logger.LogReceive(string(1, startPacket.getInstruction()));

    int totalSize = 0;
    memcpy(&totalSize, startPacket.getData(), 4);
    vector<char> buffer;
    buffer.reserve(totalSize); // reserving space for size of large data transfer

    string message = "[Client" + to_string(clientID) + "] Received DATA_START packet, receiving large data transfer chunks...\n\n";
    logger.Log(message);

    while ((int)buffer.size() < totalSize)
    {
        Packet chunk;
        recvPacket(sock, chunk);

        // Log data received from client
        logger.LogReceive(string(1, chunk.getInstruction()));

        // append chunk data to buffer
        char* chunkData = chunk.getData();
        int chunkLen = chunk.getBodyLength();
        buffer.insert(buffer.end(), chunkData, chunkData + chunkLen);
    }

    message = "[Client" + to_string(clientID) + "] Received all large data transfer chunks!\n\n";
    logger.Log(message);
    return buffer;
}
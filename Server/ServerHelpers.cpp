#include "ServerHelpers.h"

// Variable definitions — owned here, declared extern in ServerHelpers.h
mutex consoleMutex;
const string START_PASSKEY = "Grp8_StartGroundControl";
atomic<bool> serverRunning(false);

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
            {
                lock_guard<mutex> lock(consoleMutex);
                cout << "\n[Server] Shutdown command received. Stopping server..." << endl;
            }
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
    {
        // Lock console before printing so output from different threads doesn't interleave
        lock_guard<mutex> lock(consoleMutex);
        cout << "[Client " << clientID << "] Connection Established" << endl;
    }

    ServerState serverState = ServerState::Listening;

    // flightActive controls the while loop — stays true until the client sends a FLIGHT_DONE packet
    bool flightActive = true;

    // Packet objects reused each iteration — one for receiving, one for sending
    Packet rxPacket;
    Packet txPacket;

    //char RxBuffer[128] = {};

    // The loop exits when a FLIGHT_DONE packet is received
    while (flightActive)
    {
        // Main Logic Loop
        switch (serverState)
        {
        case ServerState::Listening:
            // Receive one packet from this client. If the connection drops, recvPacket() returns false and we exit the loop
            if (!recvPacket(ConnectionSocket, rxPacket))
            {
                lock_guard<mutex> lock(consoleMutex);
                cout << "[Client " << clientID << "] Connection lost during receive." << endl << endl;
                flightActive = false;   // Exit loop on dropped connection
                break;
            }

            // Check the Instruction byte for a FLIGHT_DONE packet
            if (rxPacket.getInstruction() == FLIGHT_DONE)
            {
                {
                    lock_guard<mutex> lock(consoleMutex);
                    cout << "[Client " << clientID << "] FLIGHT_DONE received." << endl;
                }

                // Build and send a one-byte ACK packet back to the client before closing the loop
                {
                    lock_guard<mutex> lock(consoleMutex);
                    cout << "[Client " << clientID << "] Sending ACK." << endl;
                }
                char ackData = static_cast<char>(ACK);
                txPacket = Packet();
                txPacket.SetData(&ackData, 1);
                sendPacket(ConnectionSocket, txPacket);

                flightActive = false;   // Exit the loop after ACK is sent
            }
            else if (rxPacket.getInstruction() == FLIGHT_ACTIVE) // Check the Instruction byte for a FLIGHT_ACTIVE packet
            {
                {
                    lock_guard<mutex> lock(consoleMutex);
                    cout << "[Client " << clientID << "] FLIGHT_ACTIVE received." << endl;
                }

                // act
                // To-Do - Create Logic for determining if a collision is imminent

                // If(collisionDetected)

                // Build and send a COLLISION_ALERT packet back to the client if collision is detected and switch state to Alert
                /* char ackData = static_cast<char>(COLLISION_ALERT);
                txPacket = Packet();
                txPacket.SetData(&ackData, 1);
                sendPacket(ConnectionSocket, txPacket);
                serverState = ServerState::Alert;*/

                // else

                // Build and send a one-byte ACK packet back to the client if no collision detected
                {
                    lock_guard<mutex> lock(consoleMutex);
                    cout << "[Client " << clientID << "] Sending ACK." << endl;
                }
                char ackData = static_cast<char>(ACK);
                txPacket = Packet();
                txPacket.SetData(&ackData, 1);
                sendPacket(ConnectionSocket, txPacket);
            }
            // send
            break;

        case ServerState::Alert:
            // receive
            // act
            // send
            break;
        default:
            break;
        }
    }

    // Clean up this client's socket when its loop exits
    closesocket(ConnectionSocket);
    {
        lock_guard<mutex> lock(consoleMutex);
        cout << "[Client " << clientID << "] Disconnected" << endl << endl;
    }
}

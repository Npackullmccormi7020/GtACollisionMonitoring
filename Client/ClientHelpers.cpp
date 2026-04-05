#include "ClientHelpers.h"

// sendPacket() — serializes a Packet and sends all bytes to the server, looping until fully delivered
// Returns true on success, false on send error
bool sendPacket(SOCKET sock, Packet& packet)
{
    // Serialize the packet into a flat byte buffer
    int totalSize = 0;
    char* buffer = packet.SerializeData(totalSize);

    if (buffer == nullptr || totalSize <= 0)
        return false;

    // Loop until all bytes are sent — TCP may split large sends
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

// recvPacket() — reads one full packet from the server into a Packet object, following the Header + Data layout defined in Packet.h
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

    // The 4th header byte is BodyLength — read that many data bytes
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

    // Send data in 255-byte chunks (max BodyLength)
    int offset = 0;
    while (offset < totalSize)
    {
        message = "[Client] Sending DATA_CHUNK Packet.\n";
        logger.Log(message);
        int chunkSize = min(254, totalSize - offset); // leaving space for the 1 instruction byte

        // Build buffer: [DATA_CHUNK instruction][chunk bytes]
        char* chunkBuffer = new char[chunkSize + 1];
        chunkBuffer[0] = static_cast<char>(DATA_CHUNK);        // first byte = instruction
        memcpy(chunkBuffer + 1, data + offset, chunkSize);     // remaining bytes = image data

        Packet chunk;
        chunk.SetData(chunkBuffer, chunkSize + 1);
        sendPacket(sock, chunk);

        // Log data being sent to server
        logger.LogSend(string(1, chunk.getInstruction()));

        delete[] chunkBuffer;
        offset += chunkSize;
    }

    message = "[Client] Sent all large data transfer chunks!\n\n";
    logger.Log(message);
    return true;
}
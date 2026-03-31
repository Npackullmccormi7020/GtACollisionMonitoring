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
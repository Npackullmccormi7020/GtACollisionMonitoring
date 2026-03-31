#include "ClientHelpers.h"
#include <thread>

using namespace std;

int main()
{
    // Initialize Logger
    Logger logger;

    //starts Winsock DLLs
    WSADATA wsaData;
    if ((WSAStartup(MAKEWORD(2, 2), &wsaData)) != 0) {
        return 0;
    }

    //initializes socket. SOCK_STREAM: TCP
    SOCKET ClientSocket;
    ClientSocket = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (ClientSocket == INVALID_SOCKET) {
        WSACleanup();
        return 0;
    }

    //Connect socket to specified server
    sockaddr_in SvrAddr;
    SvrAddr.sin_family = AF_INET;						//Address family type itnernet
    SvrAddr.sin_port = htons(27000);					//port (host to network conversion)
    SvrAddr.sin_addr.s_addr = inet_addr("127.0.0.1");	//IP address
    if ((connect(ClientSocket, (struct sockaddr*)&SvrAddr, sizeof(SvrAddr))) == SOCKET_ERROR) {
        closesocket(ClientSocket);
        WSACleanup();
        return 0;
    }

    // Log successful connection to the server
    logger.Log("[Client] Connected to Ground Control.\n");

    ClientState clientState = ClientState::Flying;

    // Packet objects reused each iteration — one for sending, one for receiving
    Packet txPacket;
    Packet rxPacket;

    // flightActive controls the while loop — stays true until the FLIGHT_DONE handshake completes
    bool flightActive = true;

    // The loop exits after the FLIGHT_DONE / ACK exchange completes or failure happens
    while (flightActive) {
        switch (clientState)
        {
        case ClientState::Flying:
        {
            // Build and send a data packet to the server
            
            // This will be the file reading and sending in 10s intervals                    <--------------

            this_thread::sleep_for(chrono::seconds(10));
            // If data is read, Flag is FLIGHT_ACTIVE
            char flightData = static_cast<char>(FLIGHT_ACTIVE);
            txPacket = Packet();
            txPacket.SetData(&flightData, 1);

            // Log data being sent
            logger.LogSend(string(1, flightData));

            if (!sendPacket(ClientSocket, txPacket))
            {
                logger.Log("[Client] Send failed. Disconnecting.\n\n");
                flightActive = false;   // Exit loop on send failure
                break;
            }


            // Else if at EOF, Flag is FLIGHT_DONE                                    <--------------
            //char flightData = static_cast<char>(FLIGHT_DONE);
            //txPacket = Packet();
            //txPacket.SetData(&flightData, 1);
            //if (!sendPacket(ClientSocket, txPacket))
            //{
            //    cout << "[Client] Send failed. Disconnecting." << endl;
            //    flightActive = false;   // Exit loop on send failure
            //    break;
            //}
            //if (rxPacket.getInstruction() == ACK)
            //{
            //    cout << "[Client] FLIGHT_DONE 'ACK' received" << endl;
            //    flightActive = false;   // Exit after full Flight Done handshake
            //}

            // Receive a response packet from the server
            if (!recvPacket(ClientSocket, rxPacket))
            {
                logger.Log("[Client] Receive failed. Disconnecting.\n\n");
                flightActive = false;   // Exit loop on recv failure
                break;
            }

            // Log data received
            logger.LogReceive(string(1, rxPacket.getInstruction()));

            // Inspect the server's response and react
            if (rxPacket.getInstruction() == COLLISION_ALERT)
            {
                logger.Log("[Client] COLLISION_ALERT received from server. Changing State to DivertCourse.\n");
                // TODO: transition clientState here based on logic
                clientState = ClientState::DivertCourse;
            }
            else if (rxPacket.getInstruction() == ACK)
                logger.Log("[Client] ACK received from server.\n");
            break;
        }
        case ClientState::DivertCourse:
        {
            // Build and send a FLIGHT_ALERT_RESPONSE packet (Large Data Transfer)                       <-------------
            // Server Responds with ACK packet and loop continues until Client logic is fully done
            
            
            
            char responseInstruction = static_cast<char>(FLIGHT_ALERT_RESPONSE);
            txPacket = Packet();
            txPacket.SetData(&responseInstruction, sizeof(responseInstruction));
            if (!sendPacket(ClientSocket, txPacket))
            {
                logger.Log("[Client] Failed to send FLIGHT_DONE packet.\n\n");
                flightActive = false;   // Exit even if send failed
                break;
            }
            logger.Log("[Client] FLIGHT_ALERT_RESPONSE sent. Waiting for ACK...\n");
            
            // Wait for the server's ACK confirming FLIGHT_DONE was received.
            if (!recvPacket(ClientSocket, rxPacket))
            {
                logger.Log("[Client] No ACK received. Closing anyway\n\n");
                flightActive = false;   //Exit regardless — server may have already closed
                break;
            }

            // Confirm the ACK
            if (rxPacket.getInstruction() == ACK)
                logger.Log("[Client] FLIGHT_ALERT_RESPONSE 'ACK' received\n");
                // To-Do - Logic for when we go back to flying state
            break;
        }
        default:
            break;
        }
    }

    //closes connection and socket
    closesocket(ClientSocket);
    //frees Winsock DLL resources
    WSACleanup();
    return 1;
}

#include "ClientHelpers.cpp"
#include <thread>

using namespace std;

int main()
{
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
    cout << "[Client] Connected to Ground Control." << endl;

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
            if (!sendPacket(ClientSocket, txPacket))
            {
                cout << "[Client] Send failed. Disconnecting." << endl << endl;
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
                cout << "[Client] Receive failed. Disconnecting." << endl << endl;
                flightActive = false;   // Exit loop on recv failure
                break;
            }

            // Inspect the server's response and react
            if (rxPacket.getInstruction() == COLLISION_ALERT)
            {
                cout << "[Client] COLLISION_ALERT received from server. Changing State to DivertCourse." << endl;
                // TODO: transition clientState here based on logic
                clientState = ClientState::DivertCourse;
            }
            else if (rxPacket.getInstruction() == ACK)
                cout << "[Client] ACK received from server." << endl;
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
                cout << "[Client] Failed to send FLIGHT_DONE packet." << endl << endl;
                flightActive = false;   // Exit even if send failed
                break;
            }
            cout << "[Client] FLIGHT_ALERT_RESPONSE sent. Waiting for ACK..." << endl;
            
            // Wait for the server's ACK confirming FLIGHT_DONE was received.
            if (!recvPacket(ClientSocket, rxPacket))
            {
                cout << "[Client] No ACK received. Closing anyway" << endl << endl;
                flightActive = false;   //Exit regardless — server may have already closed
                break;
            }

            // Confirm the ACK
            if (rxPacket.getInstruction() == ACK)
                cout << "[Client] FLIGHT_ALERT_RESPONSE 'ACK' received" << endl;
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

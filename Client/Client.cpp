#include "ClientHelpers.h"
#include <thread>
#include <cstdlib>
#include <ctime>

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
    SvrAddr.sin_family = AF_INET;						//Address family type internet
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





    // ================================================
    // =============== Main Client Loop ===============
    // ================================================
    
    // Packet objects reused each iteration — one for sending, one for receiving
    Packet txPacket;
    Packet rxPacket;

    // flightActive controls the while loop — stays true until the FLIGHT_DONE handshake completes
    bool flightActive = true;

    // Initialize simluted flight data variables
    int clientSeed = rand() % 100000;
    srand((unsigned int)time(nullptr) + clientSeed);
    double x = (rand() % 1000) / 10.0; // 0.0 - 99.9
    double y = (rand() % 1000) / 10.0;
    double z = 80.0 + (rand() % 400) / 10.0; // 80 - 120
    double vx = ((rand() % 21) - 10) / 10.0; // -1.0 to +1.0
    double vy = ((rand() % 21) - 10) / 10.0;

    // The loop exits after the FLIGHT_DONE / ACK exchange completes or failure happens
    while (flightActive) {

        // Check Client State every loop
        switch (clientState)
        {


        // Flying State - normal state with no alerts or issues
        case ClientState::Flying:
        {
            // Build and send a data packet to the server
            
            // This will be the file reading and sending in 10s intervals                    <--------------
            this_thread::sleep_for(chrono::seconds(10));

            // If data is read, Flag is FLIGHT_ACTIVE
            string message = "[Client] Sending FLIGHT_ACTIVE Packet.\n";
            logger.Log(message);

			// Simulated changes in flight data for each packet sent
            x += vx;
            y += vy;
            // z stays constant

            // Build the packet data
            char buffer[1 + sizeof(double) * 3];
            buffer[0] = static_cast<char>(FLIGHT_ACTIVE);
            memcpy(buffer + 1, &x, sizeof(double));
            memcpy(buffer + 1 + sizeof(double), &y, sizeof(double));
            memcpy(buffer + 1 + sizeof(double) * 2, &z, sizeof(double));
            txPacket = Packet();
            txPacket.SetData(buffer, sizeof(buffer));

            // Log data being sent
            logger.Log("[Client] Position Sent: (" + to_string(x) + ", " + to_string(y) + ", " + to_string(z) + ")\n");

            if (!sendPacket(ClientSocket, txPacket))
            {
                logger.Log("[Client] Send failed. Disconnecting.\n\n");
                flightActive = false;   // Exit loop on send failure
                break;
            }

            // Log data being sent to server
            logger.LogSend(string(1, txPacket.getInstruction()));

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

            // Check the server's response and react
            if (rxPacket.getInstruction() == COLLISION_ALERT)
            {
                logger.Log("[Client] COLLISION_ALERT received from server. Changing State to DivertCourse.\n");
                clientState = ClientState::DivertCourse; // Switch states to DivertCourse to receive collision aversion instructions
            }
            else if (rxPacket.getInstruction() == ACK)
                logger.Log("[Client] ACK received from server.\n");
            break;
        }



        // Collision Aversion State based on Server Instructions + Large Data Transfer
        case ClientState::DivertCourse:
        {
            // Send a FLIGHT_ALERT_RESPONSE packet (Large Data Transfer)
            // Server Responds with ACK packet and starts the large data transfer loop until finished and switches to Flying state
            string message = "[Client] Sending FLIGHT_ALERT_RESPONSE Packet.\n";
            logger.Log(message);
            char responseInstruction = static_cast<char>(FLIGHT_ALERT_RESPONSE);
            txPacket = Packet();
            txPacket.SetData(&responseInstruction, sizeof(responseInstruction));
            if (!sendPacket(ClientSocket, txPacket))
            {
                logger.Log("[Client] Failed to send FLIGHT_ALERT_RESPONSE packet.\n\n");
                flightActive = false;   // Exit even if send failed
                break;
            }

            // Log data being sent to server
            logger.LogSend(string(1, txPacket.getInstruction()));
            logger.Log("[Client] FLIGHT_ALERT_RESPONSE sent. Waiting for ACK...\n");

            // Wait for the server's ACK confirming FLIGHT_ALERT_RESPONSE was received.
            if (!recvPacket(ClientSocket, rxPacket))
            {
                logger.Log("[Client] No ACK received.\n\n");
                flightActive = false;   //Exit — server may have closed
                break;
            }

            // Log data received
            logger.LogReceive(string(1, rxPacket.getInstruction()));

            // Confirm the ACK
            if (rxPacket.getInstruction() == ACK)
            {
                logger.Log("[Client] FLIGHT_ALERT_RESPONSE 'ACK' received. Starting Large Data Transfer process...\n");

                // Read image file into a byte buffer
                // check if image exists before using
                ifstream imageFile("Images/Live_Feed1.png", std::ios::binary | std::ios::ate);
                if (!imageFile.is_open()) {
                    logger.Log("[Client] Failed to open image file.\n");
                    break;
                }
                int fileSize = imageFile.tellg();
                imageFile.seekg(0);

                char* imageBuffer = new char[fileSize];
                imageFile.read(imageBuffer, fileSize);
                imageFile.close();

                // Start Large Data Transfer Process
                sendLargeData(ClientSocket, imageBuffer, fileSize);

                delete[] imageBuffer;



                // To-Do: Collision Aversion Instructions Logic + Send Ack Packet for confirmation of received instructions         <----------------
                


                clientState = ClientState::Flying;  // resume normal flight reporting
            }
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

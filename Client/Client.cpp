#include "ClientHelpers.h"
#include <thread>
#include <cstdlib>
#include <ctime>
#include <fstream>

using namespace std;

int main(int argc, char* argv[])
{   
    // Initialize Logger
    Logger logger;

    // Validate command-line argument
    if (argc < 2)
    {
        logger.Log("[Client] ERROR: No flight path file provided.");
        logger.Log("[Client] Usage: Client.exe <flightpath_file>");
        return 1;
    }

    string flightPathFile = "Data/" +string(argv[1]);

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
    SvrAddr.sin_port = htons(GROUND_CONTROL_PORT);		//port (host to network conversion)
    SvrAddr.sin_addr.s_addr = inet_addr("127.0.0.1");	//IP address
    if ((connect(ClientSocket, (struct sockaddr*)&SvrAddr, sizeof(SvrAddr))) == SOCKET_ERROR) {
        closesocket(ClientSocket);
        WSACleanup();
        return 0;
    }

    // Log successful connection to the server
    logger.Log("[Client] Connected to Ground Control.\n");

    ClientState clientState = ClientState::Flying;
    Coordinate cords = Coordinate();
    vector<Coordinate> flightPathCoordinates;
    vector<Coordinate> aversionCoordinates;
    size_t flightCoordinateIndex = 0;
    size_t aversionCoordinateIndex = 0;

    // ================================================
    // =============== Main Client Loop ===============
    // ================================================
    
    // Packet objects reused each iteration - one for sending, one for receiving
    Packet txPacket;
    Packet rxPacket;

    // flightActive controls the while loop - stays true until the FLIGHT_DONE handshake completes
    bool flightActive = true;

    if (!loadFlightPathCoordinates(flightPathFile, flightPathCoordinates))
    {
        logger.Log("[Client] ERROR: Failed to load file: " + flightPathFile);
        flightActive = false;
    }
    else
    {
        logger.Log("[Client] Using flight path file: " + flightPathFile);
    }

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
            this_thread::sleep_for(chrono::seconds(FLIGHT_TRANSMISSION_INTERVAL_SECONDS));

            if (flightCoordinateIndex < flightPathCoordinates.size())
            {
                cords = flightPathCoordinates[flightCoordinateIndex++];
            }
            else
            {
                logger.Log("[Client] EOF reached. Sending FLIGHT_DONE.");

                char doneData = static_cast<char>(FLIGHT_DONE);
                txPacket = Packet();
                txPacket.SetData(&doneData, 1);

                if (!sendPacket(ClientSocket, txPacket))
                {
                    logger.Log("[Client] Failed to send FLIGHT_DONE. Disconnecting.");
                    flightActive = false;
                }
                else if (!recvPacket(ClientSocket, rxPacket))
                {
                    logger.Log("[Client] No ACK received for FLIGHT_DONE. Disconnecting.");
                    flightActive = false;
                }
                else if (rxPacket.getInstruction() == ACK)
                {
                    logger.Log("[Client] FLIGHT_DONE ACK received. Closing connection.");
                    flightActive = false;
                }

                break;
            }

            double x = cords.get_X();
            double y = cords.get_Y();
            double z = cords.get_Z();

            // Log the coordinate being sent
            string message = "[Client] Sending FLIGHT_ACTIVE Packet.";
            logger.Log(message);

            // Copy the coordinate data into the packet
            char* flightDataNew = new char[1 + sizeof(double)*3];       // Offset for needed flight instructions byte
            flightDataNew[0] = static_cast<char>(FLIGHT_ACTIVE);        // Get the flight instruction into the buffer at the start
            cords.copy_to_Buffer(flightDataNew+1);                      // Offset for needed flight instructions byte
            txPacket = Packet();
            txPacket.SetData(flightDataNew, 1 + sizeof(double)*3);

            if (!sendPacket(ClientSocket, txPacket))
            {
                logger.Log("[Client] Send failed. Disconnecting.\n\n");
                flightActive = false;   // Exit loop on send failure
                break;
            }

            // Log the instruction byte and the 3 double values in a readable format
            // we need to do this since our buffer is only a pointer
            /*memcpy(&x, flightDataNew + 1, sizeof(double));
            memcpy(&y, flightDataNew + 1 + sizeof(double), sizeof(double));
            memcpy(&z, flightDataNew + 1 + sizeof(double) * 2, sizeof(double));*/

            // Log data being sent to server
            message = string(1, txPacket.getInstruction()) + " | " + to_string(x) + ", " + to_string(y) + ", " + to_string(z);
            logger.LogSend(message);

            // [ADDED] Clean up the buffer now that SetData has made its own copy
            delete[] flightDataNew;

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
            ClientState nextState = getNextClientState(clientState, rxPacket.getInstruction());
            if (nextState != clientState)
            {
                logger.Log("[Client] COLLISION_ALERT received from server. Changing State to DivertCourse.\n");
                clientState = nextState; // Switch states to DivertCourse to receive collision aversion instructions
            }
            else if (rxPacket.getInstruction() == ACK)
                logger.Log("[Client] ACK received from server.\n");
            break;
        }



        // Collision Aversion State based on Server Instructions + Large Data Transfer
        case ClientState::DivertCourse:
        {
            if (aversionCoordinates.empty())
            {
                // Send a FLIGHT_ALERT_RESPONSE packet once to start the collision-aversion workflow
                string message = "[Client] Sending FLIGHT_ALERT_RESPONSE Packet.";
                logger.Log(message);
                if (!sendFlightAlertResponsePacket(ClientSocket))
                {
                    logger.Log("[Client] Failed to send FLIGHT_ALERT_RESPONSE packet.\n\n");
                    flightActive = false;
                    break;
                }

                char responseInstruction = static_cast<char>(FLIGHT_ALERT_RESPONSE);
                txPacket = Packet();
                txPacket.SetData(&responseInstruction, sizeof(responseInstruction));

                logger.LogSend(string(1, txPacket.getInstruction()));
                logger.Log("[Client] FLIGHT_ALERT_RESPONSE sent. Waiting for ACK...\n");

                if (!recvPacket(ClientSocket, rxPacket))
                {
                    logger.Log("[Client] No ACK received.\n\n");
                    flightActive = false;
                    break;
                }

                logger.LogReceive(string(1, rxPacket.getInstruction()));

                if (rxPacket.getInstruction() == ACK)
                {
                    logger.Log("[Client] FLIGHT_ALERT_RESPONSE 'ACK' received. Starting Large Data Transfer process...\n");

                    vector<char> imageBytes;
                    if (!loadBinaryFile("Images/Live_Feed1.png", imageBytes)) {
                        logger.Log("[Client] Failed to open image file.\n");
                        break;
                    }

                    sendLargeData(ClientSocket, imageBytes.data(), static_cast<int>(imageBytes.size()));

                    Packet instructionsPacket;
                    if (!recvPacket(ClientSocket, instructionsPacket))
                    {
                        logger.Log("[Client] Failed to receive collision aversion instructions.\n");
                        flightActive = false;
                        break;
                    }

                    logger.LogReceive(string(1, instructionsPacket.getInstruction()));

                    vector<Coordinate> receivedCoordinates;
                    if (!tryDeserializeCollisionAversionCoordinates(instructionsPacket, receivedCoordinates))
                    {
                        logger.Log("[Client] Invalid collision aversion instructions received.\n");
                        flightActive = false;
                        break;
                    }

                    char instructionAck = static_cast<char>(ACK);
                    txPacket = Packet();
                    txPacket.SetData(&instructionAck, 1);
                    if (!sendPacket(ClientSocket, txPacket))
                    {
                        logger.Log("[Client] Failed to ACK collision aversion instructions.\n");
                        flightActive = false;
                        break;
                    }

                    logger.LogSend(string(1, txPacket.getInstruction()));

                    aversionCoordinates = receivedCoordinates;
                    aversionCoordinateIndex = 0;
                    flightCoordinateIndex = advanceFlightPathIndex(
                        flightCoordinateIndex,
                        COLLISION_AVERSION_SKIP_COUNT,
                        flightPathCoordinates.size());
                    logger.Log("[Client] Collision aversion instructions received. Executing 5-step reroute.");
                }
            }

            if (aversionCoordinateIndex < aversionCoordinates.size())
            {
                this_thread::sleep_for(chrono::seconds(FLIGHT_TRANSMISSION_INTERVAL_SECONDS));

                cords = aversionCoordinates[aversionCoordinateIndex++];
                const double x = cords.get_X();
                const double y = cords.get_Y();
                const double z = cords.get_Z();

                logger.Log("[Client] Sending collision aversion coordinate.");

                char* flightDataNew = new char[1 + sizeof(double) * 3];
                flightDataNew[0] = static_cast<char>(FLIGHT_ACTIVE);
                cords.copy_to_Buffer(flightDataNew + 1);
                txPacket = Packet();
                txPacket.SetData(flightDataNew, 1 + sizeof(double) * 3);

                if (!sendPacket(ClientSocket, txPacket))
                {
                    delete[] flightDataNew;
                    logger.Log("[Client] Send failed during collision aversion. Disconnecting.\n\n");
                    flightActive = false;
                    break;
                }

                logger.LogSend(string(1, txPacket.getInstruction()) + " | " + to_string(x) + ", " + to_string(y) + ", " + to_string(z));
                delete[] flightDataNew;

                if (!recvPacket(ClientSocket, rxPacket))
                {
                    logger.Log("[Client] Receive failed during collision aversion. Disconnecting.\n\n");
                    flightActive = false;
                    break;
                }

                logger.LogReceive(string(1, rxPacket.getInstruction()));
                if (rxPacket.getInstruction() == ACK)
                    logger.Log("[Client] ACK received from server for collision aversion step.\n");

                if (aversionCoordinateIndex == aversionCoordinates.size())
                {
                    aversionCoordinates.clear();
                    aversionCoordinateIndex = 0;
                    clientState = ClientState::Flying;
                    logger.Log("[Client] Collision aversion complete. Returning to Flying state.");
                }
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


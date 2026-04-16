#include "ClientHelpers.h"
#include <thread>
#include <cstdlib>
#include <ctime>
#include <fstream>

using namespace std;
using std::ifstream;

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
    Coordinate cords = Coordinate();

    // ================================================
    // =============== Main Client Loop ===============
    // ================================================
    
    // Packet objects reused each iteration — one for sending, one for receiving
    Packet txPacket;
    Packet rxPacket;

    // flightActive controls the while loop — stays true until the FLIGHT_DONE handshake completes
    bool flightActive = true;

    // Open the flight path file before the loop begins
    // Each line contains one X,Y,Z coordinate set sent every 10 seconds
    ifstream flightFile(flightPathFile);
    if (!flightFile.is_open())
    {
        logger.Log("[Client] ERROR: Failed to open file: " + flightPathFile);
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
            this_thread::sleep_for(chrono::seconds(10));

            // Read the next line from the flight path file
            string line;
            bool foundLine = false;

            // Loop only while we read a line AND it's invalid (empty/comment)
            while (getline(flightFile, line) && (line.empty() || line[0] == '#')) 
            {// do nothing — just skip invalid lines
            }

            // If we failed to read a valid line, we're at EOF
            if (!flightFile)
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

            // Parse the X,Y,Z values from the comma-separated line
            double x = 0.0, y = 0.0, z = 0.0;
            try
            {
                size_t firstComma = line.find(',');
                size_t secondComma = line.find(',', firstComma + 1);

                if (firstComma == string::npos || secondComma == string::npos)
                {
                    logger.Log("[Client] Malformed line in flightpath.txt: " + line + " — skipping.");
                    break;
                }

                x = stod(line.substr(0, firstComma));
                y = stod(line.substr(firstComma + 1, secondComma - firstComma - 1));
                z = stod(line.substr(secondComma + 1));
            }
            catch (const exception& e)
            {
                logger.Log("[Client] Failed to parse coordinate line: " + line + " | Error: " + e.what());
                break;
            }

            // Set the parsed values into the Coordinate object
            cords.set_X(x);
            cords.set_Y(y);
            cords.set_Z(z);

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
            string message = "[Client] Sending FLIGHT_ALERT_RESPONSE Packet.";
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

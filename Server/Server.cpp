#include "ServerHelpers.h"
#include <thread>
#include <vector>


int main()
{
	// Initialize Logger
	Logger logger;

	//starts Winsock DLLs		
	WSADATA wsaData;
	if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0)
		return 0;

	//create server socket
	SOCKET ServerSocket;
	ServerSocket = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
	if (ServerSocket == INVALID_SOCKET) {
		WSACleanup();
		return 0;
	}

	//binds socket to address
	sockaddr_in SvrAddr;
	SvrAddr.sin_family = AF_INET;
	SvrAddr.sin_addr.s_addr = INADDR_ANY;
	SvrAddr.sin_port = htons(27000);
	if (bind(ServerSocket, (struct sockaddr*)&SvrAddr, sizeof(SvrAddr)) == SOCKET_ERROR)
	{
		closesocket(ServerSocket);
		WSACleanup();
		return 0;
	}

	// listen() uses SOMAXCONN so the OS queues multiple incoming connections while the accept loop is busy.
	if (listen(ServerSocket, SOMAXCONN) == SOCKET_ERROR) {
		closesocket(ServerSocket);
		WSACleanup();
		return 0;
	}

	// Passkey gate — prompt the user for the start passkey before the server begins accepting connections. Loops until correct key is entered.
	{
		string userInput;
		logger.Log("[Server] Enter passkey to start Ground Control: ");
		while (true)
		{
			cin >> userInput;
			if (userInput == START_PASSKEY)
			{
				logger.Log("[Server] Passkey accepted. Ground Control is now ACTIVE.");
				logger.Log("[Server] Type \"x\" at any time to stop the server.\n");
				break;
			}
			else
			{
				logger.Log("[Server] Incorrect passkey. Try again: ");
			}
		}
	}

	// Set the running flag to true now that the passkey has been accepted
	serverRunning = true;

	// Track all spawned threads so we can join them on shutdown, and assign each client a simple numeric ID for logging.
	vector<thread> clientThreads;
	int clientID = 0;

	// Spawn the input monitor on its own thread so it can listen for "x" concurrently without blocking the accept loop below.
	thread inputThread(inputMonitor, ServerSocket);

	// The single accept() call is now an accept loop so the server keeps accepting new clients instead of handling just one.
	SOCKET ConnectionSocket;
	ConnectionSocket = SOCKET_ERROR;

	// The loop continues running based on the serverRunning variable
	while (serverRunning)
	{
		logger.Log("Waiting to accept a client connection... \n\n");

		if ((ConnectionSocket = accept(ServerSocket, NULL, NULL)) == SOCKET_ERROR) {
			// Only treat this as a hard error if the server is still supposed to be running.
			// If serverRunning is false, the socket was closed intentionally by inputMonitor() to unblock accept() — not an error.
			if (!serverRunning) break;
			closesocket(ServerSocket);
			WSACleanup();
			return 0;
		}

		// Increment client counter and spawn a dedicated thread for this client.
		// The thread runs handleClient() independently so main() loops back immediately to accept the next incoming connection.
		clientID++;
		clientThreads.emplace_back(thread(handleClient, ConnectionSocket, clientID));

		// Log total number of threads spawned so far
		string message = "[Server] New Client Accepted. Total clients accepted: " + clientID;
		logger.Log(message + "\n" + "\n");
	}

	// [ADDED] Wait for the input monitor thread to finish before cleaning up
	if (inputThread.joinable()) inputThread.join();

	// Join all client threads before shutting down (ensures a clean exit).
	for (auto& t : clientThreads)
		if (t.joinable()) t.join();

	closesocket(ConnectionSocket);	//closes incoming socket
	closesocket(ServerSocket);	    //closes server socket	
	WSACleanup();					//frees Winsock resources
	return 1;
}

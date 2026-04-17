#include "pch.h"
#include "CppUnitTest.h"
#include "../Client/ClientHelpers.h"
#include "../Server/Constants.h"
#include "../Server/Coordinate.h"
#include "../Server/Packet.h"
#include "../Server/ServerHelpers.h"
#include <array>
#include <chrono>
#include <cstdio>
#include <fstream>
#include <future>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

using namespace Microsoft::VisualStudio::CppUnitTestFramework;
using namespace std;

bool server_recvPacket(SOCKET sock, Packet& outPacket);
bool server_sendPacket(SOCKET sock, Packet& packet);

namespace
{
    bool ServerReceivePacket(SOCKET sock, Packet& outPacket)
    {
        return server_recvPacket(sock, outPacket);
    }

    bool ServerSendPacket(SOCKET sock, Packet& packet)
    {
        return server_sendPacket(sock, packet);
    }

    void CloseSocketIfOpen(SOCKET& socketHandle)
    {
        if (socketHandle != INVALID_SOCKET)
        {
            closesocket(socketHandle);
            socketHandle = INVALID_SOCKET;
        }
    }

    class MockClient
    {
    public:
        SOCKET socketHandle = INVALID_SOCKET;

        void Create()
        {
            socketHandle = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
            Assert::AreNotEqual(static_cast<SOCKET>(INVALID_SOCKET), socketHandle, L"Failed to create client socket.");
        }

        void Connect(const sockaddr_in& address)
        {
            int connectResult = ::connect(socketHandle, reinterpret_cast<const sockaddr*>(&address), sizeof(address));
            Assert::AreEqual(0, connectResult, L"Failed to connect client socket.");
        }

        bool SendPacket(Packet& packet)
        {
            return sendPacket(socketHandle, packet);
        }

        bool ReceivePacket(Packet& packet)
        {
            return recvPacket(socketHandle, packet);
        }

        bool SendLargePayload(const vector<char>& payload)
        {
            return sendLargeData(socketHandle, payload.data(), static_cast<int>(payload.size()));
        }

        void Close()
        {
            CloseSocketIfOpen(socketHandle);
        }
    };

    class MockServer
    {
    public:
        SOCKET listenerSocket = INVALID_SOCKET;
        SOCKET connectionSocket = INVALID_SOCKET;
        sockaddr_in boundAddress = {};

        void StartListening(unsigned short port = 0, unsigned long addressValue = htonl(INADDR_LOOPBACK))
        {
            listenerSocket = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
            Assert::AreNotEqual(static_cast<SOCKET>(INVALID_SOCKET), listenerSocket, L"Failed to create listener socket.");

            sockaddr_in address = {};
            address.sin_family = AF_INET;
            address.sin_addr.s_addr = addressValue;
            address.sin_port = htons(port);

            int bindResult = ::bind(listenerSocket, reinterpret_cast<sockaddr*>(&address), sizeof(address));
            Assert::AreEqual(0, bindResult, L"Failed to bind listener socket.");

            int listenResult = ::listen(listenerSocket, SOMAXCONN);
            Assert::AreEqual(0, listenResult, L"Failed to listen on socket.");

            int boundLength = sizeof(boundAddress);
            int nameResult = ::getsockname(listenerSocket, reinterpret_cast<sockaddr*>(&boundAddress), &boundLength);
            Assert::AreEqual(0, nameResult, L"Failed to inspect listener address.");
        }

        void AcceptOneClient()
        {
            connectionSocket = ::accept(listenerSocket, nullptr, nullptr);
            Assert::AreNotEqual(static_cast<SOCKET>(INVALID_SOCKET), connectionSocket, L"Failed to accept server socket.");
        }

        SOCKET AcceptAnotherClient()
        {
            SOCKET acceptedSocket = ::accept(listenerSocket, nullptr, nullptr);
            Assert::AreNotEqual(static_cast<SOCKET>(INVALID_SOCKET), acceptedSocket, L"Failed to accept additional server socket.");
            return acceptedSocket;
        }

        bool SendPacket(Packet& packet)
        {
            return ServerSendPacket(connectionSocket, packet);
        }

        bool ReceivePacket(Packet& packet)
        {
            return ServerReceivePacket(connectionSocket, packet);
        }

        vector<char> ReceiveLargePayload(int clientID)
        {
            return recvLargeData(connectionSocket, clientID);
        }

        void Close()
        {
            CloseSocketIfOpen(connectionSocket);
            CloseSocketIfOpen(listenerSocket);
        }
    };

    struct MockSession
    {
        MockClient client;
        MockServer server;

        static MockSession CreateConnected(unsigned short port = 0)
        {
            MockSession session;
            session.server.StartListening(port);
            session.client.Create();
            session.client.Connect(session.server.boundAddress);
            session.server.AcceptOneClient();
            return session;
        }

        void Close()
        {
            client.Close();
            server.Close();
        }
    };

    struct MockServerWithClients
    {
        MockServer server;
        vector<MockClient> clients;
        vector<SOCKET> acceptedSockets;

        static MockServerWithClients CreateConnected(size_t clientCount)
        {
            MockServerWithClients fixture;
            fixture.server.StartListening();
            fixture.clients.resize(clientCount);

            for (MockClient& client : fixture.clients)
            {
                client.Create();
                client.Connect(fixture.server.boundAddress);
                fixture.acceptedSockets.push_back(fixture.server.AcceptAnotherClient());
            }

            if (!fixture.acceptedSockets.empty())
                fixture.server.connectionSocket = fixture.acceptedSockets.front();

            return fixture;
        }

        void Close()
        {
            for (MockClient& client : clients)
                client.Close();

            for (SOCKET& acceptedSocket : acceptedSockets)
                CloseSocketIfOpen(acceptedSocket);

            server.connectionSocket = INVALID_SOCKET;
            server.Close();
        }
    };

    Packet BuildInstructionOnlyPacket(unsigned char instruction)
    {
        Packet packet;
        char data[] = { static_cast<char>(instruction) };
        packet.SetData(data, static_cast<int>(sizeof(data)));
        return packet;
    }

    Packet BuildFlightActivePacket(double x, double y, double z)
    {
        Packet packet;
        array<char, 1 + sizeof(double) * 3> data = {};
        data[0] = static_cast<char>(FLIGHT_ACTIVE);
        memcpy(data.data() + 1, &x, sizeof(double));
        memcpy(data.data() + 1 + sizeof(double), &y, sizeof(double));
        memcpy(data.data() + 1 + sizeof(double) * 2, &z, sizeof(double));
        packet.SetData(data.data(), static_cast<int>(data.size()));
        return packet;
    }

    vector<char> BuildTestBytes(size_t size)
    {
        vector<char> data(size);
        for (size_t i = 0; i < size; ++i)
            data[i] = static_cast<char>(i % 251);
        return data;
    }

    string UniqueTestName(const string& prefix, const string& extension)
    {
        auto stamp = chrono::steady_clock::now().time_since_epoch().count();
        return prefix + "_" + to_string(stamp) + extension;
    }

    string CreateTempFile(const string& fileName, const string& contents, bool binary = false)
    {
        ios::openmode mode = ios::out | ios::trunc;
        if (binary)
            mode |= ios::binary;

        ofstream output(fileName, mode);
        output << contents;
        output.close();
        return fileName;
    }

    string CreateTempBinaryFile(const string& fileName, const vector<char>& contents)
    {
        ofstream output(fileName, ios::binary | ios::trunc);
        output.write(contents.data(), static_cast<streamsize>(contents.size()));
        output.close();
        return fileName;
    }

    string ReadTextFile(const string& fileName)
    {
        ifstream input(fileName, ios::in | ios::binary);
        stringstream buffer;
        buffer << input.rdbuf();
        return buffer.str();
    }

    void AssertBuffersEqual(const vector<char>& expected, const vector<char>& actual)
    {
        Assert::AreEqual(expected.size(), actual.size(), L"Buffer sizes differ.");
        for (size_t i = 0; i < expected.size(); ++i)
            Assert::AreEqual(static_cast<int>(expected[i]), static_cast<int>(actual[i]));
    }
}

namespace UnitTests
{
    TEST_CLASS(ClientTests)
    {
    public:
        TEST_CLASS_INITIALIZE(InitializeWinsock)
        {
            WSADATA wsaData = {};
            Assert::AreEqual(0, WSAStartup(MAKEWORD(2, 2), &wsaData), L"WSAStartup failed.");
        }

        TEST_CLASS_CLEANUP(CleanupWinsock)
        {
            WSACleanup();
        }

        TEST_METHOD(UT_CLT_001_SingleServerConnection)
        {
            MockSession session = MockSession::CreateConnected();

            Packet clientPacket = BuildInstructionOnlyPacket(ACK);
            Assert::IsTrue(session.client.SendPacket(clientPacket));

            Packet received;
            Assert::IsTrue(session.server.ReceivePacket(received));
            Assert::AreEqual(static_cast<int>(ACK), static_cast<int>(received.getInstruction()));

            session.Close();
        }

        TEST_METHOD(UT_CLT_002_AwaitServerResponse_BlocksUntilPacketArrives)
        {
            MockSession session = MockSession::CreateConnected();

            auto receiveFuture = async(launch::async, [&]() {
                Packet received;
                bool ok = session.client.ReceivePacket(received);
                return make_pair(ok, received.getInstruction());
                });

            Assert::IsTrue(receiveFuture.wait_for(chrono::milliseconds(150)) == future_status::timeout);

            Packet ack = BuildInstructionOnlyPacket(ACK);
            Assert::IsTrue(session.server.SendPacket(ack));

            auto result = receiveFuture.get();
            Assert::IsTrue(result.first);
            Assert::AreEqual(static_cast<int>(ACK), static_cast<int>(result.second));

            session.Close();
        }

        TEST_METHOD(UT_CLT_003_ReadFlightPathFile_LoadsCoordinates)
        {
            string fileName = UniqueTestName("gta_flightpath_valid", ".txt");
            string path = CreateTempFile(fileName, "# heading\n1.5,2.5,3.5\n\n4,5,6\n");

            vector<Coordinate> coordinates;
            bool loaded = loadFlightPathCoordinates(path, coordinates);

            Assert::IsTrue(loaded);
            Assert::AreEqual(static_cast<size_t>(2), coordinates.size());
            Assert::AreEqual(1.5, coordinates[0].get_X(), 0.0001);
            Assert::AreEqual(6.0, coordinates[1].get_Z(), 0.0001);

            remove(path.c_str());
        }

        TEST_METHOD(UT_CLT_004_MissingFlightPathFile_ReturnsFalse)
        {
            vector<Coordinate> coordinates;
            bool loaded = loadFlightPathCoordinates("does_not_exist_flightpath.txt", coordinates);

            Assert::IsFalse(loaded);
            Assert::AreEqual(static_cast<size_t>(0), coordinates.size());
        }

        TEST_METHOD(UT_CLT_005_MalformedFlightPathFile_ReturnsFalse)
        {
            string fileName = UniqueTestName("gta_flightpath_bad", ".txt");
            string path = CreateTempFile(fileName, "1.0,2.0,3.0\nbad,line\n");

            vector<Coordinate> coordinates;
            bool loaded = loadFlightPathCoordinates(path, coordinates);

            Assert::IsFalse(loaded);
            Assert::AreEqual(static_cast<size_t>(1), coordinates.size());

            remove(path.c_str());
        }

        TEST_METHOD(UT_CLT_006_StateTransitionOnCollisionAlert)
        {
            ClientState nextState = getNextClientState(ClientState::Flying, COLLISION_ALERT);
            Assert::IsTrue(nextState == ClientState::DivertCourse);
        }

        TEST_METHOD(UT_CLT_007_FlightPathAdjustment)
        {
            MockSession session = MockSession::CreateConnected();

            Assert::IsTrue(sendFlightAlertResponsePacket(session.client.socketHandle));

            Packet received;
            Assert::IsTrue(session.server.ReceivePacket(received));
            Assert::AreEqual(static_cast<int>(FLIGHT_ALERT_RESPONSE), static_cast<int>(received.getInstruction()));

            session.Close();
        }

        TEST_METHOD(UT_CLT_008_ImageFileLoad_ReadsFileIntoBuffer)
        {
            vector<char> expected = BuildTestBytes(512);
            string fileName = UniqueTestName("gta_image_valid", ".bin");
            string path = CreateTempBinaryFile(fileName, expected);

            vector<char> actual;
            bool loaded = loadBinaryFile(path, actual);

            Assert::IsTrue(loaded);
            AssertBuffersEqual(expected, actual);

            remove(path.c_str());
        }

        TEST_METHOD(UT_CLT_009_ImageFileNotFound_ReturnsFalse)
        {
            vector<char> actual;
            bool loaded = loadBinaryFile("missing_image.bin", actual);

            Assert::IsFalse(loaded);
            Assert::AreEqual(static_cast<size_t>(0), actual.size());
        }

        TEST_METHOD(UT_CLT_010_CorrectChunkSize_UsesMaximum254ByteChunks)
        {
            vector<char> data = BuildTestBytes(700);
            vector<vector<char>> chunks = splitLargeDataChunks(data.data(), static_cast<int>(data.size()));

            Assert::AreEqual(static_cast<size_t>(3), chunks.size());
            Assert::AreEqual(static_cast<size_t>(254), chunks[0].size());
            Assert::AreEqual(static_cast<size_t>(254), chunks[1].size());
            Assert::AreEqual(static_cast<size_t>(192), chunks[2].size());
        }

        TEST_METHOD(UT_USE_004_FlightPathFileFormat)
        {
            string fileName = UniqueTestName("gta_flightpath_usable", ".txt");
            string path = CreateTempFile(
                fileName,
                "# Example flight path\n"
                "# X,Y,Z coordinates\n"
                "100,200,300\n"
                "\n"
                "150.5,210.25,305.75\n"
            );

            vector<Coordinate> coordinates;
            bool loaded = loadFlightPathCoordinates(path, coordinates);

            Assert::IsTrue(loaded);
            Assert::AreEqual(static_cast<size_t>(2), coordinates.size());
            Assert::AreEqual(100.0, coordinates[0].get_X(), 0.0001);
            Assert::AreEqual(200.0, coordinates[0].get_Y(), 0.0001);
            Assert::AreEqual(300.0, coordinates[0].get_Z(), 0.0001);
            Assert::AreEqual(150.5, coordinates[1].get_X(), 0.0001);
            Assert::AreEqual(210.25, coordinates[1].get_Y(), 0.0001);
            Assert::AreEqual(305.75, coordinates[1].get_Z(), 0.0001);

            remove(path.c_str());
        }
    };

    TEST_CLASS(ServerTests)
    {
    public:
        TEST_CLASS_INITIALIZE(InitializeWinsock)
        {
            WSADATA wsaData = {};
            Assert::AreEqual(0, WSAStartup(MAKEWORD(2, 2), &wsaData), L"WSAStartup failed.");
        }

        TEST_CLASS_CLEANUP(CleanupWinsock)
        {
            WSACleanup();
        }

        TEST_METHOD(UT_SVR_001_AcceptMultipleClients)
        {
            MockServerWithClients fixture = MockServerWithClients::CreateConnected(2);
            Assert::AreEqual(static_cast<size_t>(2), fixture.acceptedSockets.size());
            fixture.Close();
        }

        TEST_METHOD(UT_SVR_002_ServerBlocksOnReceive_UntilClientSends)
        {
            MockSession session = MockSession::CreateConnected();

            auto receiveFuture = async(launch::async, [&]() {
                Packet packet;
                bool ok = session.server.ReceivePacket(packet);
                return make_pair(ok, packet.getInstruction());
                });

            Assert::IsTrue(receiveFuture.wait_for(chrono::milliseconds(150)) == future_status::timeout);

            Packet ack = BuildInstructionOnlyPacket(ACK);
            Assert::IsTrue(session.client.SendPacket(ack));

            auto result = receiveFuture.get();
            Assert::IsTrue(result.first);
            Assert::AreEqual(static_cast<int>(ACK), static_cast<int>(result.second));

            session.Close();
        }

        TEST_METHOD(UT_SVR_003_CollisionDetection)
        {
        }

        TEST_METHOD(UT_SVR_004_ACK_On_FLIGHT_ACTIVE)
        {
            MockSession session = MockSession::CreateConnected();
            thread serverThread(handleClient, session.server.connectionSocket, 1);
            session.server.connectionSocket = INVALID_SOCKET;

            Packet flightActive = BuildFlightActivePacket(10.0, 20.0, 30.0);
            Assert::IsTrue(session.client.SendPacket(flightActive));

            Packet response;
            Assert::IsTrue(session.client.ReceivePacket(response));
            Assert::AreEqual(static_cast<int>(ACK), static_cast<int>(response.getInstruction()));

            Packet done = BuildInstructionOnlyPacket(FLIGHT_DONE);
            Assert::IsTrue(session.client.SendPacket(done));
            Assert::IsTrue(session.client.ReceivePacket(response));
            Assert::AreEqual(static_cast<int>(ACK), static_cast<int>(response.getInstruction()));

            serverThread.join();
            session.Close();
        }

        TEST_METHOD(UT_SVR_005_COLLISION_ALERT_Response)
        {
        }

        TEST_METHOD(UT_SVR_006_ACK_On_FLIGHT_DONE)
        {
            MockSession session = MockSession::CreateConnected();
            thread serverThread(handleClient, session.server.connectionSocket, 2);
            session.server.connectionSocket = INVALID_SOCKET;

            Packet done = BuildInstructionOnlyPacket(FLIGHT_DONE);
            Assert::IsTrue(session.client.SendPacket(done));

            Packet response;
            Assert::IsTrue(session.client.ReceivePacket(response));
            Assert::AreEqual(static_cast<int>(ACK), static_cast<int>(response.getInstruction()));

            serverThread.join();
            session.Close();
        }

        TEST_METHOD(UT_SVR_007_ReceiveDataStartPacket_ContainsTotalSize)
        {
            MockSession session = MockSession::CreateConnected();
            vector<char> payload = BuildTestBytes(400);

            Assert::IsTrue(session.client.SendLargePayload(payload));

            Packet startPacket;
            Assert::IsTrue(session.server.ReceivePacket(startPacket));
            Assert::AreEqual(static_cast<int>(DATA_START), static_cast<int>(startPacket.getInstruction()));

            int totalSize = 0;
            memcpy(&totalSize, startPacket.getData(), sizeof(totalSize));
            Assert::AreEqual(static_cast<int>(payload.size()), totalSize);

            session.Close();
        }

        TEST_METHOD(UT_SVR_008_ReassembleImageChunks_IntoOriginalBuffer)
        {
            MockSession session = MockSession::CreateConnected();
            vector<char> payload = BuildTestBytes(1024);

            Assert::IsTrue(session.client.SendLargePayload(payload));

            vector<char> result = session.server.ReceiveLargePayload(1);
            AssertBuffersEqual(payload, result);

            session.Close();
        }

        TEST_METHOD(UT_SVR_009_WriteReceivedImageToFile)
        {
            vector<char> payload = BuildTestBytes(2048);
            string fileName = UniqueTestName("received_image_test", ".bin");

            Assert::IsTrue(writeBinaryFile(fileName, payload));

            vector<char> writtenBytes;
            Assert::IsTrue(loadBinaryFile(fileName, writtenBytes));
            AssertBuffersEqual(payload, writtenBytes);

            remove(fileName.c_str());
        }

        TEST_METHOD(UT_SVR_010_ParallelClientThreads_BothClientsReceiveACK)
        {
            MockSession sessionA = MockSession::CreateConnected();
            MockSession sessionB = MockSession::CreateConnected();

            thread serverThreadA(handleClient, sessionA.server.connectionSocket, 1);
            thread serverThreadB(handleClient, sessionB.server.connectionSocket, 2);
            sessionA.server.connectionSocket = INVALID_SOCKET;
            sessionB.server.connectionSocket = INVALID_SOCKET;

            auto clientA = async(launch::async, [&]() {
                Packet response;
                Packet active = BuildFlightActivePacket(1.0, 2.0, 3.0);
                bool sendOk = sessionA.client.SendPacket(active);
                bool recvOk = sessionA.client.ReceivePacket(response);
                Packet done = BuildInstructionOnlyPacket(FLIGHT_DONE);
                bool doneSend = sessionA.client.SendPacket(done);
                bool doneRecv = sessionA.client.ReceivePacket(response);
                return sendOk && recvOk && doneSend && doneRecv;
                });

            auto clientB = async(launch::async, [&]() {
                Packet response;
                Packet active = BuildFlightActivePacket(4.0, 5.0, 6.0);
                bool sendOk = sessionB.client.SendPacket(active);
                bool recvOk = sessionB.client.ReceivePacket(response);
                Packet done = BuildInstructionOnlyPacket(FLIGHT_DONE);
                bool doneSend = sessionB.client.SendPacket(done);
                bool doneRecv = sessionB.client.ReceivePacket(response);
                return sendOk && recvOk && doneSend && doneRecv;
                });

            Assert::IsTrue(clientA.get());
            Assert::IsTrue(clientB.get());

            serverThreadA.join();
            serverThreadB.join();
            sessionA.Close();
            sessionB.Close();
        }

        TEST_METHOD(UT_SVR_011_ThreadIsolation_OneDisconnectDoesNotAffectOtherClient)
        {
            MockSession sessionA = MockSession::CreateConnected();
            MockSession sessionB = MockSession::CreateConnected();

            thread serverThreadA(handleClient, sessionA.server.connectionSocket, 1);
            thread serverThreadB(handleClient, sessionB.server.connectionSocket, 2);
            sessionA.server.connectionSocket = INVALID_SOCKET;
            sessionB.server.connectionSocket = INVALID_SOCKET;

            sessionA.client.Close();
            serverThreadA.join();

            Packet active = BuildFlightActivePacket(7.0, 8.0, 9.0);
            Assert::IsTrue(sessionB.client.SendPacket(active));

            Packet response;
            Assert::IsTrue(sessionB.client.ReceivePacket(response));
            Assert::AreEqual(static_cast<int>(ACK), static_cast<int>(response.getInstruction()));

            Packet done = BuildInstructionOnlyPacket(FLIGHT_DONE);
            Assert::IsTrue(sessionB.client.SendPacket(done));
            Assert::IsTrue(sessionB.client.ReceivePacket(response));
            Assert::AreEqual(static_cast<int>(ACK), static_cast<int>(response.getInstruction()));

            serverThreadB.join();
            sessionA.Close();
            sessionB.Close();
        }

        TEST_METHOD(UT_USE_001_PasskeyPromptClarity)
        {
            string prompt = SERVER_START_PROMPT;
            Assert::IsTrue(prompt.find("passkey") != string::npos);
            Assert::IsTrue(prompt.find("Ground Control") != string::npos);
            Assert::IsTrue(prompt.find(":") != string::npos);
        }

        TEST_METHOD(UT_USE_002_ServerShutdownCommand)
        {
            MockServer server;
            server.StartListening();

            istringstream input("x\n");
            streambuf* originalBuffer = cin.rdbuf(input.rdbuf());

            serverRunning = true;
            thread shutdownThread(inputMonitor, server.listenerSocket);
            shutdownThread.join();

            cin.rdbuf(originalBuffer);

            Assert::IsFalse(serverRunning.load());
            CloseSocketIfOpen(server.listenerSocket);
        }
    };

    TEST_CLASS(SystemTests)
    {
    public:
        TEST_CLASS_INITIALIZE(InitializeWinsock)
        {
            WSADATA wsaData = {};
            Assert::AreEqual(0, WSAStartup(MAKEWORD(2, 2), &wsaData), L"WSAStartup failed.");
        }

        TEST_CLASS_CLEANUP(CleanupWinsock)
        {
            WSACleanup();
        }

        TEST_METHOD(IT_001_FullConnectionHandshake)
        {
            MockSession session = MockSession::CreateConnected();
            Assert::AreNotEqual(static_cast<SOCKET>(INVALID_SOCKET), session.client.socketHandle);
            Assert::AreNotEqual(static_cast<SOCKET>(INVALID_SOCKET), session.server.connectionSocket);

            Packet handshakePacket = BuildInstructionOnlyPacket(ACK);
            Assert::IsTrue(session.client.SendPacket(handshakePacket));

            Packet received;
            Assert::IsTrue(session.server.ReceivePacket(received));
            Assert::AreEqual(static_cast<int>(ACK), static_cast<int>(received.getInstruction()));

            session.Close();
        }

        TEST_METHOD(IT_002_FLIGHT_ACTIVE_Exchange_EndToEnd)
        {
            MockSession session = MockSession::CreateConnected();
            thread serverThread(handleClient, session.server.connectionSocket, 3);
            session.server.connectionSocket = INVALID_SOCKET;

            Packet flightActive = BuildFlightActivePacket(11.0, 12.0, 13.0);
            Assert::IsTrue(session.client.SendPacket(flightActive));

            Packet response;
            Assert::IsTrue(session.client.ReceivePacket(response));
            Assert::AreEqual(static_cast<int>(ACK), static_cast<int>(response.getInstruction()));

            Packet done = BuildInstructionOnlyPacket(FLIGHT_DONE);
            Assert::IsTrue(session.client.SendPacket(done));
            Assert::IsTrue(session.client.ReceivePacket(response));

            serverThread.join();
            session.Close();
        }

        TEST_METHOD(IT_003_CoordinateDataIntegrity_ServerReceivesExactXYZ)
        {
            MockSession session = MockSession::CreateConnected();

            Packet packet = BuildFlightActivePacket(123.456, -45.5, 999.25);
            Assert::IsTrue(session.client.SendPacket(packet));

            Packet received;
            Assert::IsTrue(session.server.ReceivePacket(received));
            Assert::AreEqual(static_cast<int>(FLIGHT_ACTIVE), static_cast<int>(received.getInstruction()));

            double x = 0.0;
            double y = 0.0;
            double z = 0.0;
            memcpy(&x, received.getData(), sizeof(double));
            memcpy(&y, received.getData() + sizeof(double), sizeof(double));
            memcpy(&z, received.getData() + sizeof(double) * 2, sizeof(double));

            Assert::AreEqual(123.456, x, 0.0001);
            Assert::AreEqual(-45.5, y, 0.0001);
            Assert::AreEqual(999.25, z, 0.0001);

            session.Close();
        }

        TEST_METHOD(IT_004_CollisionAlertFullFlow)
        {
        }

        TEST_METHOD(IT_005_LargeDataTransferIntegrity)
        {
            MockSession session = MockSession::CreateConnected();
            vector<char> payload = BuildTestBytes(1024 * 1024 + 128);
            ostringstream suppressedConsole;
            streambuf* originalCoutBuffer = cout.rdbuf(suppressedConsole.rdbuf());

            auto sendFuture = async(launch::async, [&]() {
                return session.client.SendLargePayload(payload);
                });

            vector<char> result = session.server.ReceiveLargePayload(5);
            cout.rdbuf(originalCoutBuffer);
            Assert::IsTrue(sendFuture.get());
            AssertBuffersEqual(payload, result);

            session.Close();
        }

        TEST_METHOD(IT_006_FLIGHT_DONE_Termination)
        {
            MockSession session = MockSession::CreateConnected();
            thread serverThread(handleClient, session.server.connectionSocket, 6);
            session.server.connectionSocket = INVALID_SOCKET;

            Packet done = BuildInstructionOnlyPacket(FLIGHT_DONE);
            Assert::IsTrue(session.client.SendPacket(done));

            Packet response;
            Assert::IsTrue(session.client.ReceivePacket(response));
            Assert::AreEqual(static_cast<int>(ACK), static_cast<int>(response.getInstruction()));

            serverThread.join();
            session.Close();
        }

        TEST_METHOD(IT_007_MultiClientSimultaneousTransfer)
        {
            MockSession sessionA = MockSession::CreateConnected();
            MockSession sessionB = MockSession::CreateConnected();

            thread serverThreadA(handleClient, sessionA.server.connectionSocket, 7);
            thread serverThreadB(handleClient, sessionB.server.connectionSocket, 8);
            sessionA.server.connectionSocket = INVALID_SOCKET;
            sessionB.server.connectionSocket = INVALID_SOCKET;

            auto clientA = async(launch::async, [&]() {
                Packet response;
                Packet active = BuildFlightActivePacket(21.0, 22.0, 23.0);
                bool sendOk = sessionA.client.SendPacket(active);
                bool recvOk = sessionA.client.ReceivePacket(response);
                Packet done = BuildInstructionOnlyPacket(FLIGHT_DONE);
                bool doneSend = sessionA.client.SendPacket(done);
                bool doneRecv = sessionA.client.ReceivePacket(response);
                return sendOk && recvOk && doneSend && doneRecv;
                });

            auto clientB = async(launch::async, [&]() {
                Packet response;
                Packet active = BuildFlightActivePacket(31.0, 32.0, 33.0);
                bool sendOk = sessionB.client.SendPacket(active);
                bool recvOk = sessionB.client.ReceivePacket(response);
                Packet done = BuildInstructionOnlyPacket(FLIGHT_DONE);
                bool doneSend = sessionB.client.SendPacket(done);
                bool doneRecv = sessionB.client.ReceivePacket(response);
                return sendOk && recvOk && doneSend && doneRecv;
                });

            Assert::IsTrue(clientA.get());
            Assert::IsTrue(clientB.get());

            serverThreadA.join();
            serverThreadB.join();
            sessionA.Close();
            sessionB.Close();
        }

        TEST_METHOD(IT_008_TenSecondTransmissionInterval)
        {
            Assert::AreEqual(10, FLIGHT_TRANSMISSION_INTERVAL_SECONDS);
        }

        TEST_METHOD(UT_USE_003_LogFileReadability)
        {
            remove("log.txt");

            ::Logger readableLogger;
            readableLogger.LogSend("w | 1.000000, 2.000000, 3.000000");

            string logContents = ReadTextFile("log.txt");
            Assert::IsTrue(logContents.find(" - PACKET|DIR=SEND|DATA=") != string::npos);
            Assert::IsTrue(logContents.find("1.000000, 2.000000, 3.000000") != string::npos);
        }

        TEST_METHOD(ST_001_FullSystemHappyPath)
        {
            MockSession session = MockSession::CreateConnected();
            thread serverThread(handleClient, session.server.connectionSocket, 9);
            session.server.connectionSocket = INVALID_SOCKET;

            Packet active = BuildFlightActivePacket(100.0, 200.0, 300.0);
            Assert::IsTrue(session.client.SendPacket(active));

            Packet response;
            Assert::IsTrue(session.client.ReceivePacket(response));
            Assert::AreEqual(static_cast<int>(ACK), static_cast<int>(response.getInstruction()));

            Packet done = BuildInstructionOnlyPacket(FLIGHT_DONE);
            Assert::IsTrue(session.client.SendPacket(done));
            Assert::IsTrue(session.client.ReceivePacket(response));
            Assert::AreEqual(static_cast<int>(ACK), static_cast<int>(response.getInstruction()));

            serverThread.join();
            session.Close();
        }

        TEST_METHOD(ST_002_MultiClientFullSession)
        {
            MockSession sessionA = MockSession::CreateConnected();
            MockSession sessionB = MockSession::CreateConnected();
            MockSession sessionC = MockSession::CreateConnected();

            thread serverThreadA(handleClient, sessionA.server.connectionSocket, 10);
            thread serverThreadB(handleClient, sessionB.server.connectionSocket, 11);
            thread serverThreadC(handleClient, sessionC.server.connectionSocket, 12);
            sessionA.server.connectionSocket = INVALID_SOCKET;
            sessionB.server.connectionSocket = INVALID_SOCKET;
            sessionC.server.connectionSocket = INVALID_SOCKET;

            auto runClientSession = [](MockSession& session, double baseValue) {
                Packet response;
                Packet active = BuildFlightActivePacket(baseValue, baseValue + 1.0, baseValue + 2.0);
                bool sendOk = session.client.SendPacket(active);
                bool recvOk = session.client.ReceivePacket(response);
                Packet done = BuildInstructionOnlyPacket(FLIGHT_DONE);
                bool doneSend = session.client.SendPacket(done);
                bool doneRecv = session.client.ReceivePacket(response);
                return sendOk && recvOk && doneSend && doneRecv;
                };

            auto clientA = async(launch::async, [&]() { return runClientSession(sessionA, 10.0); });
            auto clientB = async(launch::async, [&]() { return runClientSession(sessionB, 20.0); });
            auto clientC = async(launch::async, [&]() { return runClientSession(sessionC, 30.0); });

            Assert::IsTrue(clientA.get());
            Assert::IsTrue(clientB.get());
            Assert::IsTrue(clientC.get());

            serverThreadA.join();
            serverThreadB.join();
            serverThreadC.join();
            sessionA.Close();
            sessionB.Close();
            sessionC.Close();
        }

        TEST_METHOD(ST_003_StateMachineFullCycle)
        {
        }

        TEST_METHOD(ST_004_TCPIP_Communication)
        {
            MockSession session = MockSession::CreateConnected(GROUND_CONTROL_PORT);

            Assert::AreEqual(static_cast<int>(AF_INET), static_cast<int>(session.server.boundAddress.sin_family));
            Assert::AreEqual(static_cast<int>(GROUND_CONTROL_PORT), static_cast<int>(ntohs(session.server.boundAddress.sin_port)));
            Assert::AreNotEqual(static_cast<SOCKET>(INVALID_SOCKET), session.client.socketHandle);
            Assert::AreNotEqual(static_cast<SOCKET>(INVALID_SOCKET), session.server.connectionSocket);

            session.Close();
        }

        TEST_METHOD(ST_005_PacketStructureValidation)
        {
            Packet packet = BuildFlightActivePacket(1.25, 2.5, 3.75);

            int totalSize = 0;
            char* serialized = packet.SerializeData(totalSize);

            Assert::IsTrue(serialized != nullptr);
            Assert::AreEqual(EmptyPktSize + static_cast<int>(sizeof(double) * 3), totalSize);
            Assert::AreEqual(static_cast<int>(FLIGHT_ACTIVE), static_cast<int>(static_cast<unsigned char>(serialized[2])));
            Assert::AreEqual(static_cast<int>(sizeof(double) * 3), static_cast<int>(static_cast<unsigned char>(serialized[3])));
        }

        TEST_METHOD(ST_006_FullSessionLogCompleteness)
        {
            remove("log.txt");

            MockSession session = MockSession::CreateConnected();
            thread serverThread(handleClient, session.server.connectionSocket, 13);
            session.server.connectionSocket = INVALID_SOCKET;

            Packet active = BuildFlightActivePacket(9.0, 8.0, 7.0);
            Assert::IsTrue(session.client.SendPacket(active));

            Packet response;
            Assert::IsTrue(session.client.ReceivePacket(response));

            Packet done = BuildInstructionOnlyPacket(FLIGHT_DONE);
            Assert::IsTrue(session.client.SendPacket(done));
            Assert::IsTrue(session.client.ReceivePacket(response));

            serverThread.join();
            session.Close();

            string logContents = ReadTextFile("log.txt");
            Assert::IsTrue(logContents.find("Connection Established") != string::npos);
            Assert::IsTrue(logContents.find("PACKET|DIR=RECEIVE|DATA=w | 9.000000, 8.000000, 7.000000") != string::npos);
            Assert::IsTrue(logContents.find("PACKET|DIR=SEND|DATA=D") != string::npos);
            Assert::IsTrue(logContents.find("FLIGHT_DONE received.") != string::npos);
        }

        TEST_METHOD(ST_007_IdleState_NoConnections)
        {
            MockServer server;
            server.StartListening();
            Assert::AreNotEqual(static_cast<SOCKET>(INVALID_SOCKET), server.listenerSocket);
            this_thread::sleep_for(chrono::milliseconds(100));
            server.Close();
        }

        TEST_METHOD(ST_008_NetworkDropRecovery)
        {
            MockSession session = MockSession::CreateConnected();
            session.server.Close();

            Packet packet;
            Assert::IsFalse(session.client.ReceivePacket(packet));

            session.client.Close();
        }
    };
}

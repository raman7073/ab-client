#include <iostream>
#include <fstream>
#include <vector>
#include <string>
#include <cstring>
#include <map>
#include <json/json.h> // Requires JSON library
#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#pragma comment(lib, "ws2_32.lib")
#else
#include <arpa/inet.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <unistd.h>
#endif

#define SERVER_IP "127.0.0.1" // Change if needed
#define SERVER_PORT 3000
#define BUFFER_SIZE 1024

struct Packet
{
    char symbol[4];         // Ticker symbol (not null-terminated)
    char buySellIndicator;  // 'B' or 'S'
    int32_t quantity;       // Quantity (big-endian)
    int32_t price;          // Price (big-endian)
    int32_t sequenceNumber; // Sequence number (big-endian)
};

// Convert big-endian to host-endian
int32_t convertBigEndian(int32_t value)
{
    return ntohl(value);
}

// Receive a full packet
bool receiveCompletePacket(int clientSocket, Packet &packet)
{
    char *buffer = reinterpret_cast<char *>(&packet);
    size_t totalReceived = 0;
    size_t packetSize = sizeof(Packet);

    while (totalReceived < packetSize)
    {
        int bytesReceived = recv(clientSocket, buffer + totalReceived, packetSize - totalReceived, 0);
        if (bytesReceived <= 0)
        {
            std::cerr << "Error receiving packet or connection closed.\n";
            return false;
        }
        totalReceived += bytesReceived;
    }
    return true;
}

// Request missing packets
void requestMissingPackets(std::map<int, Packet> &receivedPackets, int lastSequence, int clientSocket)
{
    for (int i = 1; i <= lastSequence; i++)
    {
        if (receivedPackets.find(i) == receivedPackets.end()) // If missing
        {
            std::cout << "Requesting missing packet for sequence: " << i << std::endl;

            uint8_t requestPacket[5] = {2}; // CallType = 2
            int32_t seq = htonl(i);
            memcpy(&requestPacket[1], &seq, sizeof(int32_t));

            if (send(clientSocket, reinterpret_cast<const char *>(requestPacket), sizeof(requestPacket), 0) == -1)
            {
                std::cerr << "Failed to request missing packet for sequence " << i << std::endl;
                continue;
            }

            // Receive missing packet
            Packet packet;
            if (receiveCompletePacket(clientSocket, packet))
            {
                packet.quantity = convertBigEndian(packet.quantity);
                packet.price = convertBigEndian(packet.price);
                packet.sequenceNumber = convertBigEndian(packet.sequenceNumber);

                receivedPackets[packet.sequenceNumber] = packet;
            }
        }
    }
}

// Receive, parse packets and save to JSON
void receivePackets(int clientSocket)
{
    std::map<int, Packet> receivedPackets;
    int lastSequence = 0;

    while (true)
    {
        Packet packet;
        if (!receiveCompletePacket(clientSocket, packet))
            break; // Exit on error or connection close

  
        packet.quantity = convertBigEndian(packet.quantity);
        packet.price = convertBigEndian(packet.price);
        packet.sequenceNumber = convertBigEndian(packet.sequenceNumber);

        receivedPackets[packet.sequenceNumber] = packet;
        lastSequence = std::max(lastSequence, packet.sequenceNumber);
    }


    requestMissingPackets(receivedPackets, lastSequence, clientSocket);

    
    Json::Value jsonArray;
    for (const auto &entry : receivedPackets)
    {
        const Packet &packet = entry.second;
        Json::Value jsonPacket;

       
        std::string symbolStr(packet.symbol, 4);
        symbolStr.erase(symbolStr.find('\0')); // Remove extra nulls

        jsonPacket["symbol"] = symbolStr;
        jsonPacket["buySellIndicator"] = std::string(1, packet.buySellIndicator);
        jsonPacket["quantity"] = packet.quantity;
        jsonPacket["price"] = packet.price;
        jsonPacket["sequenceNumber"] = packet.sequenceNumber;
        jsonArray.append(jsonPacket);
    }

    
    std::ofstream outFile("output.json");
    if (outFile)
    {
        outFile << jsonArray.toStyledString();
        outFile.close();
        std::cout << "Data successfully saved to output.json\n";
    }
    else
    {
        std::cerr << "Failed to write to output.json\n";
    }
}


int main()
{
#ifdef _WIN32
    WSADATA wsaData;
    if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0)
    {
        std::cerr << "WSAStartup failed!\n";
        return 1;
    }
#endif

   
    int clientSocket = socket(AF_INET, SOCK_STREAM, 0);
    if (clientSocket == -1)
    {
        std::cerr << "Socket creation failed!\n";
        return 1;
    }

   
    sockaddr_in serverAddr{};
    serverAddr.sin_family = AF_INET;
    serverAddr.sin_port = htons(SERVER_PORT);
    inet_pton(AF_INET, SERVER_IP, &serverAddr.sin_addr);

   
    if (connect(clientSocket, (sockaddr *)&serverAddr, sizeof(serverAddr)) == -1)
    {
        std::cerr << "Connection to server failed!\n";
#ifdef _WIN32
        closesocket(clientSocket);
        WSACleanup();
#else
        close(clientSocket);
#endif
        return 1;
    }

    std::cout << "Connected to ABX Exchange Server!\n";

   
    uint8_t requestPacket[2] = {1, 0}; 
    if (send(clientSocket, reinterpret_cast<const char *>(requestPacket), sizeof(requestPacket), 0) == -1)
    {
        std::cerr << "Failed to send request packet!\n";
#ifdef _WIN32
        closesocket(clientSocket);
        WSACleanup();
#else
        close(clientSocket);
#endif
        return 1;
    }

  
    receivePackets(clientSocket);

#ifdef _WIN32
    closesocket(clientSocket);
    WSACleanup();
#else
    close(clientSocket);
#endif

    return 0;
}

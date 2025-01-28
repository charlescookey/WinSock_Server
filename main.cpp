#include <winsock2.h>
#include <ws2tcpip.h>
#include <iostream>
#include <string>
#include <sstream>
#include <vector>
#include <queue>
#include <thread>
#include <mutex>
#include <unordered_map>
#include <utility>

const char delimiter = '\x1F';

#pragma comment(lib, "Ws2_32.lib")



std::queue<std::pair<std::string, SOCKET>> messageQueue;
std::mutex queueMutex;
std::vector<SOCKET> sockets{};

void chat(SOCKET client_socket) {
    std::cout << "New Thread created\n";
    while (true) {
        // Step 6: Communicate with the client
        char buffer[1024] = { 0 };
        int bytes_received = recv(client_socket, buffer, sizeof(buffer) - 1, 0);
        if (bytes_received > 0) {
            buffer[bytes_received] = '\0';
            std::cout << "Received: " << buffer << " ***from "<<client_socket<<std::endl;
            {
                std::lock_guard<std::mutex> lock(queueMutex);
                messageQueue.push({ std::string(buffer), client_socket });
            }
        }
    }
    // Step 7: Clean up
    closesocket(client_socket);
}

void parsePrivateMessage(std::string message, SOCKET sender, std::unordered_map<std::string, SOCKET>& socketIDs) {
    std::vector<std::string> parts;
    std::stringstream ss(message);
    std::string temp;
    while (std::getline(ss, temp, delimiter)) {
        if (temp.empty())continue;
        parts.push_back(temp);
    }

    SOCKET reciever = socketIDs[parts[0]];
    message = std::string("3") + delimiter + parts[1] + delimiter + parts[2];
    send(reciever, message.c_str(), static_cast<int>(message.size()), 0);

    std::cout << "Sent \"" << message << "\" to client: " << reciever << "\n";
    
    
    message = std::string("3") + delimiter + parts[0] + delimiter + parts[2];
    send(sender, message.c_str(), static_cast<int>(message.size()), 0);

    std::cout << "Sent \"" << message << "\" to client: " << sender << "\n";
}

void oldparsePrivateMessage(std::string message, std::unordered_map<std::string, SOCKET>& socketIDs) {
    std::string reciever;

    size_t start = 0;
    size_t end = message.find(delimiter);

    reciever = message.substr(start, end - start);

    start = end + 1;
    message = message.substr(start);
    message = std::string("3") + delimiter + message;

    send(socketIDs[reciever], message.c_str(), static_cast<int>(message.size()), 0);
    std::cout << "Sent \"" << message << "\" to client: " << socketIDs[reciever] << "\n";
}
void sendtoAllSockets(std::string message, std::vector<SOCKET> sockets) {
    for (SOCKET s : sockets) {
        send(s, message.c_str(), static_cast<int>(message.size()), 0);
        std::cout << "Sent \"" << message << "\" to client: " << s << "\n";
    }
}

void sendGroupMessage(std::string message, std::vector<SOCKET> sockets) {
    message = std::string("2")+delimiter + message;
    sendtoAllSockets(message, sockets);
}

void sendUserList(std::string message, SOCKET sender, std::string& Userlist, std::vector<SOCKET> sockets, std::unordered_map<std::string, SOCKET>& socketIDs) {
    socketIDs[message] = sender;
    Userlist += delimiter + message;
    sendtoAllSockets(Userlist, sockets);
}

void parseServerMessage(std::string message, SOCKET sender, std::string& Userlist, std::unordered_map<std::string, SOCKET>& socketIDs, std::vector<SOCKET> sockets) {
    std::string temp;

    size_t start = 0;
    size_t end = message.find(delimiter);

    temp = message.substr(start, end - start);
    int type = std::stoi(temp);

    start = end + 1;
    temp = message.substr(start);

    switch (type) {
    case 1:
        sendUserList(temp,sender, Userlist, sockets, socketIDs);
        break;
    case 2:
        sendGroupMessage(temp, sockets);
        break;
    case 3:
        parsePrivateMessage(temp, sender, socketIDs);
        break;
    default:
        break;
    }
}

void serverThread() {
    std::string Userlist{ "1" };
    std::unordered_map<std::string, SOCKET> socketIDs{};
    
    while (true) {
        // Process messages
        while (!messageQueue.empty()) {
            std::pair<std::string, SOCKET> temp;
            temp = messageQueue.front();
            messageQueue.pop();
            std::cout << "Message received: " << temp.first << std::endl;
            parseServerMessage(temp.first,temp.second, Userlist, socketIDs, sockets);
        }
    }
}

int server_loop() {
    // Step 1: Initialize WinSock
    WSADATA wsaData;
    if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0) {
        std::cerr << "WSAStartup failed with error: " << WSAGetLastError() << std::endl;
        return 1;
    }

    // Step 2: Create a socket
    SOCKET server_socket = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (server_socket == INVALID_SOCKET) {
        std::cerr << "Socket creation failed with error: " << WSAGetLastError() << std::endl;
        WSACleanup();
        return 1;
    }

    // Step 3: Bind the socket
    sockaddr_in server_address = {};
    server_address.sin_family = AF_INET;
    server_address.sin_port = htons(65432);  // Server port
    server_address.sin_addr.s_addr = INADDR_ANY; // Accept connections on any IP address

    if (bind(server_socket, (sockaddr*)&server_address, sizeof(server_address)) == SOCKET_ERROR) {
        std::cerr << "Bind failed with error: " << WSAGetLastError() << std::endl;
        closesocket(server_socket);
        WSACleanup();
        return 1;
    }

    // Step 4: Listen for incoming connections
    if (listen(server_socket, SOMAXCONN) == SOCKET_ERROR) {
        std::cerr << "Listen failed with error: " << WSAGetLastError() << std::endl;
        closesocket(server_socket);
        WSACleanup();
        return 1;
    }

    std::cout << "Server is listening on port 65432..." << std::endl;

    std::thread* receivingThread = new std::thread(serverThread);


    while (true) {
        // Step 5: Accept a connection
        sockaddr_in client_address = {};
        int client_address_len = sizeof(client_address);
        SOCKET client_socket = accept(server_socket, (sockaddr*)&client_address, &client_address_len);
        if (client_socket == INVALID_SOCKET) {
            std::cerr << "Accept failed with error: " << WSAGetLastError() << std::endl;
            closesocket(server_socket);
            WSACleanup();
            return 1;
        }
        char client_ip[INET_ADDRSTRLEN];
        inet_ntop(AF_INET, &client_address.sin_addr, client_ip, INET_ADDRSTRLEN);
        std::cout << "Accepted connection from " << client_ip << ":" << ntohs(client_address.sin_port) << std::endl;
        sockets.push_back(client_socket);
        std::thread* thread = new std::thread(chat, client_socket);
    }
    
    closesocket(server_socket);
    WSACleanup();

    return 0;
}

int main() {
    server_loop();
}
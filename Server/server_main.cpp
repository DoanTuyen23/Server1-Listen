#include <iostream>
#include <winsock2.h>
#include <windows.h> 
#include <vector>
#include <string>
#include "../Common/protocol.h"

#pragma comment(lib, "ws2_32.lib") 

using namespace std;

// Thay mutex bằng CRITICAL_SECTION của Windows
CRITICAL_SECTION client_cs; 
vector<SOCKET> clients;

void broadcast_message(Message msg, SOCKET sender_socket) {
    // Thay lock_guard bằng Enter/Leave CriticalSection
    EnterCriticalSection(&client_cs);
    for (int i = 0; i < clients.size(); i++) {
        if (clients[i] != sender_socket) {
            send(clients[i], (char*)&msg, sizeof(Message), 0);
        }
    }
    LeaveCriticalSection(&client_cs);
}

// Hàm thread trong Windows phải trả về DWORD WINAPI và nhận LPVOID
DWORD WINAPI handle_client(LPVOID param) {
    SOCKET client_socket = (SOCKET)param;
    Message msg;
    
    while (true) {
        int bytes_received = recv(client_socket, (char*)&msg, sizeof(Message), 0);
        if (bytes_received <= 0) {
            cout << "Client ngat ket noi." << endl;
            break;
        }

        if (msg.type == MSG_LOGIN) {
            cout << "[LOGIN] " << msg.name << " da tham gia." << endl;
            string notif = string(msg.name) + " da tham gia!";
            strcpy(msg.data, notif.c_str());
            msg.type = MSG_CHAT;
            broadcast_message(msg, client_socket);
        }
        else if (msg.type == MSG_CHAT) {
            cout << "[CHAT] " << msg.name << ": " << msg.data << endl;
            broadcast_message(msg, client_socket);
        }
    }

    // Xóa client khỏi danh sách
    EnterCriticalSection(&client_cs);
    for (int i = 0; i < clients.size(); i++) {
        if (clients[i] == client_socket) {
            clients.erase(clients.begin() + i);
            break;
        }
    }
    LeaveCriticalSection(&client_cs);
    
    closesocket(client_socket);
    return 0;
}

int main() {
    // Khởi tạo biến khóa (Mutex)
    InitializeCriticalSection(&client_cs);

    WSADATA wsaData;
    if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0) return 1;

    SOCKET server_socket = socket(AF_INET, SOCK_STREAM, 0);
    sockaddr_in server_addr;
    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = INADDR_ANY;
    server_addr.sin_port = htons(SERVER_PORT);

    if (bind(server_socket, (sockaddr*)&server_addr, sizeof(server_addr)) < 0) {
        cerr << "Loi Bind port! Co the port dang duoc su dung." << endl;
        return 1;
    }
    listen(server_socket, 5);

    cout << "=== SERVER DANG CHAY TAI PORT " << SERVER_PORT << " ===" << endl;

    while (true) {
        SOCKET client_socket = accept(server_socket, NULL, NULL);
        if (client_socket == INVALID_SOCKET) continue;

        EnterCriticalSection(&client_cs);
        clients.push_back(client_socket);
        LeaveCriticalSection(&client_cs);

        // Thay std::thread bằng CreateThread của Windows
        CreateThread(NULL, 0, handle_client, (LPVOID)client_socket, 0, NULL);
    }

    DeleteCriticalSection(&client_cs);
    closesocket(server_socket);
    WSACleanup();
    return 0;
}
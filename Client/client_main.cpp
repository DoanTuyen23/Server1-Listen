#include <iostream>
#include <winsock2.h>
#include <windows.h>
#include <string>
#include "../Common/protocol.h"

#pragma comment(lib, "ws2_32.lib")

using namespace std;

SOCKET client_socket;
bool is_running = true;

// Hàm thread nhận tin nhắn
DWORD WINAPI receive_thread(LPVOID param) {
    Message msg;
    while (is_running) {
        int bytes = recv(client_socket, (char*)&msg, sizeof(Message), 0);
        if (bytes <= 0) {
            cout << "\nMat ket noi toi Server!" << endl;
            is_running = false;
            break;
        }

        if (msg.type == MSG_CHAT) {
            // Dùng \r để xóa dòng hiện tại, in tin nhắn đè lên
            cout << "\r" << string(50, ' ') << "\r"; 
            cout << msg.name << ": " << msg.data << endl;
            cout << "You: " << flush; 
        }
    }
    return 0;
}

int main() {
    WSADATA wsaData;
    WSAStartup(MAKEWORD(2, 2), &wsaData);

    client_socket = socket(AF_INET, SOCK_STREAM, 0);
    sockaddr_in server_addr;
    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = inet_addr(SERVER_IP);
    server_addr.sin_port = htons(SERVER_PORT);

    cout << "Dang ket noi toi Server..." << endl;
    if (connect(client_socket, (sockaddr*)&server_addr, sizeof(server_addr)) < 0) {
        cerr << "Ket noi that bai! Hay chac chan Server da bat." << endl;
        return 1;
    }
    cout << "Ket noi thanh cong!" << endl;

    Message msg;
    msg.type = MSG_LOGIN;
    cout << "Nhap ten cua ban: ";
    cin.getline(msg.name, NAME_LEN);
    send(client_socket, (char*)&msg, sizeof(Message), 0);

    // Tạo thread nhận tin bằng CreateThread
    CreateThread(NULL, 0, receive_thread, NULL, 0, NULL);

    while (is_running) {
        string input;
        cout << "You: ";
        getline(cin, input);

        if (input == "exit") break;
        if (!is_running) break;

        msg.type = MSG_CHAT;
        strcpy(msg.data, input.c_str());
        send(client_socket, (char*)&msg, sizeof(Message), 0);
    }

    is_running = false;
    closesocket(client_socket);
    WSACleanup();
    return 0;
}
#ifndef PROTOCOL_H
#define PROTOCOL_H

#include <cstdint>

// Cấu hình mạng
#define SERVER_PORT 8888
#define SERVER_IP "127.0.0.1" // Localhost
#define BUFF_SIZE 1024
#define NAME_LEN 32

enum MessageType {
    MSG_LOGIN,
    MSG_CHAT,
    MSG_DISCONNECT
};

struct Message {
    int type;                   // Loại tin nhắn
    char name[NAME_LEN];        // Tên người gửi
    char data[BUFF_SIZE];       // Nội dung chat
};

#endif
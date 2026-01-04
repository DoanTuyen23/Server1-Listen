#ifndef PROTOCOL_H
#define PROTOCOL_H

#define SERVER_PORT 8888
#define SERVER_IP "127.0.0.1"
#define NAME_LEN 32
#define PASS_LEN 32
#define BUFF_SIZE 1024

// Tự động đánh số từ 0 -> 17, khớp với Client Python
enum MessageType {
    MSG_LOGIN_REQ = 0,      // 0
    MSG_LOGIN_SUCCESS,      // 1
    MSG_LOGIN_FAIL,         // 2
    
    MSG_PRIVATE_CHAT,       // 3
    MSG_GROUP_CHAT,         // 4
    
    // --- KẾT BẠN & DANH SÁCH ---
    MSG_FRIEND_REQ,         // 5
    MSG_FRIEND_ACCEPT,      // 6
    MSG_ADD_FRIEND_SUCC,    // 7
    
    MSG_CREATE_GROUP_REQ,   // 8
    MSG_JOIN_GROUP_REQ,     // 9
    MSG_ADD_GROUP_SUCC,     // 10
    
    MSG_HISTORY,            // 11

    // --- QUẢN LÝ NHÓM ---
    MSG_CREATE_GROUP_FAIL,  // 12
    MSG_REQ_MEMBER_LIST,    // 13
    MSG_RESP_MEMBER_LIST,   // 14
    
    // --- RỜI NHÓM, HỦY KB ---
    MSG_LEAVE_GROUP,        // 15
    MSG_UNFRIEND,           // 16
    MSG_REMOVE_CONTACT,  // 17
    // --- TÍNH NĂNG GỬI FILE ---
    MSG_FILE_START,     // Báo server: "Tôi bắt đầu gửi file này, kích thước này" 18
    MSG_FILE_DATA,     // Chứa dữ liệu file (Binary chunk) 19
    MSG_FILE_END,     // Báo server: "Đã gửi xong" 20
    MSG_FILE_NOTIFY,    // Thông báo có file mới gửi đến 21
    // --- TÍNH NĂNG TẢI FILE (DOWNLOAD) ---
    MSG_FILE_DOWNLOAD_REQ, // Yêu cầu tải file từ server 22

    // --- TÍNH NĂNG CHƠI GAME CARO ---
    MSG_GAME_REQ, // Gửi lời mời chơi 23
    MSG_GAME_ACCEPT, // Đồng ý lời mời 24
    MSG_GAME_MOVE,  // Gửi nước đi 25
    MSG_GAME_END,  // Kết thúc game 26
    MSG_REQ_PENDING_LIST,  // Yêu cầu danh sách lời mời kết bạn đang chờ 27
    MSG_RESP_PENDING_LIST // Trả lời danh sách lời mời kết bạn đang chờ 28
    
};


#pragma pack(push, 1)

struct Message {
    int type;                  // 4 bytes
    char name[NAME_LEN];       // 32 bytes
    char password[PASS_LEN];   // 32 bytes
    char target[NAME_LEN];     // 32 bytes
    char group_pass[PASS_LEN]; // 32 bytes
    char data[BUFF_SIZE];      // 1024 bytes
};                             // TỔNG: 1156 bytes

#pragma pack(pop)

#endif
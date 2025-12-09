#include <iostream>
#include <winsock2.h>
#include <windows.h>
#include <vector>
#include <string>
#include <map>
#include <algorithm>
#include <fstream>
#include <sstream>
#include "../Common/protocol.h"
#include "storage.h"
#include <ctime>

#pragma comment(lib, "ws2_32.lib") 

using namespace std;

struct GroupInfo {
    string password;
    vector<SOCKET> members;
};

CRITICAL_SECTION data_cs; 
map<string, SOCKET> online_users;     
map<string, GroupInfo> groups;        

string trim(const string& str) {
    size_t first = str.find_first_not_of(" \t\r\n");
    if (string::npos == first) return "";
    size_t last = str.find_last_not_of(" \t\r\n");
    return str.substr(first, (last - first + 1));
}

// Hàm load dữ liệu từ file lên RAM
void init_server_data() {
    map<string, string> temp_groups;
    load_groups_to_memory(temp_groups);
    
    // SỬA: Dùng vòng lặp kiểu cũ để tránh lỗi biên dịch
    for (map<string, string>::iterator it = temp_groups.begin(); it != temp_groups.end(); ++it) {
        string name = it->first;
        string pass = it->second;
        GroupInfo g;
        g.password = pass;
        groups[name] = g;
    }
    cout << "[SYSTEM] Da load " << groups.size() << " nhom tu database." << endl;
}

// Hàm kiểm tra xem 1 chuỗi có nằm trong vector không
bool is_exist_in_vector(const string& val, const vector<string>& list) {
    for (size_t i = 0; i < list.size(); ++i) {
        if (list[i] == val) return true;
    }
    return false;
}

// Hàm gửi lại toàn bộ dữ liệu cũ cho Client (FIX LỖI LỘ TIN NHẮN)
void sync_client_data(SOCKET client, string username) {
    Message msg;
    
    // 1. Gửi danh sách bạn bè
    vector<string> friends = get_friend_list(username);
    msg.type = MSG_ADD_FRIEND_SUCC; 
    for (size_t i = 0; i < friends.size(); ++i) {
        strcpy(msg.target, friends[i].c_str());
        send(client, (char*)&msg, sizeof(Message), 0);
        Sleep(10); 
    }
    
    // 2. Gửi danh sách nhóm
    vector<string> my_groups = get_user_groups(username);
    msg.type = MSG_ADD_GROUP_SUCC; 
    for (size_t i = 0; i < my_groups.size(); ++i) {
        strcpy(msg.target, my_groups[i].c_str()); 
        send(client, (char*)&msg, sizeof(Message), 0);
        Sleep(10);
    }
    
    // 3. Gửi lịch sử chat cũ
    vector<string> history = get_user_history(username); // Hàm này thực ra load TOÀN BỘ file history
    msg.type = MSG_HISTORY;
    
    for (size_t i = 0; i < history.size(); ++i) {
        stringstream ss(history[i]);
        string segment;
        vector<string> seglist;
        while(getline(ss, segment, '|')) seglist.push_back(segment);
        
        // Format chuẩn: TYPE|SENDER|TARGET|CONTENT
        if (seglist.size() >= 4) {
            int type = stoi(seglist[0]);
            string sender = seglist[1];
            string target = seglist[2];
            string content = seglist[3];

            bool send_it = false;
            
            // ======================================================
            // 🔴 SỬA LỖI TẠI ĐÂY: CHECK QUYỀN RIÊNG TƯ
            // ======================================================

            if (type == MSG_PRIVATE_CHAT) {
                // QUY TẮC: User hiện tại (username) PHẢI là sender HOẶC target
                // Nếu tin nhắn là "q" gửi "tuyen", mà tôi là "qq" -> SAI -> Bỏ qua ngay
                bool is_involved = (username == sender || username == target);
                
                if (is_involved) {
                    // Nếu đã dính líu đến mình, kiểm tra thêm xem đối phương có còn là bạn không
                    // (Optional: Tuỳ bạn muốn hiện tin nhắn của người đã hủy kết bạn hay không)
                    string partner = (sender == username) ? target : sender;
                    if (is_exist_in_vector(partner, friends)) {
                        send_it = true;
                    }
                }
            } 
            else if (type == MSG_GROUP_CHAT) {
                // Với nhóm: target là Tên Nhóm.
                // Chỉ gửi nếu mình đang ở trong nhóm đó
                if (is_exist_in_vector(target, my_groups)) {
                    send_it = true;
                }
            }
            
            if (send_it) {
                memset(msg.password, 0, 32);
                sprintf(msg.password, "%d", type);
                strcpy(msg.name, sender.c_str());
                strcpy(msg.target, target.c_str());
                strcpy(msg.data, content.c_str());
                send(client, (char*)&msg, sizeof(Message), 0);
                Sleep(5);
            }
        }
    }
}

// // Hàm trả về chuỗi danh sách thành viên: "UserA, UserB, UserC"
// string get_group_members_str(string group_name) {
//     string filename = "Server/Data/" + group_name + "_members.txt";
//     ifstream f(filename.c_str());
//     string line, result = "";
//     while (getline(f, line)) {
//         if (!line.empty()) result += line + ", ";
//     }
//     f.close();
//     // Xóa dấu phẩy cuối
//     if (result.length() > 2) result = result.substr(0, result.length() - 2);
//     return result;
// }

// 2. Hàm xóa dòng trong file (An toàn tuyệt đối)
void remove_line_from_file(string filename, string text_to_remove) {
    string temp_file = filename + ".tmp";
    ifstream in(filename.c_str());
    ofstream out(temp_file.c_str());
    string line;
    
    if (!in.is_open()) {
        cout << "[ERROR] Khong mo duoc file: " << filename << endl;
        return;
    }

    // Chuẩn hóa text cần xóa (VD: "NhomA|UserB")
    string target = trim(text_to_remove);

    while (getline(in, line)) {
        string clean_line = trim(line);
        
        // Nếu dòng đọc được KHÁC target thì giữ lại.
        // Nếu dòng đọc được == target thì bỏ qua (tức là xóa).
        if (!clean_line.empty() && clean_line != target) {
            out << line << endl;
        }
    }
    in.close();
    out.close();
    
    remove(filename.c_str());
    rename(temp_file.c_str(), filename.c_str());
}

DWORD WINAPI handle_client(LPVOID param) {
    SOCKET client_socket = (SOCKET)param;
    Message msg;
    bool is_logged_in = false;
    string my_name = "";

    // BIẾN ĐỂ XỬ LÝ NHẬN FILE
    ofstream *current_file = NULL;
    string current_filename = "";
    
    while (true) {

        memset(&msg, 0, sizeof(Message));

        int bytes = recv(client_socket, (char*)&msg, sizeof(Message), 0);
        if (bytes <= 0) break;

        // 1. ĐĂNG NHẬP
        if (msg.type == MSG_LOGIN_REQ) {
            bool ok = check_login(msg.name, msg.password);
            Message response;
            if (ok) {
                response.type = MSG_LOGIN_SUCCESS;
                send(client_socket, (char*)&response, sizeof(Message), 0);
                
                is_logged_in = true;
                my_name = string(msg.name);
                EnterCriticalSection(&data_cs);
                online_users[my_name] = client_socket;
                LeaveCriticalSection(&data_cs); 
                
                cout << "[ONLINE] " << my_name << endl;
                
                // =================================================================
                // 🔴 BẮT ĐẦU ĐOẠN CODE SỬA LỖI: TỰ ĐỘNG ADD SOCKET VÀO CÁC NHÓM CŨ
                // =================================================================
                
                // 1. Lấy danh sách các nhóm mà User này từng tham gia từ File
                vector<string> my_groups_list = get_user_groups(my_name);

                EnterCriticalSection(&data_cs); // Nhớ khóa data lại vì thao tác biến toàn cục groups
                for (size_t i = 0; i < my_groups_list.size(); ++i) {
                    string gname = my_groups_list[i];
                    
                    // Nếu nhóm này có tồn tại trên RAM (Server đã load lên rồi)
                    if (groups.find(gname) != groups.end()) {
                        // Thêm socket hiện tại của user vào danh sách nhận tin của nhóm
                        groups[gname].members.push_back(client_socket);
                        cout << "[RE-JOIN] User " << my_name << " da duoc add lai vao RAM cua nhom: " << gname << endl;
                    }
                }
                LeaveCriticalSection(&data_cs);
                
                // =================================================================
                // 🔴 KẾT THÚC ĐOẠN CODE SỬA LỖI
                // =================================================================

                // Gửi lại dữ liệu cũ để hiện lên Sidebar
                sync_client_data(client_socket, my_name);
                
            } else {
                response.type = MSG_LOGIN_FAIL;
                send(client_socket, (char*)&response, sizeof(Message), 0);
            }
        }
        
        // 2. KẾT BẠN (Gửi yêu cầu)
        else if (msg.type == MSG_FRIEND_REQ) {
            EnterCriticalSection(&data_cs);
            string target(msg.target);
            // Nếu người kia online -> Gửi gói tin sang cho họ
            if (online_users.find(target) != online_users.end()) {
                send(online_users[target], (char*)&msg, sizeof(Message), 0);
                cout << "[FORWARD] Loi moi ket ban tu " << my_name << " -> " << target << endl;
            }
            LeaveCriticalSection(&data_cs);
        }
        
        // 3. CHẤP NHẬN KẾT BẠN
        else if (msg.type == MSG_FRIEND_ACCEPT) {
            // Lưu vào file
            add_friend_db(msg.name, msg.target);
            
            Message update;
            update.type = MSG_ADD_FRIEND_SUCC;
            
            // Báo cho MÌNH (người bấm đồng ý) -> Hiện bạn lên Sidebar
            strcpy(update.target, msg.target); 
            send(client_socket, (char*)&update, sizeof(Message), 0);
            
            // Báo cho NGƯỜI KIA (người gửi lời mời) -> Hiện mình lên Sidebar của họ
            EnterCriticalSection(&data_cs);
            if (online_users.find(msg.target) != online_users.end()) {
                strcpy(update.target, msg.name);
                send(online_users[msg.target], (char*)&update, sizeof(Message), 0);
            }
            LeaveCriticalSection(&data_cs);
        }
        
        // 4. TẠO NHÓM
        else if (msg.type == MSG_CREATE_GROUP_REQ) {
            string gname(msg.target);
            bool exists = false;

            EnterCriticalSection(&data_cs);
            if (groups.find(gname) != groups.end()) exists = true;
            LeaveCriticalSection(&data_cs);

            if (exists) {
                // Nếu trùng tên -> Báo lỗi
                Message resp;
                resp.type = MSG_CREATE_GROUP_FAIL;
                strcpy(resp.data, "Ten nhom da ton tai!");
                send(client_socket, (char*)&resp, sizeof(Message), 0);
            } else {
                // Nếu chưa có -> Tạo bình thường
                create_group_db(gname, msg.group_pass);
                add_group_member_db(gname, my_name);
                
                EnterCriticalSection(&data_cs);
                GroupInfo g;
                g.password = string(msg.group_pass);
                g.members.push_back(client_socket);
                groups[gname] = g;
                LeaveCriticalSection(&data_cs);

                Message resp; 
                resp.type = MSG_ADD_GROUP_SUCC;
                strcpy(resp.target, gname.c_str());
                send(client_socket, (char*)&resp, sizeof(Message), 0);
            }
        }

        // 5. VÀO NHÓM (THÊM MỚI: Logic join group)
        else if (msg.type == MSG_JOIN_GROUP_REQ) {
            string gname(msg.target);
            string pass(msg.group_pass);
            bool ok = false;

            EnterCriticalSection(&data_cs);
            // Kiểm tra nhóm có tồn tại và đúng pass không
            if (groups.find(gname) != groups.end()) {
                if (groups[gname].password == pass) {
                    ok = true;
                    // Thêm socket vào danh sách nhận tin ngay lập tức
                    groups[gname].members.push_back(client_socket);
                }
            }
            LeaveCriticalSection(&data_cs);

            if (ok) {
                // Lưu vào file database để lần sau login vẫn còn
                add_group_member_db(gname, my_name);

                // QUAN TRỌNG: Gửi gói tin này thì Sidebar bên Client mới hiện nhóm lên
                Message resp;
                resp.type = MSG_ADD_GROUP_SUCC;
                strcpy(resp.target, gname.c_str());
                send(client_socket, (char*)&resp, sizeof(Message), 0);
            } else {
                // Báo lỗi sai pass hoặc nhóm không tồn tại (Dùng tạm Login Fail để báo)
                Message resp;
                resp.type = MSG_LOGIN_FAIL; // Client sẽ hiện "Lỗi"
                send(client_socket, (char*)&resp, sizeof(Message), 0);
            }
        }

        // 6. YÊU CẦU XEM THÀNH VIÊN
        else if (msg.type == MSG_REQ_MEMBER_LIST) {
            // Gọi hàm mới trong storage.cpp
            vector<string> mems = get_group_members(msg.target);
            
            string mem_str = "";
            for (size_t i = 0; i < mems.size(); ++i) {
                mem_str += mems[i] + ", ";
            }
            // Xóa dấu phẩy thừa cuối cùng
            if (mem_str.length() > 2) mem_str = mem_str.substr(0, mem_str.length() - 2);
            if (mem_str.empty()) mem_str = "(Chua co thanh vien)";

            Message resp;
            resp.type = MSG_RESP_MEMBER_LIST;
            strcpy(resp.target, msg.target);
            strncpy(resp.data, mem_str.c_str(), 1024);
            send(client_socket, (char*)&resp, sizeof(Message), 0);
        }
        
        // 7. CHAT RIÊNG
        else if (msg.type == MSG_PRIVATE_CHAT) {
            // Lưu tin nhắn
            save_message(my_name, msg.target, msg.data, MSG_PRIVATE_CHAT);
            
            // Chuyển tiếp
            EnterCriticalSection(&data_cs);
            if (online_users.find(msg.target) != online_users.end()) {
                strcpy(msg.name, my_name.c_str());
                send(online_users[msg.target], (char*)&msg, sizeof(Message), 0);
            }
            LeaveCriticalSection(&data_cs);
        }
        
        // 8. CHAT NHÓM
        else if (msg.type == MSG_GROUP_CHAT) {
             save_message(my_name, msg.target, msg.data, MSG_GROUP_CHAT);
             strcpy(msg.name, my_name.c_str());
             
             EnterCriticalSection(&data_cs);
             if (groups.find(msg.target) != groups.end()) {
                 // SỬA: Dùng vòng lặp chỉ số để tránh lỗi biên dịch
                 vector<SOCKET>& mems = groups[msg.target].members;
                 for (size_t i = 0; i < mems.size(); ++i) {
                     if (mems[i] != client_socket) {
                         send(mems[i], (char*)&msg, sizeof(Message), 0);
                     }
                 }
             }
             LeaveCriticalSection(&data_cs);
        }


        // 9. RỜI NHÓM
        else if (msg.type == MSG_LEAVE_GROUP) {
            string gname(msg.target);
            
            // Format trong file storage: "TenNhom|TenUser"
            string line_to_remove = gname + "|" + my_name;
            
            // Xóa trong file tổng group_members.txt
            remove_line_from_file("Server/Data/group_members.txt", line_to_remove);

            // Xóa Socket trên RAM (để ngắt chat ngay)
            EnterCriticalSection(&data_cs);
            if (groups.find(gname) != groups.end()) {
                vector<SOCKET> &mems = groups[gname].members;
                for (size_t i = 0; i < mems.size(); ++i) {
                    if (mems[i] == client_socket) {
                        mems.erase(mems.begin() + i);
                        break;
                    }
                }
            }
            LeaveCriticalSection(&data_cs);

            // Báo client xóa nút
            Message resp;
            resp.type = MSG_REMOVE_CONTACT;
            strcpy(resp.target, gname.c_str());
            send(client_socket, (char*)&resp, sizeof(Message), 0);
            
            cout << "[LEAVE] " << my_name << " roi nhom " << gname << endl;
        }

        // 10. HỦY KẾT BẠN (Cập nhật logic 2 chiều)
        else if (msg.type == MSG_UNFRIEND) {
            string friend_name(msg.target);
            
            // 1. Xóa trong Database (Cả 2 chiều A|B và B|A)
            string line1 = my_name + "|" + friend_name;
            string line2 = friend_name + "|" + my_name;
            
            remove_line_from_file("Server/Data/friends.txt", line1);
            remove_line_from_file("Server/Data/friends.txt", line2);

            // 2. Gửi lệnh xóa cho MÌNH (Người bấm hủy)
            // -> Xóa nút tên người kia khỏi Sidebar của mình
            Message resp;
            resp.type = MSG_REMOVE_CONTACT;
            strcpy(resp.target, friend_name.c_str());
            send(client_socket, (char*)&resp, sizeof(Message), 0);
            
            // 3. Gửi lệnh xóa cho NGƯỜI KIA (Nếu họ đang Online)
            // -> Xóa nút tên mình khỏi Sidebar của họ
            EnterCriticalSection(&data_cs); // Bắt buộc dùng khóa vì truy cập online_users
            if (online_users.find(friend_name) != online_users.end()) {
                SOCKET friend_socket = online_users[friend_name];
                
                Message notify;
                notify.type = MSG_REMOVE_CONTACT;
                strcpy(notify.target, my_name.c_str()); // Bảo họ xóa tên MÌNH
                
                send(friend_socket, (char*)&notify, sizeof(Message), 0);
                cout << "[NOTIFY] Da bao cho " << friend_name << " xoa nut " << my_name << endl;
            }
            LeaveCriticalSection(&data_cs);
            
            cout << "[UNFRIEND] " << my_name << " - " << friend_name << endl;
        }

        // 11. BẮT ĐẦU NHẬN FILE
        if (msg.type == MSG_FILE_START) {
            string fname(msg.data); // Tên file nằm trong biến data
            string sender(msg.name);

            time_t now = time(0);
            
            // Tạo tên mới: "timestamp_tengoc" (VD: 173841234_anh.jpg)
            // Dùng to_string để chuyển số sang chuỗi
            string new_fname = to_string(now) + "_" + fname;
            
            string save_path = "Server/Data/Files/" + new_fname;
            
            current_file = new ofstream(save_path.c_str(), ios::binary);
            current_filename = new_fname;
            
            cout << "[FILE START] " << sender << " gui file: " << fname << " -> Luu thanh: " << new_fname << endl;
        }
        
        // 12. NHẬN DỮ LIỆU FILE (BINARY)
        else if (msg.type == MSG_FILE_DATA) {
            if (current_file && current_file->is_open()) {
                // Đọc độ dài chunk từ trường password
                int chunk_len = atoi(msg.password); 
                if (chunk_len > 0 && chunk_len <= 1024) {
                    current_file->write(msg.data, chunk_len);
                }
            }
        }
        
        // 13. KẾT THÚC FILE -> THÔNG BÁO CHO NGƯỜI NHẬN
        else if (msg.type == MSG_FILE_END) {
            if (current_file) {
                current_file->close();
                delete current_file;
                current_file = NULL;
            }
            
            string sender(msg.name);
            string target(msg.target); // Người nhận hoặc Nhóm
            
            cout << "[FILE END] Da luu file tu " << sender << endl;
            
            // --- THÔNG BÁO CHO NGƯỜI NHẬN ---
            // Tạo thông báo: "[FILE] sender da gui file: filename"
            Message notify;
            notify.type = MSG_FILE_NOTIFY;
            strcpy(notify.name, sender.c_str());
            strcpy(notify.target, target.c_str());
            strcpy(notify.data, current_filename.c_str()); // Tên file
            
            // Logic gửi thông báo (tương tự chat)
            // Nếu là Private
            EnterCriticalSection(&data_cs);
            if (online_users.find(target) != online_users.end()) {
                send(online_users[target], (char*)&notify, sizeof(Message), 0);
            }
            // Nếu là Group (cần duyệt danh sách mem)
            else if (groups.find(target) != groups.end()) {
                 vector<SOCKET>& mems = groups[target].members;
                 for (size_t i = 0; i < mems.size(); ++i) {
                     if (mems[i] != client_socket) {
                         send(mems[i], (char*)&notify, sizeof(Message), 0);
                     }
                 }
            }
            LeaveCriticalSection(&data_cs);
            
            // Lưu lịch sử chat dạng văn bản để load lại sau
            string history_content = "[FILE] " + current_filename;
            int type_save = (groups.find(target) != groups.end()) ? MSG_GROUP_CHAT : MSG_PRIVATE_CHAT;
            save_message(sender, target, history_content, type_save);
        }

        // 14. XỬ LÝ YÊU CẦU TẢI FILE TỪ CLIENT
        else if (msg.type == MSG_FILE_DOWNLOAD_REQ) {
            string filename(msg.data); // Tên file client muốn tải
            string filepath = "Server/Data/Files/" + filename;
            
            cout << "[DOWNLOAD] Client " << my_name << " yeu cau tai file: " << filename << endl;

            ifstream infile(filepath.c_str(), ios::binary | ios::ate); // Mở chế độ Binary + Đặt con trỏ ở cuối để lấy size
            
            if (infile.is_open()) {
                // 1. Lấy kích thước file
                int filesize = infile.tellg(); 
                infile.seekg(0, ios::beg); // Quay về đầu file

                // 2. Gửi gói START (Server -> Client)
                // Password chứa filesize, Data chứa tên file
                Message start_msg;
                start_msg.type = MSG_FILE_START; 
                sprintf(start_msg.password, "%d", filesize);
                strcpy(start_msg.data, filename.c_str());
                send(client_socket, (char*)&start_msg, sizeof(Message), 0);
                Sleep(10); // Nghỉ xíu

                // 3. Gửi nội dung file (DATA)
                char buffer[1024];
                while (!infile.eof()) {
                    infile.read(buffer, 1024);
                    int bytes_read = infile.gcount(); // Số byte thực tế đọc được
                    
                    if (bytes_read > 0) {
                        Message data_msg;
                        data_msg.type = MSG_FILE_DATA;
                        sprintf(data_msg.password, "%d", bytes_read); // Gửi độ dài chunk
                        memcpy(data_msg.data, buffer, bytes_read);    // Copy binary an toàn
                        
                        send(client_socket, (char*)&data_msg, sizeof(Message), 0);
                        Sleep(5); // Nghỉ để tránh dính gói tin
                    }
                }
                infile.close();

                // 4. Gửi gói END
                Message end_msg;
                end_msg.type = MSG_FILE_END;
                strcpy(end_msg.data, filename.c_str());
                send(client_socket, (char*)&end_msg, sizeof(Message), 0);
                
                cout << "[DOWNLOAD] Da gui xong file cho " << my_name << endl;
            } else {
                cout << "[ERROR] Khong tim thay file: " << filepath << endl;
            }
        }
    }

    if (is_logged_in) {
        EnterCriticalSection(&data_cs);
        online_users.erase(my_name);
        LeaveCriticalSection(&data_cs);
        cout << "[OFFLINE] " << my_name << endl;
    }
    closesocket(client_socket);
    return 0;
}

int main() {
    InitializeCriticalSection(&data_cs);
    system("mkdir Server\\Data 2> NUL"); // Tự tạo thư mục Data
    
    init_server_data(); // Load dữ liệu cũ

    WSADATA wsaData;
    WSAStartup(MAKEWORD(2, 2), &wsaData);

    SOCKET server_socket = socket(AF_INET, SOCK_STREAM, 0);
    sockaddr_in server_addr = {AF_INET, htons(SERVER_PORT)};
    server_addr.sin_addr.s_addr = INADDR_ANY;
    bind(server_socket, (sockaddr*)&server_addr, sizeof(server_addr));
    listen(server_socket, 5);

    cout << "=== SERVER FINAL STARTED ===" << endl;
    while (true) {
        SOCKET client = accept(server_socket, NULL, NULL);
        if (client != INVALID_SOCKET)
            CreateThread(NULL, 0, handle_client, (LPVOID)client, 0, NULL);
    }
    return 0;
}
#include <iostream>
#include <winsock2.h>
#include <windows.h>
#include <vector>
#include <string>
#include <map>
#include <set>        
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

// --- BIẾN TOÀN CỤC ---
CRITICAL_SECTION data_cs; 
map<string, SOCKET> online_users;     
map<string, GroupInfo> groups;        

// Map lưu lời mời kết bạn đang chờ (RAM)
// Key: Người nhận (UserB) -> Value: Danh sách người mời (UserA, UserC...)
map<string, set<string> > pending_invites; 

string trim(const string& str) {
    size_t first = str.find_first_not_of(" \t\r\n");
    if (string::npos == first) return "";
    size_t last = str.find_last_not_of(" \t\r\n");
    return str.substr(first, (last - first + 1));
}

// Hàm khởi tạo dữ liệu server từ database (File)
void init_server_data() {
    map<string, string> temp_groups;
    load_groups_to_memory(temp_groups);
    
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

// Hàm gửi lại toàn bộ dữ liệu cũ cho Client sau khi đăng nhập thành công
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
    vector<string> history = get_user_history(username); 
    msg.type = MSG_HISTORY;
    
    for (size_t i = 0; i < history.size(); ++i) {
        stringstream ss(history[i]);
        string segment;
        vector<string> seglist;
        while(getline(ss, segment, '|')) seglist.push_back(segment);
        
        if (seglist.size() >= 4) {
            int type = stoi(seglist[0]);
            string sender = seglist[1];
            string target = seglist[2];
            string content = seglist[3];

            bool send_it = false;

            if (type == MSG_PRIVATE_CHAT) {
                bool is_involved = (username == sender || username == target);
                
                if (is_involved) {
                    string partner = (sender == username) ? target : sender;
                    if (is_exist_in_vector(partner, friends)) {
                        send_it = true;
                    }
                }
            } 
            else if (type == MSG_GROUP_CHAT) {
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

    string target = trim(text_to_remove);

    while (getline(in, line)) {
        string clean_line = trim(line);
        if (!clean_line.empty() && clean_line != target) {
            out << line << endl;
        }
    }
    in.close();
    out.close();
    
    remove(filename.c_str());
    rename(temp_file.c_str(), filename.c_str());
}

// Hàm xử lý từng client kết nối
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
                vector<string> my_groups_list = get_user_groups(my_name);

                EnterCriticalSection(&data_cs); 
                for (size_t i = 0; i < my_groups_list.size(); ++i) {
                    string gname = my_groups_list[i];
                    if (groups.find(gname) != groups.end()) {
                        groups[gname].members.push_back(client_socket);
                        cout << "[RE-JOIN] User " << my_name << " da duoc add lai vao RAM cua nhom: " << gname << endl;
                    }
                }
                LeaveCriticalSection(&data_cs);

                sync_client_data(client_socket, my_name);
                
            } else {
                response.type = MSG_LOGIN_FAIL;
                send(client_socket, (char*)&response, sizeof(Message), 0);
            }
        }
        
        // 2. KẾT BẠN (CẬP NHẬT: Thêm logic lưu vào danh sách chờ)
        else if (msg.type == MSG_FRIEND_REQ) {
            EnterCriticalSection(&data_cs);
            string target(msg.target);
            string sender(msg.name);

            // A. Luôn lưu vào danh sách chờ (Pending List)
            pending_invites[target].insert(sender);

            // B. Nếu người kia online -> Gửi gói tin sang cho họ ngay (Popup)
            if (online_users.find(target) != online_users.end()) {
                send(online_users[target], (char*)&msg, sizeof(Message), 0);
                cout << "[FORWARD] Loi moi ket ban tu " << my_name << " -> " << target << endl;
            } else {
                cout << "[PENDING] Luu loi moi tu " << my_name << " -> " << target << " (Offline)" << endl;
            }
            LeaveCriticalSection(&data_cs);
        }

        // --- THÊM MỚI: YÊU CẦU LẤY DANH SÁCH LỜI MỜI (Nút Chuông) ---
        else if (msg.type == MSG_REQ_PENDING_LIST) {
            string list_str = "";
            EnterCriticalSection(&data_cs);
            
            // Kiểm tra xem user hiện tại có lời mời nào không
            if (pending_invites.find(my_name) != pending_invites.end()) {
                set<string>& list = pending_invites[my_name];
                for (set<string>::iterator it = list.begin(); it != list.end(); ++it) {
                    if (list_str.length() > 0) list_str += ",";
                    list_str += *it;
                }
            }
            LeaveCriticalSection(&data_cs);

            // Gửi danh sách về cho Client
            Message resp;
            resp.type = MSG_RESP_PENDING_LIST; // Type 28
            // Dữ liệu danh sách để vào trường data (vì có thể dài hơn 32 bytes)
            memset(resp.data, 0, sizeof(resp.data));
            if (!list_str.empty()) {
                strncpy(resp.data, list_str.c_str(), 1023);
            }
            send(client_socket, (char*)&resp, sizeof(Message), 0);
            cout << "[PENDING LIST] Gui danh sach cho " << my_name << ": " << list_str << endl;
        }
        
        // 3. CHẤP NHẬN KẾT BẠN (CẬP NHẬT: Xóa khỏi danh sách chờ)
        else if (msg.type == MSG_FRIEND_ACCEPT) {
            // Lưu vào file
            add_friend_db(msg.name, msg.target);
            
            // --- XÓA KHỎI PENDING LIST ---
            EnterCriticalSection(&data_cs);
            if (pending_invites.find(msg.name) != pending_invites.end()) {
                pending_invites[msg.name].erase(msg.target);
            }
            LeaveCriticalSection(&data_cs);
            // -----------------------------

            Message update;
            update.type = MSG_ADD_FRIEND_SUCC;
            
            // Báo cho MÌNH
            strcpy(update.target, msg.target); 
            send(client_socket, (char*)&update, sizeof(Message), 0);
            
            // Báo cho NGƯỜI KIA
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
                Message resp;
                resp.type = MSG_CREATE_GROUP_FAIL;
                strcpy(resp.data, "Ten nhom da ton tai!");
                send(client_socket, (char*)&resp, sizeof(Message), 0);
            } else {
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

        // 5. VÀO NHÓM
        else if (msg.type == MSG_JOIN_GROUP_REQ) {
            string gname(msg.target);
            string pass(msg.group_pass);
            bool ok = false;

            EnterCriticalSection(&data_cs);
            if (groups.find(gname) != groups.end()) {
                if (groups[gname].password == pass) {
                    ok = true;
                    groups[gname].members.push_back(client_socket);
                }
            }
            LeaveCriticalSection(&data_cs);

            if (ok) {
                add_group_member_db(gname, my_name);
                Message resp;
                resp.type = MSG_ADD_GROUP_SUCC;
                strcpy(resp.target, gname.c_str());
                send(client_socket, (char*)&resp, sizeof(Message), 0);
            } else {
                Message resp;
                resp.type = MSG_LOGIN_FAIL;
                send(client_socket, (char*)&resp, sizeof(Message), 0);
            }
        }

        // 6. YÊU CẦU XEM THÀNH VIÊN
        else if (msg.type == MSG_REQ_MEMBER_LIST) {
            vector<string> mems = get_group_members(msg.target);
            
            string mem_str = "";
            for (size_t i = 0; i < mems.size(); ++i) {
                mem_str += mems[i] + ", ";
            }
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
            save_message(my_name, msg.target, msg.data, MSG_PRIVATE_CHAT);
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
            string line_to_remove = gname + "|" + my_name;
            remove_line_from_file("Server/Data/group_members.txt", line_to_remove);

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

            Message resp;
            resp.type = MSG_REMOVE_CONTACT;
            strcpy(resp.target, gname.c_str());
            send(client_socket, (char*)&resp, sizeof(Message), 0);
            
            cout << "[LEAVE] " << my_name << " roi nhom " << gname << endl;
        }

        // 10. HỦY KẾT BẠN
        else if (msg.type == MSG_UNFRIEND) {
            string friend_name(msg.target);
            string line1 = my_name + "|" + friend_name;
            string line2 = friend_name + "|" + my_name;
            
            remove_line_from_file("Server/Data/friends.txt", line1);
            remove_line_from_file("Server/Data/friends.txt", line2);

            Message resp;
            resp.type = MSG_REMOVE_CONTACT;
            strcpy(resp.target, friend_name.c_str());
            send(client_socket, (char*)&resp, sizeof(Message), 0);
            
            EnterCriticalSection(&data_cs); 
            if (online_users.find(friend_name) != online_users.end()) {
                SOCKET friend_socket = online_users[friend_name];
                Message notify;
                notify.type = MSG_REMOVE_CONTACT;
                strcpy(notify.target, my_name.c_str()); 
                send(friend_socket, (char*)&notify, sizeof(Message), 0);
            }
            LeaveCriticalSection(&data_cs);
            
            cout << "[UNFRIEND] " << my_name << " - " << friend_name << endl;
        }

        // 11. BẮT ĐẦU NHẬN FILE
        if (msg.type == MSG_FILE_START) {
            string fname(msg.data);
            string sender(msg.name);
            time_t now = time(0);
            string new_fname = to_string(now) + "_" + fname;
            string save_path = "Server/Data/Files/" + new_fname;
            
            current_file = new ofstream(save_path.c_str(), ios::binary);
            current_filename = new_fname;
            cout << "[FILE START] " << sender << " gui file: " << fname << endl;
        }
        
        // 12. NHẬN DỮ LIỆU FILE (BINARY)
        else if (msg.type == MSG_FILE_DATA) {
            if (current_file && current_file->is_open()) {
                int chunk_len = atoi(msg.password); 
                if (chunk_len > 0 && chunk_len <= 1024) {
                    current_file->write(msg.data, chunk_len);
                }
            }
        }
        
        // 13. KẾT THÚC FILE
        else if (msg.type == MSG_FILE_END) {
            if (current_file) {
                current_file->close();
                delete current_file;
                current_file = NULL;
            }
            
            string sender(msg.name);
            string target(msg.target);
            cout << "[FILE END] Da luu file tu " << sender << endl;
            
            Message notify;
            notify.type = MSG_FILE_NOTIFY;
            strcpy(notify.name, sender.c_str());
            strcpy(notify.target, target.c_str());
            strcpy(notify.data, current_filename.c_str()); 
            
            EnterCriticalSection(&data_cs);
            if (online_users.find(target) != online_users.end()) {
                send(online_users[target], (char*)&notify, sizeof(Message), 0);
            }
            else if (groups.find(target) != groups.end()) {
                 vector<SOCKET>& mems = groups[target].members;
                 for (size_t i = 0; i < mems.size(); ++i) {
                     if (mems[i] != client_socket) {
                         send(mems[i], (char*)&notify, sizeof(Message), 0);
                     }
                 }
            }
            LeaveCriticalSection(&data_cs);
            
            string history_content = "[FILE] " + current_filename;
            int type_save = (groups.find(target) != groups.end()) ? MSG_GROUP_CHAT : MSG_PRIVATE_CHAT;
            save_message(sender, target, history_content, type_save);
        }

        // 14. XỬ LÝ YÊU CẦU TẢI FILE TỪ CLIENT
        else if (msg.type == MSG_FILE_DOWNLOAD_REQ) {
            string filename(msg.data);
            string filepath = "Server/Data/Files/" + filename;
            cout << "[DOWNLOAD] Client " << my_name << " yeu cau tai: " << filename << endl;

            ifstream infile(filepath.c_str(), ios::binary | ios::ate);
            if (infile.is_open()) {
                int filesize = infile.tellg(); 
                infile.seekg(0, ios::beg);

                Message start_msg;
                start_msg.type = MSG_FILE_START; 
                sprintf(start_msg.password, "%d", filesize);
                strcpy(start_msg.data, filename.c_str());
                send(client_socket, (char*)&start_msg, sizeof(Message), 0);
                Sleep(10); 

                char buffer[1024];
                while (!infile.eof()) {
                    infile.read(buffer, 1024);
                    int bytes_read = infile.gcount();
                    
                    if (bytes_read > 0) {
                        Message data_msg;
                        data_msg.type = MSG_FILE_DATA;
                        sprintf(data_msg.password, "%d", bytes_read);
                        memcpy(data_msg.data, buffer, bytes_read); 
                        send(client_socket, (char*)&data_msg, sizeof(Message), 0);
                        Sleep(5);
                    }
                }
                infile.close();

                Message end_msg;
                end_msg.type = MSG_FILE_END;
                strcpy(end_msg.data, filename.c_str());
                send(client_socket, (char*)&end_msg, sizeof(Message), 0);
                cout << "[DOWNLOAD] Done." << endl;
            } else {
                cout << "[ERROR] Khong tim thay file: " << filepath << endl;
            }
        }

        // 15. XỬ LÝ GAME CARO
        else if (msg.type == MSG_GAME_REQ || msg.type == MSG_GAME_ACCEPT || 
                 msg.type == MSG_GAME_MOVE || msg.type == MSG_GAME_END) {
            
            string sender(msg.name);
            string target(msg.target);
            
            EnterCriticalSection(&data_cs);
            if (online_users.find(target) != online_users.end()) {
                strcpy(msg.name, sender.c_str()); 
                send(online_users[target], (char*)&msg, sizeof(Message), 0);
            }
            LeaveCriticalSection(&data_cs);
            
            cout << "[GAME] " << sender << " -> " << target << " (Type: " << msg.type << ")" << endl;
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
    system("mkdir Server\\Data 2> NUL"); 
    
    init_server_data(); 

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
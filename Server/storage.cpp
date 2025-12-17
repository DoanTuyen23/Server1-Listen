#include "storage.h"
#include <fstream>
#include <iostream>
#include <sys/stat.h>
#include <direct.h>

using namespace std;

const string DATA_DIR = "Server/Data";
const string ACC_FILE = "Server/Data/accounts.txt";
const string FRIENDS_FILE = "Server/Data/friends.txt";
const string GROUPS_FILE = "Server/Data/groups.txt";
const string G_MEM_FILE = "Server/Data/group_members.txt";
const string MSG_FILE = "Server/Data/messages.txt";

// Hàm đảm bảo thư mục dữ liệu tồn tại
void ensure_data_dir() {
    _mkdir("Server");
    _mkdir(DATA_DIR.c_str());
}

// Hàm kiểm tra đăng nhập hoặc đăng ký tài khoản
bool check_login(string username, string password) {
    ensure_data_dir();
    ifstream file(ACC_FILE);
    string line;
    if (file.is_open()) {
        while (getline(file, line)) {
            size_t del = line.find("|");
            if (del != string::npos) {
                if (line.substr(0, del) == username) return (line.substr(del + 1) == password);
            }
        }
        file.close();
    }
    ofstream outfile(ACC_FILE, ios::app);
    outfile << username << "|" << password << endl; // Đăng ký tài khoản mới
    return true; 
}

// Hàm thêm bạn bè vào cơ sở dữ liệu
void add_friend_db(string user1, string user2) {
    ensure_data_dir();
    ofstream file(FRIENDS_FILE, ios::app);
    file << user1 << "|" << user2 << endl;
}

// Hàm lấy danh sách bạn bè của một người dùng
vector<string> get_friend_list(string user) {
    vector<string> list;
    ifstream file(FRIENDS_FILE);
    string line;
    while (getline(file, line)) {
        size_t del = line.find("|");
        if (del == string::npos) continue;
        string u1 = line.substr(0, del);
        string u2 = line.substr(del + 1);
        if (u1 == user) list.push_back(u2);
        else if (u2 == user) list.push_back(u1);
    }
    return list;
}

// Hàm tạo nhóm mới
void create_group_db(string name, string pass) {
    ensure_data_dir();
    ofstream file(GROUPS_FILE, ios::app);
    file << name << "|" << pass << endl;
}

// Hàm thêm thành viên vào nhóm
void add_group_member_db(string group, string user) {
    ensure_data_dir();
    ofstream file(G_MEM_FILE, ios::app);
    file << group << "|" << user << endl;
}

// Hàm lấy danh sách nhóm của một người dùng
vector<string> get_user_groups(string user) {
    vector<string> list;
    ifstream file(G_MEM_FILE);
    string line;
    while (getline(file, line)) {
        size_t del = line.find("|");
        if (del == string::npos) continue;
        string g = line.substr(0, del);
        string u = line.substr(del + 1);
        if (u == user) list.push_back(g);
    }
    return list;
}

// Hàm load nhóm vào bộ nhớ
void load_groups_to_memory(map<string, string>& groups_map) {
    ifstream file(GROUPS_FILE);
    string line;
    while (getline(file, line)) {
        size_t del = line.find("|");
        if (del != string::npos) groups_map[line.substr(0, del)] = line.substr(del + 1);
    }
}

// Hàm lưu tin nhắn vào cơ sở dữ liệu
void save_message(string sender, string target, string content, int type) {
    ensure_data_dir();
    ofstream file(MSG_FILE, ios::app);
    file << type << "|" << sender << "|" << target << "|" << content << endl;
}

// Hàm lấy lịch sử tin nhắn của người dùng
vector<string> get_user_history(string username) {
    vector<string> history;
    ifstream file(MSG_FILE);
    string line;
    while (getline(file, line)) history.push_back(line);
    return history;
}

// Hàm lấy danh sách thành viên của 1 nhóm
vector<string> get_group_members(string group_name) {
    vector<string> list;
    ifstream file(G_MEM_FILE.c_str()); // Đọc file group_members.txt
    string line;
    while (getline(file, line)) {
        // Định dạng file: GroupName|UserName
        size_t del = line.find("|");
        if (del == string::npos) continue;
        
        string g = line.substr(0, del);
        string u = line.substr(del + 1);
        
        // Nếu dòng này là của nhóm đang tìm -> Thêm user vào danh sách
        if (g == group_name) {
            // Xử lý xóa ký tự xuống dòng (nếu có) để tránh lỗi hiển thị
            while (!u.empty() && (u.back() == '\r' || u.back() == '\n')) {
                u.pop_back();
            }
            list.push_back(u);
        }
    }
    return list;
}
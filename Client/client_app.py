import customtkinter as ctk
import socket
import threading
import struct
import tkinter as tk
from tkinter import messagebox, simpledialog
from datetime import datetime
from tkinter import filedialog 
import os 
import subprocess 
import sys

# --- CẤU HÌNH ---
SERVER_IP = '127.0.0.1'
SERVER_PORT = 8888
PACK_FORMAT = f'i 32s 32s 32s 32s 1024s'
PACK_SIZE = 1156

# Message Types
MSG_LOGIN_REQ = 0
MSG_LOGIN_SUCCESS = 1
MSG_LOGIN_FAIL = 2
MSG_PRIVATE_CHAT = 3
MSG_GROUP_CHAT = 4
MSG_FRIEND_REQ = 5
MSG_FRIEND_ACCEPT = 6
MSG_ADD_FRIEND_SUCC = 7
MSG_CREATE_GROUP_REQ = 8
MSG_JOIN_GROUP_REQ = 9
MSG_ADD_GROUP_SUCC = 10
MSG_HISTORY = 11
MSG_CREATE_GROUP_FAIL = 12
MSG_REQ_MEMBER_LIST   = 13
MSG_RESP_MEMBER_LIST  = 14

MSG_LEAVE_GROUP       = 15
MSG_UNFRIEND          = 16
MSG_REMOVE_CONTACT    = 17  # Server báo Client xóa nút khỏi Sidebar

MSG_FILE_START        = 18  # Bắt đầu gửi file
MSG_FILE_DATA         = 19  # Dữ liệu file
MSG_FILE_END          = 20  # Kết thúc gửi file
MSG_FILE_NOTIFY       = 21  # Thông báo đã gửi file
MSG_FILE_DOWNLOAD_REQ = 22  # Yêu cầu tải file
MSG_GAME_REQ          = 23  # Yêu cầu chơi game
MSG_GAME_ACCEPT       = 24  # Chấp nhận chơi game
MSG_GAME_MOVE         = 25  # Di chuyển trong game
MSG_GAME_END          = 26  # Kết thúc game

# --- LỚP GIAO DIỆN ---
class ContactButton(ctk.CTkButton):
    # Thêm tham số on_right_click vào cuối
    def __init__(self, master, real_name, display_text, type, callback, on_right_click):
        super().__init__(master, text=display_text, anchor="w", command=lambda: callback(real_name, type))
        self.type = type
        self.real_name = real_name
        self.pack(fill="x", pady=2, padx=5)
        self.configure(fg_color="transparent", text_color="white", height=40)
        
        # Gắn sự kiện chuột phải
        self.bind("<Button-3>", lambda event: on_right_click(event, real_name, type))

    def set_unread(self, active):
        if active: self.configure(fg_color="#C0392B") 
        else: self.configure(fg_color="transparent")

    def set_active_bg(self, active):
        if active: self.configure(fg_color="#2980B9") 
        else: self.configure(fg_color="transparent")

# --- LỚP BÀN CỜ CARO ---
class CaroBoard(ctk.CTkToplevel):
    def __init__(self, master, enemy_name, my_turn, symbol, on_move_callback):
        super().__init__(master)
        self.title(f"Caro: Bạn vs {enemy_name}")
        
        # --- CẤU HÌNH KÍCH THƯỚC ---
        w_child = 600  # Chiều rộng cửa sổ game
        h_child = 650  # Chiều cao cửa sổ game

        # --- TÍNH TOÁN VỊ TRÍ CĂN GIỮA ---
        # 1. Lấy thông tin vị trí và kích thước của cửa sổ cha (Ứng dụng chính)
        # master ở đây chính là self (của ChatClient) được truyền vào
        x_parent = master.winfo_x()
        y_parent = master.winfo_y()
        w_parent = master.winfo_width()
        h_parent = master.winfo_height()

        # 2. Tính toán tọa độ (x, y) mới để tâm trùng nhau
        # Công thức: Vị trí cha + (Rộng cha - Rộng con) / 2
        new_x = int(x_parent + (w_parent - w_child) / 2)
        new_y = int(y_parent + (h_parent - h_child) / 2)

        # 3. Thiết lập hình học: Rộng x Cao + Tọa độ X + Tọa độ Y
        self.geometry(f"{w_child}x{h_child}+{new_x}+{new_y}")
        
        self.resizable(False, False)
        
        self.enemy_name = enemy_name
        self.my_turn = my_turn
        self.my_symbol = symbol # "X" hoặc "O"
        self.enemy_symbol = "O" if symbol == "X" else "X"
        self.on_move_callback = on_move_callback
        
        self.BOARD_SIZE = 15
        self.CELL_SIZE = 35
        self.board_data = {} # Lưu nước đi: key="row_col", value="X"/"O"
        self.game_over = False

        # Status Label
        status_text = "Lượt của BẠN" if my_turn else f"Lượt của {enemy_name}"
        color = "green" if my_turn else "red"
        self.lbl_status = ctk.CTkLabel(self, text=status_text, font=("Arial", 18, "bold"), text_color=color)
        self.lbl_status.pack(pady=10)

        # Canvas bàn cờ
        canvas_size = self.BOARD_SIZE * self.CELL_SIZE
        self.canvas = tk.Canvas(self, width=canvas_size, height=canvas_size, bg="#F0D9B5", highlightthickness=0)
        self.canvas.pack()
        self.canvas.bind("<Button-1>", self.on_click)

        self.draw_grid()
        self.protocol("WM_DELETE_WINDOW", self.on_close)
        
        # Focus ngay vào cửa sổ này để người dùng nhận biết
        self.lift()
        self.focus_force()

    def draw_grid(self):
        for i in range(self.BOARD_SIZE):
            # Vẽ kẻ ngang
            self.canvas.create_line(0, i*self.CELL_SIZE, self.BOARD_SIZE*self.CELL_SIZE, i*self.CELL_SIZE)
            # Vẽ kẻ dọc
            self.canvas.create_line(i*self.CELL_SIZE, 0, i*self.CELL_SIZE, self.BOARD_SIZE*self.CELL_SIZE)

    def on_click(self, event):
        if self.game_over or not self.my_turn: return

        # Tính toán tọa độ lưới
        col = event.x // self.CELL_SIZE
        row = event.y // self.CELL_SIZE
        
        if 0 <= col < self.BOARD_SIZE and 0 <= row < self.BOARD_SIZE:
            key = f"{row}_{col}"
            if key not in self.board_data:
                # 1. Vẽ nước đi của mình
                self.draw_symbol(row, col, self.my_symbol)
                self.board_data[key] = self.my_symbol
                
                # 2. Kiểm tra thắng
                if self.check_win(row, col, self.my_symbol):
                    self.game_over = True
                    self.lbl_status.configure(text="BẠN THẮNG! 🏆", text_color="gold")
                    messagebox.showinfo("Kết quả", "Chúc mừng! Bạn đã thắng.")
                else:
                    self.set_turn(False)
                
                # 3. Gửi nước đi cho Server
                self.on_move_callback(row, col, self.game_over)

    def opponent_move(self, row, col):
        """Xử lý khi đối thủ đi"""
        key = f"{row}_{col}"
        if key not in self.board_data:
            self.draw_symbol(row, col, self.enemy_symbol)
            self.board_data[key] = self.enemy_symbol
            
            # Kiểm tra xem nó có thắng mình không (Check hộ luôn cho chắc)
            if self.check_win(row, col, self.enemy_symbol):
                self.game_over = True
                self.lbl_status.configure(text="BẠN THUA RỒI! 💀", text_color="red")
                messagebox.showinfo("Kết quả", "Bạn đã thua!")
            else:
                self.set_turn(True)

    # Vẽ ký hiệu (X hoặc O) lên canvas
    def draw_symbol(self, row, col, symbol):
        x = col * self.CELL_SIZE + self.CELL_SIZE // 2
        y = row * self.CELL_SIZE + self.CELL_SIZE // 2
        r = self.CELL_SIZE // 2 - 4
        
        color = "red" if symbol == "X" else "blue"
        if symbol == "X":
            self.canvas.create_line(x-r, y-r, x+r, y+r, width=3, fill=color)
            self.canvas.create_line(x+r, y-r, x-r, y+r, width=3, fill=color)
        else:
            self.canvas.create_oval(x-r, y-r, x+r, y+r, width=3, outline=color)
    
    # Cập nhật trạng thái lượt chơi
    def set_turn(self, is_my_turn):
        self.my_turn = is_my_turn
        if is_my_turn:
            self.lbl_status.configure(text="Lượt của BẠN", text_color="green")
        else:
            self.lbl_status.configure(text=f"Đợi {self.enemy_name}...", text_color="gray")
    
    # Kiểm tra thắng sau mỗi nước đi
    def check_win(self, row, col, symbol):
        # 4 Hướng: Ngang, Dọc, Chéo chính, Chéo phụ
        directions = [(0, 1), (1, 0), (1, 1), (1, -1)]
        for dr, dc in directions:
            count = 1
            # Duyệt xuôi
            for i in range(1, 5):
                r, c = row + dr*i, col + dc*i
                if self.board_data.get(f"{r}_{c}") == symbol: count += 1
                else: break
            # Duyệt ngược
            for i in range(1, 5):
                r, c = row - dr*i, col - dc*i
                if self.board_data.get(f"{r}_{c}") == symbol: count += 1
                else: break
            
            if count >= 5: return True
        return False

    # Xử lý khi đóng cửa sổ
    def on_close(self):
        if not self.game_over:
            if messagebox.askyesno("Thoát", "Đang chơi mà thoát là thua đó nha?"):
                self.destroy()
        else:
            self.destroy()

# --- LỚP ỨNG DỤNG CHÍNH ---
class ChatClient(ctk.CTk):
    def __init__(self):
        super().__init__()
        self.title("Messenger Pro Max")
        self.geometry("1100x700")
        ctk.set_appearance_mode("Dark")
        
        self.client = None
        self.my_name = ""
        self.contacts = {} 
        self.messages = {} 
        self.current_target = None

        self.BATCH_SIZE = 20 # Chỉ hiện 20 tin mỗi lần load
        self.current_display_count = 0 # Đếm xem đang hiện bao nhiêu tin
        
        # Biến hỗ trợ chơi game
        self.game_window = None
        
        self.init_ui()

    def init_ui(self):
        # LOGIN SCREEN
        self.login_frame = ctk.CTkFrame(self)
        self.login_frame.pack(fill="both", expand=True)
        ctk.CTkLabel(self.login_frame, text="ĐĂNG NHẬP", font=("Arial", 30, "bold")).pack(pady=40)
        self.entry_user = ctk.CTkEntry(self.login_frame, placeholder_text="Username", width=300)
        self.entry_user.pack(pady=10)
        self.entry_pass = ctk.CTkEntry(self.login_frame, placeholder_text="Password", show="*", width=300)
        self.entry_pass.pack(pady=10)
        ctk.CTkButton(self.login_frame, text="Login", command=self.login, width=300).pack(pady=20)

        # MAIN SCREEN
        self.main_ui = ctk.CTkFrame(self)
        
        # Sidebar
        self.sidebar = ctk.CTkFrame(self.main_ui, width=260, corner_radius=0)
        self.sidebar.pack(side="left", fill="y")
        
        self.lbl_name = ctk.CTkLabel(self.sidebar, text="...", font=("Arial", 20, "bold"))
        self.lbl_name.pack(pady=15)
        
        self.entry_add = ctk.CTkEntry(self.sidebar, placeholder_text="Nhập tên người/nhóm...")
        self.entry_add.pack(fill="x", padx=10, pady=5)
        
        btn_frame = ctk.CTkFrame(self.sidebar, fg_color="transparent")
        btn_frame.pack(fill="x", padx=5)
        ctk.CTkButton(btn_frame, text="+ Bạn", width=70, fg_color="green", command=self.req_friend).pack(side="left", padx=2)
        ctk.CTkButton(btn_frame, text="+ Nhóm", width=70, fg_color="#D35400", command=self.create_group).pack(side="left", padx=2)
        ctk.CTkButton(btn_frame, text="Vào Nhóm", width=70, fg_color="#2980B9", command=self.join_group).pack(side="left", padx=2)

        ctk.CTkLabel(self.sidebar, text="─── DANH SÁCH ───").pack(pady=10)
        self.scroll_contacts = ctk.CTkScrollableFrame(self.sidebar, fg_color="transparent")
        self.scroll_contacts.pack(fill="both", expand=True)

        # Chat Area
        self.right_frame = ctk.CTkFrame(self.main_ui)
        self.right_frame.pack(side="right", fill="both", expand=True)

        # Header Frame (Để chứa tên nhóm và nút xem thành viên)
        self.header_frame = ctk.CTkFrame(self.right_frame, height=40, fg_color="#222")
        self.header_frame.pack(fill="x")

        self.header_chat = ctk.CTkLabel(self.header_frame, text="Chào mừng!", font=("Arial", 16, "bold"), text_color="white")
        self.header_chat.pack(side="left", padx=20, pady=5)

        # Nút xem thành viên (Mặc định ẩn, chỉ hiện khi chat nhóm)
        self.btn_members = ctk.CTkButton(self.header_frame, text="Thành viên", width=80, height=25, 
                                         fg_color="#555", command=self.req_members)
        
        self.header_chat = ctk.CTkLabel(self.right_frame, text="Chào mừng!", font=("Arial", 16, "bold"), height=40, fg_color="#222")
        self.header_chat.pack(fill="x")

        self.scroll_chat = ctk.CTkScrollableFrame(self.right_frame, fg_color="#1a1a1a")
        self.scroll_chat.pack(fill="both", expand=True, padx=5, pady=5)

        self.input_frame = ctk.CTkFrame(self.right_frame, height=50)
        self.input_frame.pack(fill="x", padx=5, pady=5)

        # --- NÚT GỬI FILE (BÊN TRÁI) ---
        self.btn_file = ctk.CTkButton(self.input_frame, text="+", width=35, fg_color="#444", command=self.choose_file)
        self.btn_file.pack(side="left", padx=5)
        
        # --- NÚT CHƠI GAME (BÊN TRÁI) ---
        self.btn_game = ctk.CTkButton(self.input_frame, text="🎮", width=35, fg_color="#8e44ad", command=self.req_game)
        self.btn_game.pack(side="left", padx=5)

        self.entry_msg = ctk.CTkEntry(self.input_frame, placeholder_text="Nhập tin nhắn...")
        self.entry_msg.pack(side="left", fill="x", expand=True, padx=5)
        self.entry_msg.bind("<Return>", self.send_msg)
        
        # Biến hỗ trợ tải file
        self.downloading_file = None # Biến giữ file đang tải về
        self.downloading_path = ""   # Đường dẫn lưu file

        ctk.CTkButton(self.input_frame, text="Gửi", width=60, command=self.send_msg).pack(side="right", padx=5)
    
    # --- HÀM ĐÓNG GÓI DỮ LIỆU AN TOÀN ---
    def pack(self, type, name="", pwd="", target="", gpwd="", data=""):
        return struct.pack(PACK_FORMAT, type, name.encode(), pwd.encode(), target.encode(), gpwd.encode(), data.encode())

    # --- HÀM ĐĂNG NHẬP ---
    def login(self):
        u = self.entry_user.get()
        p = self.entry_pass.get()
        try:
            self.client = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
            self.client.connect((SERVER_IP, SERVER_PORT))
            self.client.send(self.pack(MSG_LOGIN_REQ, u, p))
            
            raw = self.client.recv(PACK_SIZE)
            data = struct.unpack(PACK_FORMAT, raw)
            if data[0] == MSG_LOGIN_SUCCESS:
                self.my_name = u
                self.lbl_name.configure(text=f"Hi, {u}")
                self.login_frame.pack_forget()
                self.main_ui.pack(fill="both", expand=True)
                threading.Thread(target=self.loop, daemon=True).start()
            else:
                messagebox.showerror("Lỗi", "Sai thông tin đăng nhập")
        except Exception as e: messagebox.showerror("Lỗi", f"Lỗi kết nối: {e}")

    # --- HÀM LẶP NHẬN DỮ LIỆU TỪ SERVER ---
    def loop(self):
        buffer = b""
        while True:
            try:
                chunk = self.client.recv(4096)
                if not chunk: break
                buffer += chunk
                while len(buffer) >= PACK_SIZE:
                    packet = buffer[:PACK_SIZE]
                    buffer = buffer[PACK_SIZE:]
                    data = struct.unpack(PACK_FORMAT, packet)
                    self.after(0, self.handle_packet, data)
            except: break

    # --- HÀM CHỌN FILE ĐỂ GỬI ---
    def choose_file(self):
        if not self.current_target:
            messagebox.showwarning("Chú ý", "Hãy chọn người nhận trước!")
            return

        # Mở hộp thoại chọn file
        filepath = filedialog.askopenfilename()
        if filepath:
            # Chạy thread gửi file để không lag giao diện
            threading.Thread(target=self.sending_file_thread, args=(filepath,)).start()

    # Hàm gửi file trong thread riêng
    def sending_file_thread(self, filepath):
        try:
            filename = os.path.basename(filepath)
            filesize = os.path.getsize(filepath)
            
            # Hiển thị trạng thái đang gửi (Text tạm thời)
            self.after(0, self.render_bubble, self.my_name, f"Dang gui: {filename}...", True, True) # is_sys=True để hiện chữ nghiêng

            # 1. Gửi gói START
            self.client.send(self.pack(MSG_FILE_START, self.my_name, str(filesize), self.current_target, "", filename))
            
            # 2. Đọc file và gửi từng chunk
            with open(filepath, "rb") as f:
                while True:
                    chunk = f.read(1024) 
                    if not chunk: break
                    
                    chunk_len = len(chunk)
                    padded_chunk = chunk.ljust(1024, b'\0') 
                    
                    pkt = struct.pack(PACK_FORMAT, 
                                      MSG_FILE_DATA, 
                                      self.my_name.encode(), 
                                      str(chunk_len).encode(), 
                                      self.current_target.encode(), 
                                      b"", 
                                      padded_chunk)
                    self.client.send(pkt)
                    
                    import time
                    time.sleep(0.005) 

            # 3. Gửi gói END
            self.client.send(self.pack(MSG_FILE_END, self.my_name, "", self.current_target))
           # Định dạng nội dung hiển thị cho khớp với Server (có chữ [FILE])
            display_text = f"[FILE] {filename}"
            
            # A. Lưu vào RAM (Để click qua lại không bị mất nút)
            if self.current_target not in self.messages: 
                self.messages[self.current_target] = []

            self.messages[self.current_target].append({
                'sender': self.my_name,
                'content': display_text,
                'is_sys': False,
                'is_file': True,       
                'filename': filename  
            })

            # B. Vẽ nút File lên màn hình (Thay thế dòng thông báo text cũ)
            # Dùng lambda trong after để truyền được nhiều tham số
            self.after(0, lambda: self.render_bubble(
                sender=self.my_name, 
                content=display_text, 
                is_me=True, 
                is_sys=False, 
                is_file=True,        # Kích hoạt chế độ vẽ nút
                filename=filename
            ))
            
            self.after(50, self.scroll_to_bottom)
            
        except Exception as e:
            print(f"Lỗi gửi file: {e}")
            messagebox.showerror("Lỗi", "Không thể gửi file!")

    def handle_packet(self, data):
        """Xử lý logic khi nhận được gói tin"""
        
        # --- HÀM GIẢI MÃ AN TOÀN ---
        def decode_safe(bytes_data):
            try:
                return bytes_data.partition(b'\0')[0].decode('utf-8', errors='replace')
            except:
                return ""

        m_type = data[0]
        sender = decode_safe(data[1])
        # pass (data[2]) bỏ qua ở đây, xử lý trong process_chat_msg nếu cần
        target = decode_safe(data[3])
        # group_pass (data[4])
        content = decode_safe(data[5])
        
        print(f"[DEBUG] Type={m_type} | Sender={sender} | Target={target}") 
\
        if m_type in [MSG_PRIVATE_CHAT, MSG_GROUP_CHAT, MSG_HISTORY]:
            self.process_chat_msg(m_type, sender, target, content, data)

        # 2. Xử lý thông báo thêm bạn/nhóm thành công
        elif m_type == MSG_ADD_FRIEND_SUCC:
            self.add_contact_btn(target, "PRIVATE")
            self.add_system_message(target, "Hai bạn đã trở thành bạn bè.")
            
        elif m_type == MSG_ADD_GROUP_SUCC:
            self.add_contact_btn(target, "GROUP")
            self.add_system_message(target, f"Bạn đã tham gia nhóm {target}")
            
        # 3. Xử lý lời mời kết bạn
        elif m_type == MSG_FRIEND_REQ:
            ans = messagebox.askyesno("Kết bạn", f"{sender} muốn kết bạn. Đồng ý?")
            if ans:
                self.client.send(self.pack(MSG_FRIEND_ACCEPT, self.my_name, "", sender))

        # Xử lý lỗi tạo nhóm trùng tên
        elif m_type == MSG_CREATE_GROUP_FAIL:
            messagebox.showerror("Thất bại", content)

        # Xử lý hiển thị danh sách thành viên
        elif m_type == MSG_RESP_MEMBER_LIST:
            # content chứa danh sách thành viên
            # target chứa tên nhóm
            messagebox.showinfo(f"Thành viên nhóm {target}", f"Danh sách:\n{content}")

        # --- XÓA NÚT KHI RỜI NHÓM/HỦY KẾT BẠN THÀNH CÔNG ---
        elif m_type == MSG_REMOVE_CONTACT:
            target_name = target # Tên cần xóa
            
            # 1. Xóa nút khỏi giao diện
            if target_name in self.contacts:
                self.contacts[target_name].destroy() # Xóa widget
                del self.contacts[target_name]       # Xóa khỏi dict
            
            # 2. Xóa dữ liệu chat cũ (tùy chọn)
            if target_name in self.messages:
                del self.messages[target_name]

            # 3. Nếu đang mở đoạn chat đó thì clear màn hình
            if self.current_target == target_name:
                self.current_target = None
                self.header_chat.configure(text="...")
                self.btn_members.pack_forget() # Ẩn nút thành viên
                for w in self.scroll_chat.winfo_children(): w.destroy()
                messagebox.showinfo("Thông báo", f"Đã xóa liên hệ {target_name}")

        # --- XỬ LÝ NHẬN FILE MỚI ---
        elif m_type == MSG_FILE_NOTIFY:
            # content chính là tên file (VD: baitap.docx)
            display_text = f"[FILE] {content}"
            
            # 1. Xác định đoạn chat (Private hay Group)
            chat_key = ""
            if target == self.my_name: # Chat riêng (Người khác gửi cho mình)
                chat_key = sender
                mode = "PRIVATE"
            else: # Chat nhóm (Người khác gửi vào nhóm)
                chat_key = target
                mode = "GROUP"
                
            # 2. Lưu tin nhắn vào RAM 
            # QUAN TRỌNG: Lưu thêm cờ 'is_file' và 'filename' để phục vụ việc tải sau này
            if chat_key not in self.messages: self.messages[chat_key] = []
            
            self.messages[chat_key].append({
                'sender': sender, 
                'content': display_text, 
                'is_sys': False,
                'is_file': True,      # Đánh dấu đây là tin nhắn chứa file
                'filename': content   # Lưu tên file gốc (quan trọng để gửi yêu cầu tải)
            })
            
            # 3. Tạo nút trên Sidebar nếu chưa có (Trường hợp người lạ gửi file)
            if chat_key not in self.contacts:
                self.add_contact_btn(chat_key, mode)
                
            # 4. Cập nhật giao diện
            if self.current_target == chat_key:

                self.render_bubble(sender, display_text, False, False, is_file=True, filename=content)
                
                # Cuộn xuống dưới cùng để thấy file mới
                self.after(50, self.scroll_to_bottom)
            else:
                # Nếu đang không mở cuộc trò chuyện này thì báo đỏ (unread)
                if chat_key in self.contacts: self.contacts[chat_key].set_unread(True)
        
        # 1. SERVER BẮT ĐẦU GỬI FILE VỀ
        elif m_type == MSG_FILE_START:
            
            print(f"[DOWNLOAD] Bat dau nhan file size={sender} bytes") # sender chứa filesize do server gửi

        # 2. NHẬN DỮ LIỆU FILE
        elif m_type == MSG_FILE_DATA:
            if self.downloading_file:
                try:
                    chunk_len_str = data[2].partition(b'\0')[0].decode('utf-8', errors='replace')
                    
                    if chunk_len_str.isdigit():
                        chunk_len = int(chunk_len_str)
                        chunk_data = data[5][:chunk_len]
                        
                        self.downloading_file.write(chunk_data)
                except Exception as e:
                    print(f"Lỗi ghi file: {e}")

        # 3. KẾT THÚC TẢI
        elif m_type == MSG_FILE_END:
            if self.downloading_file:
                self.downloading_file.close()
                self.downloading_file = None
                
                ans = messagebox.askyesno("Tải xong", "Đã tải xong file. Bạn có muốn mở ngay không?")
                if ans:
                    try:
                        # Mở file trên Windows
                        os.startfile(self.downloading_path)
                    except:
                        # Fallback cho các OS khác (nếu cần)
                        subprocess.call(['open', self.downloading_path])
        # 4. GAME: NHẬN LỜI MỜI
        elif m_type == MSG_GAME_REQ:
            ans = messagebox.askyesno("Thách đấu", f"{sender} muốn chơi Caro với bạn. Chiến không?")
            if ans:
                # Đồng ý -> Gửi gói ACCEPT -> Mình đi sau (O)
                self.client.send(self.pack(MSG_GAME_ACCEPT, self.my_name, "", sender))
                # Mình (người nhận lời mời) sẽ là O, đi sau
                self.after(0, lambda: self.start_game(sender, False, "O"))
                self.current_target = sender # Chuyển tab chat sang đối thủ luôn

        # 5. GAME: ĐỐI PHƯƠNG ĐỒNG Ý
        elif m_type == MSG_GAME_ACCEPT:
           # Mình (người mời) sẽ là X, đi trước
            messagebox.showinfo("Vào game", f"{sender} đã đồng ý! Bạn (X) đi trước.")
            self.after(0, lambda: self.start_game(sender, True, "X"))

        # 6. GAME: NHẬN NƯỚC ĐI
        elif m_type == MSG_GAME_MOVE:
            # content chứa "row,col"
            try:
                r_str, c_str = content.split(',')
                row, col = int(r_str), int(c_str)

                if self.game_window:
                    self.game_window.opponent_move(row, col)

                    # Kiểm tra xem họ có báo WIN không (trong trường password - data[2])
                    raw_flags = data[2].partition(b'\0')[0].decode('utf-8', errors='replace')
                    if "WIN" in raw_flags:
                        self.game_window.lbl_status.configure(text="BẠN THUA RỒI! 💀", text_color="red")
                        self.game_window.game_over = True
                        messagebox.showinfo("Kết quả", "Đối thủ đã thắng!")
            except: pass

        

    def process_chat_msg(self, type, sender, target, content, raw_data):
        """Xử lý tin nhắn chat (Private, Group, History)"""
        if type in [MSG_PRIVATE_CHAT, MSG_GROUP_CHAT] and content.startswith("[FILE] "):
            return 

        chat_key = ""
        is_history = (type == MSG_HISTORY)
        
        # --- LOGIC XÁC ĐỊNH NGƯỜI CHAT ---
        if is_history:
            # Decode password để lấy type gốc
            raw_pass_cleaned = raw_data[2].partition(b'\0')[0].decode('utf-8', errors='replace')
            real_type = int(raw_pass_cleaned) if raw_pass_cleaned.isdigit() else MSG_PRIVATE_CHAT
            
            if real_type == MSG_PRIVATE_CHAT:
                chat_key = sender if sender != self.my_name else target
                mode = "PRIVATE"
            else:
                chat_key = target
                mode = "GROUP"
        else:
            if type == MSG_PRIVATE_CHAT:
                chat_key = sender if sender != self.my_name else target
                mode = "PRIVATE"
            else:
                chat_key = target
                mode = "GROUP"

        # --- PHÁT HIỆN FILE TỪ LỊCH SỬ ---
        is_file_msg = False
        filename = ""
        
        # Server lưu file dưới dạng: "[FILE] ten_file.ext"
        if content.startswith("[FILE] "):
            is_file_msg = True
            filename = content[7:] 
        
        # --- LƯU VÀO RAM ---
        if chat_key not in self.messages: self.messages[chat_key] = []
        
        self.messages[chat_key].append({
            'sender': sender, 
            'content': content, 
            'is_sys': False,
            'is_file': is_file_msg,  # Lưu cờ báo hiệu đây là file
            'filename': filename     # Lưu tên file để tải về
        })
        
        # Tạo nút sidebar nếu chưa có
        if chat_key not in self.contacts:
            self.add_contact_btn(chat_key, mode)

        # --- CẬP NHẬT UI ---
        if self.current_target == chat_key:
            # Truyền tham số is_file và filename vào render_bubble
            self.render_bubble(sender, content, sender == self.my_name, False, 
                               is_file=is_file_msg, filename=filename)
            
            self.after(50, self.scroll_to_bottom) 
            
        elif not is_history:
            if chat_key in self.contacts:
                self.contacts[chat_key].set_unread(True)

    # --- HÀM THÊM TIN NHẮN HỆ THỐNG ---
    def add_system_message(self, target, text):
        """Thêm tin nhắn hệ thống vào đoạn chat (Thay vì Popup)"""
        if target not in self.messages: self.messages[target] = []
        self.messages[target].append({'sender': 'SYSTEM', 'content': text, 'is_sys': True})
        
        # Nếu đang mở đoạn chat đó thì hiện luôn
        if self.current_target == target:
            self.render_bubble("SYSTEM", text, False, True)
        else:
            # Nếu không thì báo đỏ để người dùng bấm vào xem
            if target in self.contacts: self.contacts[target].set_unread(True)

    def add_contact_btn(self, name, mode):
        # name ở đây là tên gốc (VD: "AI")
        if name in self.contacts: return
        
        # Tạo tên hiển thị (Thêm [N] nếu là nhóm)
        display_text = f"[N] {name}" if mode == "GROUP" else name
        
        # TRUYỀN CẢ 2 TÊN VÀO: name (gốc) và display_text (hiển thị)
        btn = ContactButton(self.scroll_contacts, name, display_text, mode, self.select_contact, self.show_context_menu)
        
        # Lưu vào dict bằng tên gốc
        self.contacts[name] = btn 
        
        if name not in self.messages: self.messages[name] = []

    def show_context_menu(self, event, name, type):
        # Tạo menu kiểu cổ điển của Tkinter (Vì CustomTkinter chưa hỗ trợ Menu tốt)
        menu = tk.Menu(self, tearoff=0)
        
        if type == "GROUP":
            menu.add_command(label="Rời nhóm", command=lambda: self.req_leave_group(name))
        else:
            menu.add_command(label="Hủy kết bạn", command=lambda: self.req_unfriend(name))
            
        # Hiển thị menu ngay tại vị trí con trỏ chuột
        menu.post(event.x_root, event.y_root)

    def req_leave_group(self, name):
        if messagebox.askyesno("Xác nhận", f"Rời nhóm {name}?"):
            self.client.send(self.pack(MSG_LEAVE_GROUP, self.my_name, "", name))

    def req_unfriend(self, name):
        if messagebox.askyesno("Xác nhận", f"Hủy kết bạn với {name}?"):
            self.client.send(self.pack(MSG_UNFRIEND, self.my_name, "", name))

    def req_members(self):
        if self.current_target:
            # Gửi yêu cầu Type 13 lên Server
            self.client.send(self.pack(MSG_REQ_MEMBER_LIST, self.my_name, "", self.current_target))
    
    #--- HÀM CHỌN ĐOẠN CHAT ---
    def select_contact(self, name, mode):
        if self.current_target and self.current_target in self.contacts:
            self.contacts[self.current_target].set_active_bg(False)
        self.current_target = name
        self.contacts[name].set_active_bg(True)
        self.contacts[name].set_unread(False)
        self.header_chat.configure(text=f"Đang chat với: {name}")

        # Hiện/Ẩn nút thành viên
        if mode == "GROUP":
            self.btn_members.pack(side="right", padx=10, pady=5)
        else:
            self.btn_members.pack_forget() 
        
        # ==================================================================
        # 🔴 BƯỚC 1: RESET THANH CUỘN VỀ ĐẦU NGAY LẬP TỨC
        # Để tránh việc Camera nhìn vào vùng đen phía dưới
        self.scroll_chat._parent_canvas.yview_moveto(0.0)
        # ==================================================================

        # Xóa tin nhắn cũ
        for w in self.scroll_chat.winfo_children(): w.destroy()
        
        # Hiển thị tin nhắn từ RAM (Chỉ 20 tin cuối)
        if name in self.messages:
            all_msgs = self.messages[name]
            total = len(all_msgs)
            
            # Chỉ lấy 20 tin cuối cùng
            start_index = max(0, total - self.BATCH_SIZE)
            msgs_to_show = all_msgs[start_index:] 
            
            # Lưu lại trạng thái là mình đang load từ index nào
            self.loaded_start_index = start_index 

            # Nếu vẫn còn tin cũ hơn (start_index > 0), hiện nút "Xem tin cũ"
            if start_index > 0:
                btn_load_more = ctk.CTkButton(self.scroll_chat, text="▲ Xem tin cũ hơn", 
                                              fg_color="#444", height=20,
                                              command=self.load_more_history)
                btn_load_more.pack(pady=5)

            # Vẽ các tin nhắn đã lọc
            for msg in msgs_to_show:
                self.render_bubble(
                    sender=msg['sender'], 
                    content=msg['content'], 
                    is_me=(msg['sender'] == self.my_name), 
                    is_sys=msg.get('is_sys', False),
                    is_file=msg.get('is_file', False),
                    filename=msg.get('filename', "")
                )

        self.scroll_chat.update_idletasks()
        self.after(50, self.scroll_to_bottom)
    
    #--- HÀM VẼ BONG BÓNG TIN NHẮN ---
    def render_bubble(self, sender, content, is_me, is_sys, is_file=False, filename=""):
        frame = ctk.CTkFrame(self.scroll_chat, fg_color="transparent")
        
        if is_sys:
            frame.pack(fill="x", pady=5)
            ctk.CTkLabel(frame, text=content, font=("Arial", 11, "italic"), text_color="gray").pack()
            return # Dừng luôn nếu là tin hệ thống

        # --- XỬ LÝ CHO PHÍA NGƯỜI GỬI (LÀ MÌNH) ---
        if is_me:
            frame.pack(fill="x", pady=5, anchor="e")
            
            if is_file:
                btn = ctk.CTkButton(frame, text=f"📁 {content}", 
                                    fg_color="#0066cc", hover_color="#0052a3", # Màu xanh đậm hơn
                                    width=150,
                                    state="normal", # Hoặc "disabled" nếu không muốn cho bấm
                                    # Nếu muốn bấm để tải lại file của chính mình (để test server)
                                    command=lambda: self.request_download(filename))
                btn.pack(side="right")
            else:
                # Tin nhắn văn bản thường
                ctk.CTkLabel(frame, text=content, fg_color="#0084ff", text_color="white", corner_radius=15, padx=10, pady=5).pack(side="right")

        # --- XỬ LÝ CHO PHÍA NGƯỜI NHẬN (LÀ HỌ) ---
        else:
            frame.pack(fill="x", pady=5, anchor="w")
            ctk.CTkLabel(frame, text=sender, font=("Arial", 9), text_color="gray").pack(anchor="w", padx=5)
            
            if is_file:
                # Nếu là file họ gửi -> Vẽ nút Tải về (Màu xanh lá)
                btn = ctk.CTkButton(frame, text=f"⬇ {content}", 
                                    fg_color="#2ecc71", hover_color="#27ae60",
                                    width=150,
                                    command=lambda: self.request_download(filename))
                btn.pack(side="left")
            else:
                # Tin nhắn văn bản thường
                ctk.CTkLabel(frame, text=content, fg_color="#333", text_color="white", corner_radius=15, padx=10, pady=5).pack(side="left")

    def req_friend(self):
        t = self.entry_add.get().strip()
        if t: 
            self.client.send(self.pack(MSG_FRIEND_REQ, self.my_name, "", t))
            messagebox.showinfo("Thông báo", f"Đã gửi lời mời tới {t}")
            self.entry_add.delete(0, "end")

    def create_group(self):
        t = self.entry_add.get().strip()
        if t:
            p = simpledialog.askstring("Mật khẩu", f"Đặt pass cho nhóm {t}:")
            if p: self.client.send(self.pack(MSG_CREATE_GROUP_REQ, self.my_name, "", t, p))
            self.entry_add.delete(0, "end")

    def join_group(self):
        t = self.entry_add.get().strip()
        if t:
            p = simpledialog.askstring("Mật khẩu", f"Nhập pass nhóm {t}:")
            if p: self.client.send(self.pack(MSG_JOIN_GROUP_REQ, self.my_name, "", t, p))
            self.entry_add.delete(0, "end")

    def send_msg(self, event=None):
        txt = self.entry_msg.get()
        if txt and self.current_target:
            mode = self.contacts[self.current_target].type
            type = MSG_PRIVATE_CHAT if mode == "PRIVATE" else MSG_GROUP_CHAT
            self.client.send(self.pack(type, self.my_name, "", self.current_target, "", txt))
            
            if self.current_target not in self.messages: self.messages[self.current_target] = []
            self.messages[self.current_target].append({'sender': self.my_name, 'content': txt, 'is_sys': False})
            self.render_bubble(self.my_name, txt, True, False)
            self.entry_msg.delete(0, "end")
            self.after(50, self.scroll_to_bottom)

    def scroll_to_bottom(self):
        """Hàm cuộn xuống dưới cùng khung chat"""
        try:
            # Bắt buộc tính toán lại layout trước khi cuộn
            self.scroll_chat.update_idletasks() 
            
            # Cuộn xuống đáy (1.0)
            self.scroll_chat._parent_canvas.yview_moveto(1.0)
        except Exception as e:
            print(f"Lỗi cuộn: {e}")

    def load_more_history(self):
        """Hàm xử lý khi bấm nút 'Xem tin cũ hơn'"""
        if not self.current_target or self.current_target not in self.messages: return
        
        # 1. Tính toán vị trí tin nhắn cần lấy
        current_start = self.loaded_start_index
        new_start = max(0, current_start - self.BATCH_SIZE)
        
        if new_start == current_start: return # Hết tin để load rồi

        # 2. Lưu lại số lượng tin nhắn TRƯỚC khi load thêm (để tính tỷ lệ)
        # Ví dụ: đang hiện 20 tin
        msgs_before = len(self.messages[self.current_target]) - current_start
        
        # Cập nhật index mới
        self.loaded_start_index = new_start
        
        # 3. Vẽ lại giao diện (Lúc này màn hình sẽ bị nhảy lung tung)
        self.reload_ui_range(new_start)
        
        # 4. TÍNH TOÁN VỊ TRÍ CUỘN ĐỂ GIỮ NGUYÊN TẦM NHÌN (QUAN TRỌNG)
        # Số lượng tin sau khi load (Ví dụ: 40 tin)
        msgs_after = len(self.messages[self.current_target]) - new_start
        
        # Số tin vừa được thêm vào (Ví dụ: 20 tin)
        added_msgs = msgs_after - msgs_before
        
        # Tính tỷ lệ phần trăm chiều cao mà đám tin mới chiếm giữ
        # Ví dụ: thêm 20 tin trong tổng 40 tin -> Chiếm 0.5 (50%)
        # Ta muốn thanh cuộn nhảy đến ngay sau đám tin mới này -> Tức là vị trí 0.5
        scroll_ratio = added_msgs / msgs_after
        
        # Nếu có nút "Xem tin cũ" ở trên cùng, nó chiếm 1 ít diện tích, 
        # ta trừ nhẹ đi 1 xíu (khoảng 0.02) để nhìn thấy được 1 phần tin nhắn cũ vừa load
        final_pos = max(0.0, scroll_ratio - 0.05) 

        # 5. Thực hiện cuộn
        self.scroll_chat.update_idletasks() # Bắt buộc tính toán xong giao diện mới cuộn
        self.scroll_chat._parent_canvas.yview_moveto(final_pos)
        
    # --- HÀM VẼ LẠI GIAO DIỆN TỪ VỊ TRÍ CHỈ ĐỊNH ---
    def reload_ui_range(self, start_idx):
        """Hàm vẽ lại giao diện từ vị trí start_idx đến cuối"""
        # Xóa sạch
        for w in self.scroll_chat.winfo_children(): w.destroy()
        
        all_msgs = self.messages[self.current_target]
        msgs_to_show = all_msgs[start_idx:]
        
        # Vẽ nút load more nếu cần
        if start_idx > 0:
            ctk.CTkButton(self.scroll_chat, text="▲ Xem tin cũ hơn", 
                          fg_color="#444", height=20,
                          command=self.load_more_history).pack(pady=5)
            
        for msg in msgs_to_show:
             self.render_bubble(
                sender=msg['sender'], 
                content=msg['content'], 
                is_me=(msg['sender'] == self.my_name), 
                is_sys=msg.get('is_sys', False),
                is_file=msg.get('is_file', False),
                filename=msg.get('filename', "")
            )
        
        # Quan trọng: Khi load tin cũ, không được scroll xuống đáy nữa
        # Mà nên giữ vị trí scroll ở trên cùng (để thấy tin vừa load)
        self.scroll_chat._parent_canvas.yview_moveto(0.0)

    # --- HÀM YÊU CẦU TẢI FILE MỚI ---
    def request_download(self, filename):
        # 1. Hỏi người dùng muốn lưu vào đâu
        save_path = filedialog.asksaveasfilename(initialfile=filename, title="Lưu file")
        
        if save_path:
            self.downloading_path = save_path
            
            # Mở file sẵn để chờ ghi dữ liệu
            try:
                self.downloading_file = open(save_path, "wb")
                
                # 2. Gửi yêu cầu lên Server (Type 22)
                self.client.send(self.pack(MSG_FILE_DOWNLOAD_REQ, self.my_name, "", "", "", filename))
                
                messagebox.showinfo("Bắt đầu tải", f"Đang tải {filename}...")
            except Exception as e:
                messagebox.showerror("Lỗi", f"Không thể tạo file: {e}")

    def req_game(self):
        """Gửi lời mời chơi game"""
        if not self.current_target: return
        # Chỉ cho chơi Private
        if self.contacts[self.current_target].type == "GROUP":
            messagebox.showwarning("Lỗi", "Chỉ chơi Caro 1 vs 1 thôi!")
            return

        self.client.send(self.pack(MSG_GAME_REQ, self.my_name, "", self.current_target))
        messagebox.showinfo("Game", "Đã gửi lời mời, đợi họ đồng ý nhé!")

    def send_game_move(self, row, col, is_win):
        """Callback khi mình đánh 1 nước"""
        # Gửi tọa độ dạng "row,col"
        content = f"{row},{col}"
        # Nếu thắng thì gửi cờ báo hiệu (Hack nhẹ: dùng trường password để gửi cờ thắng)
        flags = "WIN" if is_win else ""
        self.client.send(self.pack(MSG_GAME_MOVE, self.my_name, flags, self.current_target, "", content))

    def start_game(self, opponent, my_turn, symbol):
        """Mở cửa sổ bàn cờ"""
        if self.game_window: 
            self.game_window.destroy()

        self.game_window = CaroBoard(self, opponent, my_turn, symbol, self.send_game_move)

if __name__ == "__main__":
    app = ChatClient()
    app.mainloop()
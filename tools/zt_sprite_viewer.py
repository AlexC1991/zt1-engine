import struct
import zipfile
import tkinter as tk
from tkinter import filedialog, messagebox, Scrollbar, ttk
from PIL import Image, ImageTk
import os

class ZTSpriteViewer:
    def __init__(self, root):
        self.root = root
        self.root.title("Zoo Tycoon Sprite Viewer")
        self.root.geometry("1300x900")
        
        self.zf = None
        self.file_list = []
        self.palette = None
        self.palette_name = "None"
        self.current_frames = []
        self.current_frame_idx = 0
        self.animation_running = False
        self.animation_speed = 100
        self.zoom = 4
        self.current_filename = None
        
        self.setup_ui()
    
    def setup_ui(self):
        # --- LEFT PANEL ---
        frame_left = tk.Frame(self.root, width=350, bg="#1a1a2e")
        frame_left.pack(side=tk.LEFT, fill=tk.Y)
        frame_left.pack_propagate(False)
        
        btn_frame = tk.Frame(frame_left, bg="#1a1a2e")
        btn_frame.pack(fill=tk.X, padx=5, pady=5)
        
        tk.Button(btn_frame, text="📁 Open ZTD", command=self.open_ztd,
                  bg="#e94560", fg="white", font=("Arial", 10, "bold")).pack(side=tk.LEFT, padx=2)
        tk.Button(btn_frame, text="📂 Folder", command=self.open_folder,
                  bg="#0f3460", fg="white", font=("Arial", 10, "bold")).pack(side=tk.LEFT, padx=2)
        tk.Button(btn_frame, text="🎨 Palette", command=self.load_palette_file,
                  bg="#16213e", fg="white", font=("Arial", 10, "bold")).pack(side=tk.LEFT, padx=2)
        
        tk.Label(frame_left, text="Filter:", bg="#1a1a2e", fg="white").pack(anchor=tk.W, padx=5)
        self.ent_filter = tk.Entry(frame_left, bg="#16213e", fg="white", insertbackground="white")
        self.ent_filter.pack(fill=tk.X, padx=5, pady=2)
        self.ent_filter.bind("<KeyRelease>", self.apply_filter)
        
        list_frame = tk.Frame(frame_left, bg="#1a1a2e")
        list_frame.pack(fill=tk.BOTH, expand=True, padx=5, pady=5)
        
        sb = Scrollbar(list_frame)
        sb.pack(side=tk.RIGHT, fill=tk.Y)
        self.listbox = tk.Listbox(list_frame, bg="#16213e", fg="#eaeaea", 
                                   font=("Consolas", 9), selectbackground="#e94560",
                                   yscrollcommand=sb.set)
        self.listbox.pack(fill=tk.BOTH, expand=True)
        sb.config(command=self.listbox.yview)
        self.listbox.bind('<<ListboxSelect>>', self.on_select)
        
        self.lbl_palette = tk.Label(frame_left, text="Palette: None", 
                                     bg="#1a1a2e", fg="#ffc107", font=("Arial", 9))
        self.lbl_palette.pack(anchor=tk.W, padx=5, pady=5)
        
        # --- RIGHT PANEL ---
        frame_right = tk.Frame(self.root, bg="#0f0f0f")
        frame_right.pack(side=tk.RIGHT, fill=tk.BOTH, expand=True)
        
        # Controls
        ctrl_frame = tk.Frame(frame_right, bg="#1a1a2e", pady=8)
        ctrl_frame.pack(fill=tk.X)
        
        tk.Label(ctrl_frame, text="Zoom:", bg="#1a1a2e", fg="white").pack(side=tk.LEFT, padx=5)
        for z in [1, 2, 4, 6, 8]:
            tk.Button(ctrl_frame, text=f"{z}x", width=3, command=lambda z=z: self.set_zoom(z),
                      bg="#0f3460", fg="white").pack(side=tk.LEFT, padx=1)
        
        ttk.Separator(ctrl_frame, orient=tk.VERTICAL).pack(side=tk.LEFT, fill=tk.Y, padx=10)
        
        tk.Label(ctrl_frame, text="Animation:", bg="#1a1a2e", fg="white").pack(side=tk.LEFT, padx=5)
        
        self.btn_play = tk.Button(ctrl_frame, text="▶ Play", command=self.toggle_animation,
                                   bg="#27ae60", fg="white", font=("Arial", 10, "bold"), width=8)
        self.btn_play.pack(side=tk.LEFT, padx=2)
        
        tk.Button(ctrl_frame, text="⏮", command=self.prev_frame, bg="#555", fg="white", width=3).pack(side=tk.LEFT, padx=1)
        tk.Button(ctrl_frame, text="⏭", command=self.next_frame, bg="#555", fg="white", width=3).pack(side=tk.LEFT, padx=1)
        
        tk.Label(ctrl_frame, text="Speed:", bg="#1a1a2e", fg="white").pack(side=tk.LEFT, padx=5)
        self.speed_var = tk.StringVar(value="100")
        speed_combo = ttk.Combobox(ctrl_frame, textvariable=self.speed_var, 
                                    values=["50", "75", "100", "150", "200", "300"], width=5)
        speed_combo.pack(side=tk.LEFT, padx=2)
        speed_combo.bind("<<ComboboxSelected>>", self.on_speed_change)
        tk.Label(ctrl_frame, text="ms", bg="#1a1a2e", fg="white").pack(side=tk.LEFT)
        
        ttk.Separator(ctrl_frame, orient=tk.VERTICAL).pack(side=tk.LEFT, fill=tk.Y, padx=10)
        
        tk.Button(ctrl_frame, text="💾 Export PNG", command=self.export_png,
                  bg="#9b59b6", fg="white", font=("Arial", 9)).pack(side=tk.LEFT, padx=2)
        tk.Button(ctrl_frame, text="💾 Export GIF", command=self.export_gif,
                  bg="#8e44ad", fg="white", font=("Arial", 9)).pack(side=tk.LEFT, padx=2)
        tk.Button(ctrl_frame, text="💾 Export All", command=self.export_all,
                  bg="#16a085", fg="white", font=("Arial", 9)).pack(side=tk.LEFT, padx=2)
        
        # Info panel
        info_frame = tk.Frame(frame_right, bg="#16213e", pady=5)
        info_frame.pack(fill=tk.X, padx=10, pady=5)
        
        self.lbl_info = tk.Label(info_frame, text="Load a ZTD or folder to begin...",
                                  bg="#16213e", fg="#00ff88", font=("Consolas", 10),
                                  anchor=tk.W, justify=tk.LEFT)
        self.lbl_info.pack(fill=tk.X, padx=10)
        
        self.lbl_frame = tk.Label(info_frame, text="Frame: -/-",
                                   bg="#16213e", fg="#ffcc00", font=("Consolas", 11, "bold"))
        self.lbl_frame.pack(side=tk.RIGHT, padx=10)
        
        # Canvas
        canvas_frame = tk.Frame(frame_right, bg="#0f0f0f")
        canvas_frame.pack(fill=tk.BOTH, expand=True, padx=10, pady=10)
        
        self.canvas = tk.Canvas(canvas_frame, bg="#282828", highlightthickness=1,
                                 highlightbackground="#444")
        self.canvas.pack(fill=tk.BOTH, expand=True)
        
        # Status bar
        self.status_var = tk.StringVar(value="Ready")
        status_bar = tk.Label(frame_right, textvariable=self.status_var,
                              bg="#1a1a2e", fg="#888", anchor=tk.W, padx=10)
        status_bar.pack(fill=tk.X)
    
    # === FILE OPERATIONS ===
    
    def open_ztd(self):
        path = filedialog.askopenfilename(title="Open ZTD File",
            filetypes=[("ZTD Files", "*.ztd *.ZTD"), ("ZIP Files", "*.zip"), ("All", "*.*")])
        if not path: return
        try:
            self.zf = zipfile.ZipFile(path, 'r')
            self.file_list = sorted(self.zf.namelist())
            self.current_folder = None
            self.update_list(self.file_list)
            self.auto_load_palette()
            self.status_var.set(f"Opened: {os.path.basename(path)} ({len(self.file_list)} files)")
        except Exception as e:
            messagebox.showerror("Error", str(e))
    
    def open_folder(self):
        path = filedialog.askdirectory(title="Select Sprite Folder")
        if not path: return
        try:
            self.zf = None
            self.current_folder = path
            self.file_list = [f for f in sorted(os.listdir(path)) if os.path.isfile(os.path.join(path, f))]
            self.update_list(self.file_list)
            self.auto_load_palette()
            self.status_var.set(f"Opened: {path} ({len(self.file_list)} files)")
        except Exception as e:
            messagebox.showerror("Error", str(e))
    
    def load_palette_file(self):
        path = filedialog.askopenfilename(title="Select Palette",
            filetypes=[("Palette Files", "*.pal *.PAL"), ("All", "*.*")])
        if not path: return
        try:
            with open(path, 'rb') as f:
                self.palette = self.parse_palette(f.read())
            self.palette_name = os.path.basename(path)
            self.lbl_palette.config(text=f"Palette: {self.palette_name}", fg="#00ff88")
            self.refresh_current()
        except Exception as e:
            messagebox.showerror("Error", str(e))
    
    def parse_palette(self, data):
        palette = []
        if len(data) >= 1028:
            count = struct.unpack_from('<I', data, 0)[0]
            for i in range(min(count, 256)):
                offset = 4 + i * 4
                if offset + 4 <= len(data):
                    r, g, b, a = data[offset:offset+4]
                    palette.append((r, g, b))
        elif len(data) >= 768:
            for i in range(256):
                offset = i * 3
                if offset + 3 <= len(data):
                    palette.append(tuple(data[offset:offset+3]))
        while len(palette) < 256:
            palette.append((0, 0, 0))
        return palette
    
    def auto_load_palette(self):
        pal_files = [f for f in self.file_list if f.lower().endswith('.pal')]
        if pal_files:
            try:
                data = self.read_file(pal_files[0])
                self.palette = self.parse_palette(data)
                self.palette_name = pal_files[0]
                self.lbl_palette.config(text=f"Palette: {pal_files[0]}", fg="#00ff88")
                return
            except: pass
        self.create_fallback_palette()
    
    def create_fallback_palette(self):
        self.palette = []
        for i in range(256):
            if i == 0: self.palette.append((255, 0, 255))
            elif i < 64: self.palette.append((40+i, 30+i//2, 20))
            elif i < 128: self.palette.append((80+(i-64), 50+(i-64)//2, 30))
            elif i < 192: self.palette.append((140+(i-128), 100+(i-128)//2, 60))
            else: self.palette.append((min(255, 200+(i-192)), min(255, 150+(i-192)), min(255, 100)))
        self.palette_name = "Fallback"
        self.lbl_palette.config(text="Palette: Fallback", fg="#ffc107")
    
    def read_file(self, filename):
        if self.zf:
            return self.zf.read(filename)
        return open(os.path.join(self.current_folder, filename), 'rb').read()
    
    def apply_filter(self, event=None):
        q = self.ent_filter.get().lower()
        self.update_list([f for f in self.file_list if q in f.lower()])
    
    def update_list(self, items):
        self.listbox.delete(0, tk.END)
        for f in items:
            self.listbox.insert(tk.END, f)
    
    def on_select(self, event):
        sel = self.listbox.curselection()
        if sel:
            self.stop_animation()
            self.load_sprite(self.listbox.get(sel[0]))
    
    # === SPRITE DECODING ===
    
    def decode_frame(self, data, start_ptr, width, height):
        img = Image.new('RGBA', (width, height), (0, 0, 0, 0))
        pixels = img.load()
        ptr = start_ptr
        
        for y in range(height):
            if ptr >= len(data): break
            cmd_count = data[ptr]
            ptr += 1
            if cmd_count >= 0xF0: continue
            
            x = 0
            for _ in range(cmd_count):
                if ptr + 1 >= len(data): break
                skip, run = data[ptr], data[ptr + 1]
                ptr += 2
                x += skip
                for _ in range(run):
                    if ptr >= len(data): break
                    idx = data[ptr]
                    if x < width and idx < 256:
                        r, g, b = self.palette[idx]
                        pixels[x, y] = (0, 0, 0, 0) if idx == 0 else (r, g, b, 255)
                    x += 1
                    ptr += 1
        return img
    
    def parse_main_header(self, data):
        """Parse the main file header. Returns (frame_header_start, base_width) or None."""
        if len(data) < 20:
            return None
        
        try:
            # @0: height_header (u32)
            # @4: str_len (u32)
            # @8: palette string (str_len bytes)
            # @8+str_len: width (u32)
            # @8+str_len+4: first frame header starts
            
            str_len = struct.unpack_from('<I', data, 4)[0]
            
            if str_len == 0 or str_len > 200:
                return None
            
            width_pos = 8 + str_len
            if width_pos + 4 > len(data):
                return None
            
            base_width = struct.unpack_from('<I', data, width_pos)[0]
            
            if base_width == 0 or base_width > 500:
                return None
            
            frame_header_start = width_pos + 4
            
            return (frame_header_start, base_width)
        except:
            return None
    
    def find_frame_headers(self, data):
        """Find all animation frame headers in sprite data"""
        
        # First, parse main header to find where frames start
        header_info = self.parse_main_header(data)
        if not header_info:
            return []
        
        frame_start, base_width = header_info
        
        frames = []
        pos = frame_start
        
        while pos + 14 < len(data) and len(frames) < 100:
            # Frame header: [rle_size:u32][height:u16][width:u16][x_off:u16][y_off:u16][flags:u16]
            rle_size = struct.unpack_from('<I', data, pos)[0]
            h = struct.unpack_from('<H', data, pos + 4)[0]
            w = struct.unpack_from('<H', data, pos + 6)[0]
            x_off = struct.unpack_from('<H', data, pos + 8)[0]
            y_off = struct.unpack_from('<H', data, pos + 10)[0]
            
            # Validate header - be more lenient
            if 50 < rle_size < 10000 and 5 < h < 300 and 5 < w < 300:
                frames.append({
                    'header_pos': pos,
                    'rle_pos': pos + 14,
                    'rle_size': rle_size,
                    'width': w,
                    'height': h,
                    'x_off': x_off,
                    'y_off': y_off
                })
                
                # Find next header by searching after this frame's data
                next_search_start = pos + 14 + rle_size - 30
                if next_search_start < pos + 14:
                    next_search_start = pos + 14
                
                found = False
                for search in range(next_search_start, min(next_search_start + 60, len(data) - 14)):
                    rs = struct.unpack_from('<I', data, search)[0]
                    hh = struct.unpack_from('<H', data, search + 4)[0]
                    ww = struct.unpack_from('<H', data, search + 6)[0]
                    if 50 < rs < 10000 and 5 < hh < 300 and 5 < ww < 300:
                        # Additional validation: dimensions should be similar to first frame
                        if abs(ww - frames[0]['width']) < 20 and abs(hh - frames[0]['height']) < 20:
                            pos = search
                            found = True
                            break
                if not found:
                    break
            else:
                break
        
        return frames
    
    def load_sprite(self, filename):
        """Load sprite file and decode all animation frames"""
        try:
            if filename.endswith('/') or filename.lower().endswith(('.pal', '.ani', '.txt', '.cfg')):
                self.lbl_info.config(text=f"Skipped: {filename}")
                return
            
            data = self.read_file(filename)
            if len(data) < 50:
                self.lbl_info.config(text=f"File too small: {len(data)} bytes")
                return
            
            self.current_filename = filename
            
            if not self.palette:
                self.create_fallback_palette()
            
            # Find all frames
            frame_headers = self.find_frame_headers(data)
            
            if not frame_headers:
                self.lbl_info.config(text=f"No valid frames found in {filename}")
                return
            
            # Decode all frames
            self.current_frames = []
            max_h = max(f['height'] for f in frame_headers)
            
            for hdr in frame_headers:
                img = self.decode_frame(data, hdr['rle_pos'], hdr['width'], hdr['height'])
                self.current_frames.append({
                    'image': img,
                    'width': hdr['width'],
                    'height': hdr['height'],
                    'x_off': hdr['x_off'],
                    'y_off': hdr['y_off'],
                    'max_h': max_h
                })
            
            self.current_frame_idx = 0
            self.display_current_frame()
            
            info = f"File: {filename}\n"
            info += f"Frames: {len(self.current_frames)} | "
            info += f"Size: {frame_headers[0]['width']}x{frame_headers[0]['height']}"
            self.lbl_info.config(text=info)
            self.status_var.set(f"Loaded {len(self.current_frames)} frames from {filename}")
            
        except Exception as e:
            self.lbl_info.config(text=f"Error: {e}")
            import traceback
            traceback.print_exc()
    
    def display_current_frame(self):
        """Display the current animation frame"""
        if not self.current_frames:
            return
        
        frame = self.current_frames[self.current_frame_idx]
        img = frame['image']
        
        scaled = img.resize((img.width * self.zoom, img.height * self.zoom), Image.NEAREST)
        
        self.tk_img = ImageTk.PhotoImage(scaled)
        
        self.canvas.delete("all")
        cx = self.canvas.winfo_width() // 2
        cy = self.canvas.winfo_height() // 2
        self.canvas.create_image(cx, cy, image=self.tk_img, anchor=tk.CENTER)
        
        self.lbl_frame.config(text=f"Frame: {self.current_frame_idx + 1}/{len(self.current_frames)}")
    
    # === ANIMATION CONTROLS ===
    
    def toggle_animation(self):
        if self.animation_running:
            self.stop_animation()
        else:
            self.start_animation()
    
    def start_animation(self):
        if not self.current_frames:
            return
        self.animation_running = True
        self.btn_play.config(text="⏹ Stop", bg="#e74c3c")
        self.animate()
    
    def stop_animation(self):
        self.animation_running = False
        self.btn_play.config(text="▶ Play", bg="#27ae60")
    
    def animate(self):
        if not self.animation_running:
            return
        
        self.current_frame_idx = (self.current_frame_idx + 1) % len(self.current_frames)
        self.display_current_frame()
        
        self.root.after(self.animation_speed, self.animate)
    
    def next_frame(self):
        if self.current_frames:
            self.current_frame_idx = (self.current_frame_idx + 1) % len(self.current_frames)
            self.display_current_frame()
    
    def prev_frame(self):
        if self.current_frames:
            self.current_frame_idx = (self.current_frame_idx - 1) % len(self.current_frames)
            self.display_current_frame()
    
    def on_speed_change(self, event=None):
        self.animation_speed = int(self.speed_var.get())
    
    def set_zoom(self, z):
        self.zoom = z
        self.display_current_frame()
    
    def refresh_current(self):
        sel = self.listbox.curselection()
        if sel:
            self.load_sprite(self.listbox.get(sel[0]))
    
    # === EXPORT ===
    
    def export_png(self):
        if not self.current_frames:
            return
        
        path = filedialog.asksaveasfilename(defaultextension=".png",
            filetypes=[("PNG", "*.png")],
            initialfile=f"{os.path.basename(self.current_filename or 'sprite')}_strip.png")
        if not path:
            return
        
        max_w = max(f['width'] for f in self.current_frames)
        max_h = max(f['height'] for f in self.current_frames)
        
        strip = Image.new('RGBA', (max_w * len(self.current_frames), max_h), (0, 0, 0, 0))
        x = 0
        for f in self.current_frames:
            y = max_h - f['height']
            strip.paste(f['image'], (x, y), f['image'])
            x += max_w
        
        strip = strip.resize((strip.width * self.zoom, strip.height * self.zoom), Image.NEAREST)
        strip.save(path)
        self.status_var.set(f"Exported: {path}")
    
    def export_gif(self):
        if not self.current_frames:
            return
        
        path = filedialog.asksaveasfilename(defaultextension=".gif",
            filetypes=[("GIF", "*.gif")],
            initialfile=f"{os.path.basename(self.current_filename or 'sprite')}.gif")
        if not path:
            return
        
        max_w = max(f['width'] for f in self.current_frames)
        max_h = max(f['height'] for f in self.current_frames)
        
        gif_frames = []
        for f in self.current_frames:
            frame_img = Image.new('RGBA', (max_w, max_h), (0, 0, 0, 0))
            y = max_h - f['height']
            frame_img.paste(f['image'], (0, y), f['image'])
            frame_img = frame_img.resize((max_w * self.zoom, max_h * self.zoom), Image.NEAREST)
            gif_frames.append(frame_img)
        
        gif_frames[0].save(path, save_all=True, append_images=gif_frames[1:],
                          duration=self.animation_speed, loop=0, disposal=2)
        self.status_var.set(f"Exported GIF: {path}")
    
    def export_all(self):
        if not self.file_list:
            return
        
        folder = filedialog.askdirectory(title="Select Output Folder")
        if not folder:
            return
        
        exported = 0
        for filename in self.file_list:
            if filename.lower().endswith(('.pal', '.ani', '.txt', '.cfg', '/')):
                continue
            try:
                data = self.read_file(filename)
                headers = self.find_frame_headers(data)
                if headers:
                    img = self.decode_frame(data, headers[0]['rle_pos'], 
                                          headers[0]['width'], headers[0]['height'])
                    safe_name = filename.replace('/', '_').replace('\\', '_')
                    img.save(os.path.join(folder, f"{safe_name}.png"))
                    exported += 1
            except:
                pass
        
        self.status_var.set(f"Exported {exported} sprites to {folder}")
        messagebox.showinfo("Done", f"Exported {exported} sprites")


if __name__ == "__main__":
    root = tk.Tk()
    app = ZTSpriteViewer(root)
    root.mainloop()

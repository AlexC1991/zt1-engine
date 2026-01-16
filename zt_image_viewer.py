import struct
import zipfile
import tkinter as tk
from tkinter import filedialog, messagebox, Scrollbar, ttk
from PIL import Image, ImageTk
import os

class ZTMapViewer:
    def __init__(self, root):
        self.root = root
        self.root.title("Zoo Tycoon Map Viewer")
        self.root.geometry("1300x900")

        self.zf = None
        self.file_list = []
        self.palette = None
        self.palette_name = "None"
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

        tk.Button(ctrl_frame, text="💾 Export PNG", command=self.export_png,
                  bg="#9b59b6", fg="white", font=("Arial", 9)).pack(side=tk.LEFT, padx=2)

        # Info panel
        info_frame = tk.Frame(frame_right, bg="#16213e", pady=5)
        info_frame.pack(fill=tk.X, padx=10, pady=5)

        self.lbl_info = tk.Label(info_frame, text="Load a ZTD or folder to begin...",
                                  bg="#16213e", fg="#00ff88", font=("Consolas", 10),
                                  anchor=tk.W, justify=tk.LEFT)
        self.lbl_info.pack(fill=tk.X, padx=10)

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
            self.update_list(self.file_list)
            self.auto_load_palette()
            self.status_var.set(f"Opened: {os.path.basename(path)} ({len(self.file_list)} files)")
        except Exception as e:
            messagebox.showerror("Error", str(e))

    def open_folder(self):
        path = filedialog.askdirectory(title="Select Folder")
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

    def read_file(self, filename):
        if self.zf:
            return self.zf.read(filename)
        return open(os.path.join(self.current_folder, filename), 'rb').read()

    # === PALETTE HANDLING ===

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
        for i in range(256):
            offset = i * 3
            if offset + 3 <= len(data):
                palette.append(tuple(data[offset:offset + 3]))
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
            except:
                self.create_fallback_palette()
        else:
            self.create_fallback_palette()

    def create_fallback_palette(self):
        self.palette = [(i, i, i) for i in range(256)]
        self.palette_name = "Fallback"
        self.lbl_palette.config(text="Palette: Fallback", fg="#ffc107")

    # === LISTBOX / FILTER ===

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
            self.load_map(self.listbox.get(sel[0]))

    # === MAP IMAGE HANDLING ===

    def load_map(self, filename):
        try:
            if filename.lower().endswith(('.pal', '.txt', '.cfg', '/')):
                self.lbl_info.config(text=f"Skipped: {filename}")
                return

            data = self.read_file(filename)
            self.current_filename = filename

            if not self.palette:
                self.create_fallback_palette()

            # Assuming square map for simplicity (adjust if needed)
            size = int(len(data) ** 0.5)
            img = Image.new('RGB', (size, size))
            pixels = img.load()

            for y in range(size):
                for x in range(size):
                    idx = data[y*size + x]
                    if idx < len(self.palette):
                        pixels[x, y] = self.palette[idx]

            self.current_image = img
            self.display_image()
            self.lbl_info.config(text=f"File: {filename} | Size: {size}x{size}")
            self.status_var.set(f"Loaded map: {filename}")

        except Exception as e:
            self.lbl_info.config(text=f"Error: {e}")
            import traceback
            traceback.print_exc()

    def display_image(self):
        if not hasattr(self, 'current_image'):
            return
        scaled = self.current_image.resize((self.current_image.width*self.zoom, self.current_image.height*self.zoom), Image.NEAREST)
        self.tk_img = ImageTk.PhotoImage(scaled)
        self.canvas.delete("all")
        cx = self.canvas.winfo_width() // 2
        cy = self.canvas.winfo_height() // 2
        self.canvas.create_image(cx, cy, image=self.tk_img, anchor=tk.CENTER)

    def set_zoom(self, z):
        self.zoom = z
        self.display_image()

    def refresh_current(self):
        sel = self.listbox.curselection()
        if sel:
            self.load_map(self.listbox.get(sel[0]))

    # === EXPORT ===

    def export_png(self):
        if not hasattr(self, 'current_image'):
            return

        path = filedialog.asksaveasfilename(defaultextension=".png",
                                            filetypes=[("PNG", "*.png")],
                                            initialfile=f"{os.path.basename(self.current_filename or 'map')}.png")
        if not path:
            return
        img = self.current_image.resize((self.current_image.width*self.zoom, self.current_image.height*self.zoom), Image.NEAREST)
        img.save(path)
        self.status_var.set(f"Exported: {path}")

if __name__ == "__main__":
    root = tk.Tk()
    app = ZTMapViewer(root)
    root.mainloop()

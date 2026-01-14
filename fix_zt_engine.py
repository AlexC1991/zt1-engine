import os
import shutil
import tkinter as tk
from tkinter import filedialog, messagebox

# --- THE CORRECTED C++ FUNCTION ---
NEW_FUNCTION_CODE = """AnimationData * AniFile::loadAnimationData(PalletManager * pallet_manager, const std::string &ztd_file, const std::string &directory) {
    SDL_RWops * rw = ZtdFile::getFile(ztd_file, directory);
    if (rw == NULL) return NULL;

    Sint64 file_size = SDL_RWsize(rw);
    AnimationData * animation_data = new AnimationData;
    animation_data->frame_count = 0;
    animation_data->frames = nullptr;

    // --- HEADER PARSING ---
    // The file starts directly with Height (4 bytes), then String Len (4 bytes)
    uint32_t total_height = SDL_ReadLE32(rw);
    uint32_t str_len = SDL_ReadLE32(rw);

    // Skip Palette String
    SDL_RWseek(rw, str_len, RW_SEEK_CUR);

    uint32_t total_width = SDL_ReadLE32(rw);
    
    animation_data->width = (uint16_t)total_width;
    animation_data->height = (uint16_t)total_height;

    // --- DYNAMIC FRAME LOADING ---
    std::vector<AnimationFrameData> temp_frames;

    while (SDL_RWtell(rw) < file_size) {
        AnimationFrameData frame;
        
        // 1. FRAME HEADER
        frame.size = SDL_ReadLE32(rw);
        
        // EOF / Garbage check
        if (frame.size == 0 || frame.size > 10000000) break;

        frame.height = SDL_ReadLE16(rw);
        frame.width = SDL_ReadLE16(rw);
        frame.offset_x = SDL_ReadLE16(rw);
        frame.offset_y = SDL_ReadLE16(rw);
        frame.mystery_bytes = SDL_ReadLE16(rw);
        frame.is_shadow = false;

        // 2. PIXEL DATA
        frame.lines = (AnimationLineData *) calloc(frame.height, sizeof(AnimationLineData));
        
        long frame_data_start = SDL_RWtell(rw);
        long frame_data_end = frame_data_start + frame.size;

        for(int y = 0; y < frame.height; y++) {
            if (SDL_RWtell(rw) >= frame_data_end) break;

            frame.lines[y].instruction_count = SDL_ReadU8(rw);
            
            if (frame.lines[y].instruction_count > 0) {
                frame.lines[y].instructions = (AnimationDrawInstruction *) calloc(frame.lines[y].instruction_count, sizeof(AnimationDrawInstruction));
                
                for(int x = 0; x < frame.lines[y].instruction_count; x++) {
                    frame.lines[y].instructions[x].offset = SDL_ReadU8(rw);
                    frame.lines[y].instructions[x].color_count = SDL_ReadU8(rw);
                    
                    if (frame.lines[y].instructions[x].color_count > 0) {
                        frame.lines[y].instructions[x].colors = (uint8_t *) calloc(frame.lines[y].instructions[x].color_count, sizeof(uint8_t));
                        SDL_RWread(rw, frame.lines[y].instructions[x].colors, sizeof(uint8_t), frame.lines[y].instructions[x].color_count);
                    }
                }
            }
        }

        temp_frames.push_back(frame);
        
        // Align to next frame
        SDL_RWseek(rw, frame_data_end, RW_SEEK_SET);
    }

    // --- CONVERT VECTOR TO ARRAY ---
    animation_data->frame_count = (uint32_t)temp_frames.size();
    if (animation_data->frame_count > 0) {
        animation_data->frames = (AnimationFrameData *) calloc(animation_data->frame_count, sizeof(AnimationFrameData));
        for (size_t i = 0; i < temp_frames.size(); i++) {
            animation_data->frames[i] = temp_frames[i];
        }
    }

    SDL_RWclose(rw);
    return animation_data;
}"""

def select_file():
    root = tk.Tk()
    root.withdraw() # Hide the main window
    file_path = filedialog.askopenfilename(
        title="Select 'AniFile.cpp' to Patch",
        filetypes=[("C++ Source", "AniFile.cpp"), ("All Files", "*.*")]
    )
    return file_path

def patch_file(file_path):
    if not file_path:
        print("No file selected. Exiting.")
        return

    if not os.path.exists(file_path):
        print(f"Error: Could not find {file_path}")
        return

    print(f"Reading {file_path}...")
    with open(file_path, 'r', encoding='utf-8') as f:
        lines = f.readlines()

    # 1. Locate the function start
    start_idx = -1
    for i, line in enumerate(lines):
        if "AnimationData * AniFile::loadAnimationData" in line:
            start_idx = i
            break
    
    if start_idx == -1:
        messagebox.showerror("Error", "Could not find 'loadAnimationData' function to replace.")
        return

    # 2. Locate the function end (matching braces)
    end_idx = -1
    brace_count = 0
    found_start_brace = False
    
    for i in range(start_idx, len(lines)):
        line = lines[i]
        brace_count += line.count('{')
        brace_count -= line.count('}')
        
        if '{' in line:
            found_start_brace = True
            
        if found_start_brace and brace_count == 0:
            end_idx = i
            break

    if end_idx == -1:
        messagebox.showerror("Error", "Could not find the end of the function.")
        return

    print(f"Function found lines {start_idx+1} to {end_idx+1}. Replacing...")

    # 3. Create Backup
    shutil.copy(file_path, file_path + ".bak")
    print(f"Backup created: {file_path}.bak")

    # 4. Construct new file content
    new_content = lines[:start_idx]
    new_content.append(NEW_FUNCTION_CODE + "\n")
    new_content.extend(lines[end_idx+1:])

    # 5. Write file
    with open(file_path, 'w', encoding='utf-8') as f:
        f.writelines(new_content)
    
    print("SUCCESS: File patched!")
    messagebox.showinfo("Success", "AniFile.cpp has been successfully patched!")

if __name__ == "__main__":
    target_file = select_file()
    patch_file(target_file)
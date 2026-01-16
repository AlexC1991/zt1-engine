import os

def apply(src_dir, root_dir):
    filename = "ZtdFile.cpp"
    filepath = os.path.join(src_dir, filename)
    
    if not os.path.exists(filepath): return False
    
    with open(filepath, "r", encoding="utf-8") as f: content = f.read()
    
    modified = False
    
    # ---------------------------------------------------------
    # FIX 1: Don't crash on missing file content (return nullptr)
    # ---------------------------------------------------------
    if 'SDL_Log("Could not load content' in content:
        lines = content.split('\n')
        new_lines = []
        i = 0
        while i < len(lines):
            new_lines.append(lines[i])
            if 'SDL_Log("Could not load content' in lines[i]:
                j = i + 1
                while j < len(lines) and lines[j].strip() == '':
                    new_lines.append(lines[j])
                    j += 1
                # If next line isn't a return, insert one
                if j < len(lines) and 'return' not in lines[j]:
                    new_lines.append('    return nullptr; // [PATCH] Don\'t crash on missing files')
                    modified = True
            i += 1
        if modified: content = '\n'.join(new_lines)

    # ---------------------------------------------------------
    # FIX 2: C2665 ERROR (IniReader Constructor)
    # ---------------------------------------------------------
    # The compiler hates 'new IniReader("", 0)'. It needs 'new IniReader((void*)"", 0)'.
    
    # Case A: Patch existing broken code
    if 'return new IniReader("", 0);' in content:
        content = content.replace(
            'return new IniReader("", 0);', 
            'return new IniReader((void*)"", 0);'
        )
        modified = True
        
    # Case B: Patch original code
    elif 'CRITICAL: Could not load ini' in content:
        content = content.replace(
            'SDL_Log("CRITICAL: Could not load ini',
            'SDL_Log("Warning: Could not load ini'
        )
        content = content.replace(
            'return nullptr;',
            'return new IniReader((void*)"", 0);', 
            1
        )
        modified = True

    if modified:
        with open(filepath, "w", encoding="utf-8") as f: f.write(content)
        print("    -> Updated ZtdFile.cpp (Fix C2665)")
        return True
    
    return False
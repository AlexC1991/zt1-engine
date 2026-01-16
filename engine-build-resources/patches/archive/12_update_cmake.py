import os

def apply(src_dir, root_dir):
    """Add ScenarioManager and UiListBox to CMakeLists.txt"""
    
    cmake_path = os.path.join(root_dir, "CMakeLists.txt")
    
    if not os.path.exists(cmake_path):
        print(f"    ! CMakeLists.txt not found")
        return False
    
    with open(cmake_path, "r", encoding="utf-8") as f:
        content = f.read()
    
    modified = False
    
    # Check if already has our files
    if "ScenarioManager" in content:
        return False
    
    # Find the source files list and add our new files
    # Look for patterns like:
    # src/ResourceManager.cpp
    # or add_executable with source list
    
    # Try to find where source files are listed
    if "src/ResourceManager.cpp" in content:
        # Add our files near ResourceManager
        old_line = "src/ResourceManager.cpp"
        new_lines = """src/ResourceManager.cpp
    src/ScenarioManager.cpp
    src/ui/UiListBox.cpp"""
        
        if old_line in content and "ScenarioManager.cpp" not in content:
            content = content.replace(old_line, new_lines)
            modified = True
            print("    -> Added ScenarioManager.cpp and UiListBox.cpp to CMakeLists.txt")
    
    # Alternative: look for set(SOURCES pattern
    elif "set(SOURCES" in content or "set(SRC" in content:
        # Find the closing paren of the source list
        lines = content.split('\n')
        new_lines = []
        found_sources = False
        
        for line in lines:
            new_lines.append(line)
            if ('set(SOURCES' in line or 'set(SRC' in line) and not found_sources:
                found_sources = True
            elif found_sources and ')' in line and 'ScenarioManager' not in content:
                # Add before the closing paren
                idx = len(new_lines) - 1
                new_lines.insert(idx, '    src/ScenarioManager.cpp')
                new_lines.insert(idx, '    src/ui/UiListBox.cpp')
                modified = True
                found_sources = False
                print("    -> Added new source files to CMakeLists.txt")
        
        if modified:
            content = '\n'.join(new_lines)
    
    if modified:
        with open(cmake_path, "w", encoding="utf-8") as f:
            f.write(content)
        return True
    else:
        print("    ! Could not find source file list in CMakeLists.txt")
        print("    ! You may need to manually add:")
        print("    !   src/ScenarioManager.cpp")
        print("    !   src/ui/UiListBox.cpp")
    
    return False

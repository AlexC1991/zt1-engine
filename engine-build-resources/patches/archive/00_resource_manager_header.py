import os

def apply(src_dir, root_dir):
    filename = "ResourceManager.hpp"
    filepath = os.path.join(src_dir, filename)
    
    if not os.path.exists(filepath):
        return False
    
    with open(filepath, "r", encoding="utf-8") as f:
        content = f.read()
    
    # 1. Check if already patched
    if "bool isDirectory" in content:
        return False
    
    # 2. Define the new declarations we need to add
    new_methods = """
  // [PATCH] New helper methods for handling missing files/directories
  std::string findActualResourceKey(const std::string &base_name);
  bool isDirectory(const std::string& path);
"""
    
    # 3. ROBUST STRATEGY: Insert before the final closing brace "};" 
    # This ignores formatting/spacing issues and just puts the code in the class.
    last_brace = content.rfind("};")
    if last_brace != -1:
        new_content = content[:last_brace] + new_methods + content[last_brace:]
        
        with open(filepath, "w", encoding="utf-8") as f:
            f.write(new_content)
        print("    -> Updated ResourceManager.hpp (Injected missing declarations)")
        return True
    
    print("    ! Error: Could not find class closing brace in ResourceManager.hpp")
    return False
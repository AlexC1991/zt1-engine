import os

def apply(src_dir, root_dir):
    """Add getId() method to UiElement for element lookup"""
    
    filepath = os.path.join(src_dir, "ui", "UiElement.hpp")
    
    if not os.path.exists(filepath):
        print("    ! UiElement.hpp not found")
        return False
    
    with open(filepath, "r", encoding="utf-8") as f:
        content = f.read()
    
    modified = False
    
    # Add getId() method if not present
    if "int getId()" not in content:
        old_line = "int getLayer() {return this->layer;};"
        new_line = """int getLayer() {return this->layer;};
  int getId() {return this->id;};  // [PATCH] Get element ID"""
        
        if old_line in content:
            content = content.replace(old_line, new_line)
            modified = True
            print("    -> Added getId() to UiElement.hpp")
    
    if modified:
        with open(filepath, "w", encoding="utf-8") as f:
            f.write(content)
        return True
    
    return False

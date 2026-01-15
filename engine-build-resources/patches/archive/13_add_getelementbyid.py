import os

def apply(src_dir, root_dir):
    """Add getElementById method to UiLayout to allow finding UI elements"""
    
    # Update UiLayout.hpp
    hpp_path = os.path.join(src_dir, "ui", "UiLayout.hpp")
    if not os.path.exists(hpp_path):
        print("    ! UiLayout.hpp not found")
        return False
    
    with open(hpp_path, "r", encoding="utf-8") as f:
        hpp_content = f.read()
    
    modified = False
    
    # Add getElementById declaration if not present
    if "getElementById" not in hpp_content:
        # Find the public section and add the method
        old_public = "UiAction handleInputs(std::vector<Input> &inputs);"
        new_public = """UiAction handleInputs(std::vector<Input> &inputs);
  UiElement* getElementById(int id);  // [PATCH] Find element by ID"""
        
        if old_public in hpp_content:
            hpp_content = hpp_content.replace(old_public, new_public)
            modified = True
    
    if modified:
        with open(hpp_path, "w", encoding="utf-8") as f:
            f.write(hpp_content)
        print("    -> Added getElementById to UiLayout.hpp")
    
    # Update UiLayout.cpp
    cpp_path = os.path.join(src_dir, "ui", "UiLayout.cpp")
    if not os.path.exists(cpp_path):
        print("    ! UiLayout.cpp not found")
        return modified
    
    with open(cpp_path, "r", encoding="utf-8") as f:
        cpp_content = f.read()
    
    # Add getElementById implementation if not present
    if "UiLayout::getElementById" not in cpp_content:
        # Add at the end of the file
        new_method = '''

// [PATCH] Find element by ID recursively
UiElement* UiLayout::getElementById(int targetId) {
  // First check direct children
  for (UiElement* child : this->children) {
    if (child->getId() == targetId) {
      return child;
    }
  }
  
  // Then check recursively (for nested layouts)
  for (UiElement* child : this->children) {
    // Try to cast to UiLayout and search its children
    UiLayout* childLayout = dynamic_cast<UiLayout*>(child);
    if (childLayout) {
      UiElement* found = childLayout->getElementById(targetId);
      if (found) return found;
    }
  }
  
  return nullptr;
}
'''
        cpp_content += new_method
        
        with open(cpp_path, "w", encoding="utf-8") as f:
            f.write(cpp_content)
        print("    -> Added getElementById to UiLayout.cpp")
        modified = True
    
    return modified

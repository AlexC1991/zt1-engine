import os
import re

def apply(src_dir, root_dir):
    """Add UiListBox support to UiLayout.cpp"""
    
    filepath = os.path.join(src_dir, "ui", "UiLayout.cpp")
    
    if not os.path.exists(filepath):
        print("    ! UiLayout.cpp not found at " + filepath)
        return False
    
    with open(filepath, "r", encoding="utf-8") as f:
        content = f.read()
    
    modified = False
    
    # 1. Add include for UiListBox
    if '#include "UiListBox.hpp"' not in content:
        # Add after existing includes
        old_include = '#include "UiButton.hpp"'
        new_include = '#include "UiButton.hpp"\n#include "UiListBox.hpp"'
        
        if old_include in content:
            content = content.replace(old_include, new_include)
            modified = True
            print("    -> Added UiListBox.hpp include")
    
    # 2. Add UIListBox type handling in process_sections
    if 'UIListBox' not in content:
        # Find the place where UIText is handled and add UIListBox after it
        old_text_handler = '''} else if (element_type == "UIText") {
      new_element = (UiElement *) new UiText(ini_reader, resource_manager, section);
    } else if (element_type == "UILayout") {'''
        
        new_text_handler = '''} else if (element_type == "UIText") {
      new_element = (UiElement *) new UiText(ini_reader, resource_manager, section);
    } else if (element_type == "UIListBox") {
      new_element = (UiElement *) new UiListBox(ini_reader, resource_manager, section);
    } else if (element_type == "UILayout") {'''
        
        if old_text_handler in content:
            content = content.replace(old_text_handler, new_text_handler)
            modified = True
            print("    -> Added UIListBox type handler")
        else:
            # Try alternate formatting - look for the pattern more flexibly
            if '"UIText"' in content and '"UIListBox"' not in content:
                # Insert after the UiText handler using regex
                pattern = r'(\} else if \(element_type == "UIText"\) \{\s*new_element = \(UiElement \*\) new UiText\([^)]+\);)'
                
                replacement = r'''\1
    } else if (element_type == "UIListBox") {
      new_element = (UiElement *) new UiListBox(ini_reader, resource_manager, section);'''
                
                new_content = re.sub(pattern, replacement, content)
                if new_content != content:
                    content = new_content
                    modified = True
                    print("    -> Added UIListBox type handler (regex method)")
    
    if modified:
        with open(filepath, "w", encoding="utf-8") as f:
            f.write(content)
        return True
    
    return False

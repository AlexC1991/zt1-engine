import os

def apply_hook():
    target_path = r"C:\Users\batty\OneDrive\Documents\GitHub\zt1-engine\src\Window.cpp"
    renderer_var = "this->renderer"
    
    print(f"Injecting Diagnostics into: {target_path}")
    print(f"Using Renderer: {renderer_var}")
    
    with open(target_path, 'r') as f:
        code = f.read()
        
    if "globalDiag" in code:
        print("Already patched.")
        return

    # 1. Add Include
    if '#include "DiagnosticUtilityData.hpp"' not in code:
        code = '#include "DiagnosticUtilityData.hpp"\n' + code
        
    # 2. Add Init (Try to find 'SDL_CreateWindow' or Start of file)
    # We will just use a static init trick right before the draw call to be safe
    # It's less efficient but guarantees it works without finding 'main()'
    
    # 3. Inject Draw Hook
    # We look for the exact line we found earlier: SDL_RenderPresent(renderer_var);
    # We remove whitespace for matching
    
    hook_code = f'''
    // [GLOBAL DIAGNOSTICS]
    static DiagnosticUtilityData* globalDiag = new DiagnosticUtilityData();
    if(globalDiag) { 
        globalDiag->update(); 
        globalDiag->draw({renderer_var}); 
    }
    SDL_RenderPresent({renderer_var});'''
    
    # Replace the render call with our hook + render call
    # We try to match the string generically
    
    search_str = f"SDL_RenderPresent({renderer_var});"
    
    if search_str in code:
        code = code.replace(search_str, hook_code)
        with open(target_path, 'w') as f:
            f.write(code)
        print("[SUCCESS] Diagnostics Injected!")
    else:
        print("[ERROR] Exact string replace failed. Trying looser match...")
        # Fallback for spacing differences
        import re
        regex = r"SDL_RenderPresent\s*\(\s*" + re.escape(renderer_var) + r"\s*\)\s*;"
        code = re.sub(regex, hook_code, code)
        with open(target_path, 'w') as f:
            f.write(code)
        print("[SUCCESS] Diagnostics Injected (Regex)!")

if __name__ == "__main__":
    apply_hook()

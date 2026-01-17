import zipfile
import glob
import os

def dump_tutorial2():
    # We are looking for the config file for "Tutorial 2 - Basic Gameplay"
    target_file = "scn01/scn01.scn" 
    
    ztd_files = glob.glob(os.path.join("build", "Release", "*.ztd"))
    print(f"--- SEARCHING FOR {target_file} ---")

    for ztd_path in ztd_files:
        try:
            with zipfile.ZipFile(ztd_path, 'r') as z:
                # Case insensitive search
                for f in z.namelist():
                    if target_file.lower() in f.lower():
                        print(f"\n[FOUND] {f} inside {os.path.basename(ztd_path)}")
                        print("-" * 50)
                        
                        try:
                            # Read content
                            content = z.read(f).decode('latin-1', errors='ignore')
                            
                            # Print the file content so we can see the [Goals] section
                            print(content)
                        except Exception as e:
                            print(f"Error decoding: {e}")
                            
                        print("-" * 50)
                        return 
        except:
            pass
    print("Could not find the file.")

if __name__ == "__main__":
    dump_tutorial2()
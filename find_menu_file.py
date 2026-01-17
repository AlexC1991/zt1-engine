import os

def search_files():
    print("--- SEARCHING FOR MENU LOGIC ---")
    search_str = "Updating scenario details for"
    
    # Walk through src directory
    for root, dirs, files in os.walk("src"):
        for file in files:
            if file.endswith(".cpp"):
                path = os.path.join(root, file)
                try:
                    with open(path, 'r', encoding='utf-8', errors='ignore') as f:
                        content = f.read()
                        if search_str in content:
                            print(f"\n[FOUND IT!] The logic is in: {path}")
                            print("Please upload this file to the chat!")
                            return
                except:
                    pass
    
    print("\n[Not Found] Could not find the specific log message.")

if __name__ == "__main__":
    search_files()
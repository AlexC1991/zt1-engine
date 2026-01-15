import os

def apply(src_dir, root_dir):
    loader_file = os.path.join(root_dir, "vendor", "pe-resource-loader", "src", "pe_resource_loader.c")
    if not os.path.exists(loader_file): return False

    with open(loader_file, "r", encoding="utf-8") as f: content = f.read()
    
    old_func = "PeResourceLoader_GetDirectoryIdEntries(PeResourceLoader * loader, PeResourceDirectory * directory) {"
    new_func = """PeResourceLoader_GetDirectoryIdEntries(PeResourceLoader * loader, PeResourceDirectory * directory) {
    if (!loader) return NULL; /* [PATCH] NULL check */"""
    
    if old_func in content and "/* [PATCH] NULL check */" not in content:
        content = content.replace(old_func, new_func)
        with open(loader_file, "w", encoding="utf-8") as f: f.write(content)
        print("    -> Patched pe_resource_loader.c")
        return True
    return False
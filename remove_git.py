import os
import shutil
import stat

def remove_readonly(func, path, excinfo):
    os.chmod(path, stat.S_IWRITE)
    func(path)

for root, dirs, files in os.walk('vendor'):
    if '.git' in dirs:
        git_path = os.path.join(root, '.git')
        print(f"Removing: {git_path}")
        shutil.rmtree(git_path, onerror=remove_readonly)
        dirs.remove('.git')

print("Done!")
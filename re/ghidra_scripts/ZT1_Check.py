# Opens ZT1 Check (re/check.py), the window that shows whether what's mapped
# is right: struct fields against zoo.exe's loader code, a species' .ai
# values at the mapped offsets, and - with the original game running - the
# real objects in its memory, watched live.
#@author zt1-engine
#@category zt1-engine
#@menupath Tools.zt1-engine.Open checker
#@runtime Jython

from __future__ import print_function
import os, sys
sys.path.insert(0, getSourceFile().getParentFile().getAbsolutePath())
for _m in ("zt1re", "zt1db", "zt1ghidra"):  # (fresh each run: Jython keeps modules)
    sys.modules.pop(_m, None)
import zt1re

from java.io import File
from java.lang import ProcessBuilder


def main():
    root = zt1re.repo_root(getSourceFile())
    script = zt1re.path(root, "re", "check.py")
    # (pythonw: no console window; python if that's all there is)
    for exe in ("pythonw", "python", "py"):
        try:
            ProcessBuilder([exe, script]).directory(File(root)).start()
            print("Opened ZT1 Check (%s %s)" % (exe, script))
            return
        except:  # noqa: E722 (not on the PATH)
            continue
    popup("Couldn't start Python. Install it, or run  python re/check.py  in the repo.")


main()

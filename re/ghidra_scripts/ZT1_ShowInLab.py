# Sends the function under the cursor to the ZT1 Lab (zt1-engine.exe --lab):
# it's picked there, with its decompiled code, calls, the engine code that
# implements it and any script about it. Ctrl+Alt+L. Starts the Lab if it
# isn't running.
#@author zt1-engine
#@category zt1-engine
#@keybinding ctrl alt L
#@menupath Tools.zt1-engine.Show in Lab
#@runtime Jython

from __future__ import print_function
import os, sys
sys.path.insert(0, getSourceFile().getParentFile().getAbsolutePath())
for _m in ("zt1re", "zt1db", "zt1ghidra"):  # (fresh each run: Jython keeps modules)
    sys.modules.pop(_m, None)
import zt1re

from java.io import File
from java.lang import ProcessBuilder, System


def lab_running():
    try:
        out = ProcessBuilder(["tasklist", "/fo", "csv", "/nh"]).start()
        from java.util import Scanner
        text = Scanner(out.getInputStream()).useDelimiter("\\A").next()
        return "zt1-engine" in text.lower()
    except:  # noqa: E722
        return False


def main():
    root = zt1re.repo_root(getSourceFile())
    f = getFunctionContaining(currentAddress)
    addr = f.getEntryPoint() if f else currentAddress
    out = open(zt1re.path(root, "re", "private", "lab_select.txt"), "w")
    out.write("0x%06x\n" % addr.getOffset())
    out.close()
    print("Sent %s%s to the Lab" % (addr, " (" + f.getName(True) + ")" if f else ""))
    # The bridge, so the Lab can show its code (started if it isn't)
    if System.getProperties().get("zt1.lab.bridge") is None:
        runScript("ZT1_LabBridge.py")
    if not lab_running():
        for exe in ("zt1-engine.exe", "zt1-engine-dev.exe"):
            path = zt1re.path(root, "build", "Release", exe)
            if os.path.exists(path):
                pb = ProcessBuilder([path, "--lab"]).directory(File(zt1re.path(root, "build", "Release")))
                pb.environment().put("ZT_LAB_SELECT", "0x%06x" % addr.getOffset())
                pb.start()
                print("Started the Lab (%s)" % exe)
                return
        popup("No zt1-engine.exe in build/Release: build the engine first.")


main()

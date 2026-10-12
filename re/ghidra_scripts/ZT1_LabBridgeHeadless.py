# The Lab bridge in a headless Ghidra the ZT1 Lab starts for itself (when
# no Ghidra of yours has the bridge on): ZT1_LabBridge started, then kept
# running until the Lab closes this Ghidra (it ends the process). Not for
# the Ghidra window - there, run ZT1_LabBridge itself.
#@author zt1-engine
#@category zt1-engine
#@runtime Jython

import time

runScript("ZT1_LabBridge.py")
print("ZT1 Lab bridge (headless): ready")
while True:
    time.sleep(60)

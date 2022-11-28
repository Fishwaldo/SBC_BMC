import os
import sys
Import("env")
from shutil import copyfile

if os.path.exists("sdkconfig.defaults." + env.get("PIOENV")):
    print("Copying sdkconfig.defaults." + env.get("PIOENV"))
    copyfile("sdkconfig.defaults." + env.get("PIOENV"), "sdkconfig.defaults",)
else:
    sys.exit("sdkconfig.defaults." + env.get("PIOENV") + " does not exist")
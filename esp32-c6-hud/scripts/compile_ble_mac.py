Import("env")

import os

from SCons.Script import File

project_dir = env.subst("$PROJECT_DIR")
build_dir = env.subst("$BUILD_DIR")
src_build_dir = os.path.join(build_dir, "src")
os.makedirs(src_build_dir, exist_ok=True)

include_dir = os.path.join(project_dir, "include")
source = os.path.join(project_dir, "src", "ble_mac.mm")
target = os.path.join(src_build_dir, "ble_mac_mac.o")

ble_obj = env.Command(
    target,
    source,
    (
        "clang++ -std=gnu++17 -fobjc-arc -x objective-c++ "
        f"-I{include_dir} -c $SOURCE -o $TARGET"
    ),
)

env.Append(LINKFLAGS=[ble_obj])
env.Append(LINKFLAGS=["-framework", "CoreBluetooth", "-framework", "Foundation"])
env.Depends("$BUILD_DIR/${PROGNAME}", ble_obj)

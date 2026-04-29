#!/usr/bin/env python
import os
import sys

from methods import print_error


libname = "OIP-COMMS"
projectdir = "demo"

localEnv = Environment(tools=["default"], PLATFORM="")

customs = ["custom.py"]
customs = [os.path.abspath(path) for path in customs]

opts = Variables(customs, ARGUMENTS)
opts.Update(localEnv)

Help(opts.GenerateHelpText(localEnv))

env = localEnv.Clone()

submodule_initialized = False
dir_name = 'godot-cpp'
if os.path.isdir(dir_name):
    if os.listdir(dir_name):
        submodule_initialized = True

if not submodule_initialized:
    print_error("""godot-cpp is not available within this folder, as Git submodules haven't been initialized.
Run the following command to download godot-cpp:

    git submodule update --init --recursive""")
    sys.exit(1)

env = SConscript("godot-cpp/SConstruct", {"env": env, "customs": customs})

env.Append(CPPPATH=["src/", "ads/AdsLib/"])
env.Append(LIBPATH=["lib/"])
env.Append(LIBS=["plctag", "open62541"])
env.Append(CPPDEFINES=[("CONFIG_DEFAULT_LOGLEVEL", "1")])

# ADS variant selection. On Windows we use Beckhoff's installed TcAdsDll because
# TC3 4026's Secure ADS enforcement silently drops the standalone library's plain-TCP
# requests. On Linux/macOS, TcAdsDll is unavailable, so we fall back to the standalone
# library, which works correctly when connecting to a remote TwinCAT system (its
# documented use case).
ads_sources_dir = "ads/AdsLib/standalone"
if env["platform"] == "windows":
    env.Append(CXXFLAGS=["/MT", "/EHsc"])
    env.Append(LIBS=["ws2_32"])
    env.Append(CPPDEFINES=["USE_TWINCAT_ROUTER"])
    beckhoff_ads_root = "C:/Program Files (x86)/Beckhoff/TwinCAT/AdsApi/TcAdsDll"
    env.Append(CPPPATH=[beckhoff_ads_root + "/Include"])
    env.Append(LIBPATH=[beckhoff_ads_root + "/Lib/x64"])
    env.Append(LIBS=["TcAdsDll", "delayimp", "Advapi32"])
    # Delay-load TcAdsDll so the import isn't resolved at our DLL's load time;
    # register_types.cpp's preload_tc_ads_dll() runs first and pulls TcAdsDll.dll
    # in from its standard install path, sidestepping any PATH-staleness in the
    # host process (e.g., Godot started before TwinCAT was installed).
    env.Append(LINKFLAGS=["/DELAYLOAD:TcAdsDll.dll"])
    ads_sources_dir = "ads/AdsLib/TwinCAT"
else:
    env.Append(LINKFLAGS=["-static"])
sources = (
    Glob("src/*.cpp")
    + Glob("ads/AdsLib/*.cpp")
    + Glob("ads/AdsLib/bhf/*.cpp")
    + Glob(ads_sources_dir + "/*.cpp")
)

if env["target"] in ["editor", "template_debug"]:
    try:
        doc_data = env.GodotCPPDocData("src/gen/doc_data.gen.cpp", source=Glob("doc_classes/*.xml"))
        sources.append(doc_data)
    except AttributeError:
        print("Not including class reference as we're targeting a pre-4.3 baseline.")

file = "{}{}{}".format(libname, env["suffix"], env["SHLIBSUFFIX"])
filepath = ""

if env["platform"] == "macos" or env["platform"] == "ios":
    filepath = "{}.framework/".format(env["platform"])
    file = "{}{}".format(libname, env["suffix"])

libraryfile = "bin/{}/{}{}".format(env["platform"], filepath, file)
library = env.SharedLibrary(
    libraryfile,
    source=sources,
)

copy = env.InstallAs("{}/bin/{}/{}lib{}".format(projectdir, env["platform"], filepath, file), library)

default_args = [library, copy]
Default(*default_args)

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

# paho/src/ goes LAST on purpose: ADS and Paho both ship an internal "Log.h",
# and ADS's .cpp files must resolve to ads/AdsLib/Log.h. oip_comms.cpp still
# finds MQTTClient.h here (no name collision). Paho's own .c files need the
# reverse order, so they build with a cloned env (see paho_env below).
env.Append(CPPPATH=["src/", "src/_scaffold/", "ads/AdsLib/", "paho/src/", "thirdparty/quickjs/"])
env.Append(LIBPATH=["lib/"])
env.Append(LIBS=["plctag", "open62541"])
env.Append(CPPDEFINES=[("CONFIG_DEFAULT_LOGLEVEL", "1")])

# With neither PAHO_MQTT_EXPORTS nor PAHO_MQTT_IMPORTS defined, Paho's
# LIBMQTT_API expands to nothing, which is what we want when compiling its
# sources directly into OIP-COMMS.dll (static link). MQTTClient.c also includes
# "VersionInfo.h" (normally cmake-generated); we ship a stub at src/VersionInfo.h.
paho_sources_skip = {
    # async client (we use the sync MQTTClient API exclusively)
    "MQTTAsync.c", "MQTTAsyncUtils.c",
    # TLS support (out of scope for the MVP; pulls OpenSSL)
    "SSLSocket.c",
    # standalone "print client version" CLI helper
    "MQTTVersion.c",
}

# Local TC3 4026 connections require the TwinCAT variant; TC3 4026 enforces
# Secure ADS by default and silently drops requests from the standalone
# library (manifests as 0x745 sync timeout). Standalone variant works fine
# for remote TwinCAT systems. Auto-detect by probing for TcAdsDef.h; without
# TwinCAT installed the build still succeeds via standalone.
ads_sources_dir = "ads/AdsLib/standalone"
beckhoff_ads_root = "C:/Program Files (x86)/Beckhoff/TwinCAT/AdsApi/TcAdsDll"
have_twincat_sdk = (
    env["platform"] == "windows"
    and os.path.isfile(beckhoff_ads_root + "/Include/TcAdsDef.h")
)

if env["platform"] == "windows":
    env.Append(CXXFLAGS=["/MT", "/EHsc"])
    env.Append(LIBS=["ws2_32"])
    if have_twincat_sdk:
        print("ADS variant: TwinCAT (TcAdsDll detected at " + beckhoff_ads_root + ")")
        env.Append(CPPDEFINES=["USE_TWINCAT_ROUTER"])
        env.Append(CPPPATH=[beckhoff_ads_root + "/Include"])
        env.Append(LIBPATH=[beckhoff_ads_root + "/Lib/x64"])
        env.Append(LIBS=["TcAdsDll", "delayimp", "Advapi32"])
        # Delay-load so preload_tc_ads_dll() can resolve TcAdsDll from its
        # install path before any import is touched — works around stale PATH
        # in host processes started before TwinCAT was installed.
        env.Append(LINKFLAGS=["/DELAYLOAD:TcAdsDll.dll"])
        ads_sources_dir = "ads/AdsLib/TwinCAT"
    else:
        print("ADS variant: standalone (TcAdsDll not found at " + beckhoff_ads_root + ")")
else:
    env.Append(LINKFLAGS=["-static"])
# Paho's .c files need paho/src/ FIRST so their internal "Log.h" wins over ADS's
# (the reverse of the main env's order). The clone also isolates Paho-only defines.
paho_env = env.Clone()
paho_env.Replace(CPPPATH=["paho/src/", "src/", "ads/AdsLib/"])
if env["platform"] == "windows":
    # WIN32_LEAN_AND_MEAN keeps <windows.h> from pulling the legacy <winsock.h>,
    # which otherwise collides with the <winsock2.h> Paho actually uses. The
    # _CRT/_WINSOCK_DEPRECATED defines just silence MSVC warning noise.
    paho_env.Append(CPPDEFINES=[
        "WIN32_LEAN_AND_MEAN",
        "_CRT_SECURE_NO_WARNINGS",
        "_WINSOCK_DEPRECATED_NO_WARNINGS",
    ])

paho_objects = [
    paho_env.SharedObject(str(node))
    for node in Glob("paho/src/*.c")
    if os.path.basename(str(node)) not in paho_sources_skip
]

# QuickJS-ng (C11, uses <stdatomic.h>) backs the soft_plc transport's embedded ST engine. Built with
# a cloned env so its C11 flags don't touch paho's C sources. src/soft_plc.cpp (the C++ wrapper) is
# picked up by the Glob("src/*.cpp") below and finds quickjs.h via the CPPPATH added above.
qjs_env = env.Clone()
if env["platform"] == "windows":
    qjs_env.Append(CFLAGS=["/MT", "/std:c11", "/experimental:c11atomics"])
    qjs_env.Append(CPPDEFINES=["_CRT_SECURE_NO_WARNINGS"])
quickjs_objects = [
    qjs_env.SharedObject(s)
    for s in [
        "thirdparty/quickjs/quickjs.c",
        "thirdparty/quickjs/libregexp.c",
        "thirdparty/quickjs/libunicode.c",
        "thirdparty/quickjs/dtoa.c",
    ]
]

sources = (
    Glob("src/*.cpp")
    + Glob("src/_scaffold/*.cpp")  # behavior-VM spike (behavior_engine + behavior_runtime)
    + Glob("ads/AdsLib/*.cpp")
    + Glob("ads/AdsLib/bhf/*.cpp")
    + Glob(ads_sources_dir + "/*.cpp")
    + paho_objects
    + quickjs_objects
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

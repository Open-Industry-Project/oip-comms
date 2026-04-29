# Open Industry Project - GDextension Component
This component is for the Open Industry Project (OIP). It enables communications with the following libraries:

Prebuilt and dropped into `lib/` (built from source from these commits):
- https://github.com/libplctag/libplctag/commit/c55bc5876d938dda1c609750cde5ae4812d7b8a8
- https://github.com/open62541/open62541/commit/de932080cf1264748b5b757dd7e87e422c4df0aa

Tracked as a git submodule under `ads/` and compiled from source with the rest of the project:
- https://github.com/Beckhoff/ADS

The Beckhoff ADS library has two variants and the build picks one per platform:
- **Windows**: links against the locally-installed `TcAdsDll.dll` (Beckhoff's official client) via the `USE_TWINCAT_ROUTER` define. Required because TwinCAT 3 build 4026 enforces Secure ADS, and the standalone library only speaks plain TCP — its requests are silently dropped by 4026's runtime. Requires TwinCAT to be installed locally; `TcAdsDll.dll` is bundled alongside the GDExtension at distribution time so end-users without a TwinCAT install on PATH still load it.
- **Linux / macOS**: uses the standalone library (its own AmsRouter, plain TCP). This is the standalone lib's documented use case — connecting from a non-TwinCAT host to a remote TwinCAT system. A route entry must be configured on the remote TwinCAT for the local AmsNetId.

See PR on Open Industry Project [https://github.com/open62541/open62541](https://github.com/Open-Industry-Project/Open-Industry-Project/pull/161)

# Building from Source
Please read Godot's documentation on building from source and GDextension:
- https://docs.godotengine.org/en/stable/contributing/development/compiling/index.html
- https://docs.godotengine.org/en/stable/tutorials/scripting/gdextension/gdextension_cpp_example.html

After cloning, fetch the submodules (`godot-cpp` and `ads`):

`git submodule update --init --recursive`

Build command:
`scons platform=windows debug_symbols=yes`

The output of building will be the DLLs located in: https://github.com/bikemurt/OIP_gdext/tree/main/demo/bin/windows

The DLLs, and `oip_comms.gdextension` file must be copied to the `oip_comms` dock plugin for the main Open Industry Project repo: `Open-Industry-Project/addons/oip_comms/bin/`. Right now just building for Windows, but should be extendable to other platforms.

This GDextension as well as the libs (libplctag, open62541) are built with the `/MT` flag. According to dumpbin this removes any external deps on MSVC runtime and should improve portability.

https://stackoverflow.com/a/56061183/7132687

This project uses the standard library.

# Documentation

Update https://github.com/bikemurt/OIP_gdext/blob/main/doc_classes/OIPComms.xml as required and rebuild.

# Debugging
As long as you build with `debug_symbols=yes`, the 4.5 branch of OIP will be able to debug this GDextension application. 

Inside the `.vs/` folder you can create `launch.vs.json`:

```
{
	"version": "0.2.1",
	"defaults": {},
	"configurations": [
		{
			"type": "default",
			"project": "location_of_oip_4.5_build\\godot.windows.editor.x86_64.exe",
			"name": "Godot Editor",
			"args": [ "location_of_project\\Open-Industry-Project\\project.godot" ]
		}
	]
}
```

Then you can launch the OIP editor from Visual Studio and drop in breakpoints to test.

# Licensing
Right now technically there is no license and use is only as a part of the Open Industry Project.
I haven't gone through licensing requirements yet, but please review licensing requirements of the Godot Engine, the Open Industry Project, libplctag and open62541. It's going to be the common denominator of those licenses.

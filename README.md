# Open Industry Project - GDextension Component

> [!IMPORTANT]
> This is the communications plugin for the Godot version of Open Industry Project, and it is no longer maintained. Its successor runs in the browser and as a desktop app: try it at [openindustryproject.org](https://www.openindustryproject.org), or find the code at [Open-Industry-Project/Open-Industry-Project](https://github.com/Open-Industry-Project/Open-Industry-Project).

This component is for the Open Industry Project (OIP). It exposes a single `OIPComms` singleton that drives multiple industrial-comms protocols through one unified read/write/poll API. Each tag group is registered with a `protocol` string that selects the backend.


| `protocol` | Backend | Source |
|------------|---------|--------|
| `ab_eip`, `modbus_tcp` | [libplctag](https://github.com/libplctag/libplctag) | prebuilt in `lib/` |
| `opc_ua` | [open62541](https://github.com/open62541/open62541) | prebuilt in `lib/` |
| `s7` | Siemens S7 PUT/GET | vendored in [src/S7Com.cpp](src/S7Com.cpp) |
| `ads` | [Beckhoff/ADS](https://github.com/Beckhoff/ADS) | git submodule under `ads/` |
| `rtde` | Universal Robots Real-Time Data Exchange (v2) | hand-rolled in [src/rtde_client.cpp](src/rtde_client.cpp), no external dep |
| `mqtt` | [Eclipse Paho MQTT C](https://github.com/eclipse-paho/paho.mqtt.c) (synchronous client) | git submodule under `paho/`, compiled inline (no separate `.dll` to ship) |

Prebuilt-library commits (built from source, dropped into `lib/`):
- https://github.com/libplctag/libplctag/commit/c55bc5876d938dda1c609750cde5ae4812d7b8a8
- https://github.com/open62541/open62541/commit/de932080cf1264748b5b757dd7e87e422c4df0aa

The Beckhoff ADS library has two variants and the build picks one per platform:
- **Windows**: links against the locally-installed `TcAdsDll.dll` (Beckhoff's official client) via the `USE_TWINCAT_ROUTER` define. Required because TwinCAT 3 build 4026 enforces Secure ADS, and the standalone library only speaks plain TCP — its requests are silently dropped by 4026's runtime. Requires TwinCAT to be installed locally; the GDExtension delay-loads `TcAdsDll.dll` at runtime, resolving the install path from the Windows registry (see [src/tcads_loader.cpp](src/tcads_loader.cpp)). The DLL itself is **not** redistributed with this project.
- **Linux / macOS**: uses the standalone library (its own AmsRouter, plain TCP). This is the standalone lib's documented use case — connecting from a non-TwinCAT host to a remote TwinCAT system. A route entry must be configured on the remote TwinCAT for the local AmsNetId.

## Modbus specifics
As mentioned above, modbus TCP communications are achieved through the `libtagplc` library. It is generally well behaved but the only potential hiccup is dealing with multi-register data types like FLOAT32. `libtagplc` uses `3210`, big-endian byte order (see [here](https://github.com/libplctag/libplctag/wiki/Tag-String-Attributes#optional-generic-byte-order-attributes)).

A modbus "element" is a single 16-bit register, so any value wider than 16 bits spans multiple registers. You declare the value's type when registering the tag and `OIPComms` sizes the register span for you:

`OIPComms.register_tag(tag_group_name, tag_name, OIPComms.TAG_TYPE_FLOAT32)`

The `data_type` argument (one of the `TAG_TYPE_*` constants) is only consulted for `modbus_tcp`; FLOAT32/INT32/UINT32 occupy two registers, FLOAT64/INT64/UINT64 occupy four, and everything else is one. For all other protocols the type is inferred from the `read_*`/`write_*` method you call and `data_type` is ignored (it defaults to `TAG_TYPE_BOOL`).

This calculator can be used to help confirm your data type and endian-ness: https://modbuskit.com/en/data-converter

Other tools that can help modbus debugging:
- [ModbusPal](https://modbuspal.sourceforge.net/): this acts as a modbus simulator, generating modbus data
- [QModMaster](https://sourceforge.net/projects/qmodmaster/): this acts as a modbus client, reading data from ModbusPal

## RTDE specifics
The RTDE backend talks to Universal Robots controllers (real hardware or URSim) on TCP port 30004 using protocol version 2. The full variable vocabulary is documented in the [official UR RTDE guide](https://docs.universal-robots.com/tutorials/communication-protocol-tutorials/rtde-guide.html). Tag-group fields:
- **Gateway**: robot IP address (e.g. `192.168.56.101`).
- **Path / CPU**: unused.
- **Polling rate (ms)**: mapped to the controller's stream frequency (`Hz = 1000 / polling_ms`), clamped to [1, 500] Hz.

Tag names follow UR's RTDE variable vocabulary. Vector fields are read element-by-element using `name[index]` syntax. A few examples:
- `actual_q[0]` … `actual_q[5]` — joint angles, one tag per joint (read with `read_float64`).
- `actual_TCP_pose[0]` … `actual_TCP_pose[5]` — TCP pose components.
- `runtime_state` — controller program state (read with `read_uint32`).
- `input_int_register_0` — writable INT32 register (write with `write_int32`).
- `input_double_register_0` — writable DOUBLE register (write with `write_float64`).

Tag names that begin with `input_` are routed to the input recipe automatically and are writable from this side; everything else lives in the output recipe and is read-only.

## MQTT specifics
The MQTT backend talks to any MQTT 3.1.1 broker over plain TCP (the MVP does not yet support TLS, QoS 1/2, or retained messages). Each registered tag name is treated as one MQTT topic; the broker payload is interpreted as a raw little-endian scalar matching whichever `read_*`/`write_*` API is used to access it. There is no JSON/text decoding; the producer side is expected to publish the same byte layout the consumer reads.

Tag-group fields:
- **Gateway**: broker host with optional port. Accepts `host`, `host:port`, or a full `tcp://host:port` URI. Port defaults to 1883.
- **Path**: MQTT client ID. Leave empty to use an auto-generated id (`oip_comms_<group>_<addr>`).
- **CPU**: optional credentials as `user:password`. Empty means anonymous; `user` alone (no colon) is sent without a password.

Tag names map directly to topics, e.g. `sensors/temp1`, `factory/line_a/state`. MQTT wildcards (`+`, `#`) are not supported; each tag is one specific topic.

A poll cycle does not perform any per-tag work for MQTT: subscriptions are issued once at session start, then Paho's receiver thread fills the per-tag value cache as messages arrive. `polling_interval` therefore controls only the reconnect/health-check cadence, not the read rate.

Paho is a git submodule at `paho/` pinned to v1.3.13; its `.c` sources are compiled directly into `OIP-COMMS.dll` (no separate `paho-mqtt3c.dll` to ship). The sync-client subset is built; async, TLS, and the standalone version CLI are skipped. The version string Paho exposes at runtime is hard-coded in [src/VersionInfo.h](src/VersionInfo.h) and should be bumped when the submodule is updated.

See PR on Open Industry Project [https://github.com/open62541/open62541](https://github.com/Open-Industry-Project/Open-Industry-Project/pull/161)

# Building from Source
Please read Godot's documentation on building from source and GDextension:
- https://docs.godotengine.org/en/stable/contributing/development/compiling/index.html
- https://docs.godotengine.org/en/stable/tutorials/scripting/gdextension/gdextension_cpp_example.html

After cloning, fetch the submodules (`godot-cpp` and `ads`):

`git submodule update --init --recursive`

Build command:
`scons platform=windows debug_symbols=yes`

The output of building will be the DLLs located in: https://github.com/Open-Industry-Project/oip-comms/tree/main/demo/bin/windows

The DLLs, and `oip_comms.gdextension` file must be copied to the `oip_comms` dock plugin for the main Open Industry Project repo: `Open-Industry-Project/addons/oip_comms/bin/`. Right now just building for Windows, but should be extendable to other platforms.

This GDextension as well as the libs (libplctag, open62541) are built with the `/MT` flag. According to dumpbin this removes any external deps on MSVC runtime and should improve portability.

https://stackoverflow.com/a/56061183/7132687

This project uses the standard library.

# Documentation

Update https://github.com/Open-Industry-Project/oip-comms/blob/main/doc_classes/OIPComms.xml as required and rebuild.

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

## License

This project is licensed under the [MIT License](LICENSE.md).

For the licenses of bundled and linked third-party libraries (godot-cpp, Beckhoff ADS, libplctag, open62541, TcAdsDll), see [THIRD_PARTY_LICENSES.md](THIRD_PARTY_LICENSES.md).

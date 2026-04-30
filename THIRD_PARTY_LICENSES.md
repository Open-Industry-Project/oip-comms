# Third-Party Licenses

This GDExtension links against and/or bundles the following third-party libraries. Each is distributed under its own license, reproduced or referenced below.

---

## godot-cpp

- **Upstream:** https://github.com/godotengine/godot-cpp
- **Used as:** git submodule, statically linked into the GDExtension
- **License:** MIT

```
MIT License

Copyright (c) 2017-present Godot Engine contributors.

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.
```

---

## Beckhoff ADS

- **Upstream:** https://github.com/Beckhoff/ADS
- **Used as:** git submodule (`ads/`), compiled from source on Linux/macOS
- **License:** MIT

```
The MIT License (MIT)
Copyright (c) 2015 Beckhoff Automation GmbH & Co. KG

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in
all copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.
```

---

## libplctag

- **Upstream:** https://github.com/libplctag/libplctag
- **Pinned commit:** [c55bc5876d938dda1c609750cde5ae4812d7b8a8](https://github.com/libplctag/libplctag/commit/c55bc5876d938dda1c609750cde5ae4812d7b8a8)
- **Used as:** prebuilt binary in `lib/plctag.dll` (and `lib/plctag.lib`)
- **License:** dual-licensed, **MPL 2.0** OR **LGPL 2.1+** (recipient's choice)
- **SPDX:** `MPL-2.0 OR LGPL-2.1-or-later`

Full license texts at the pinned commit:
- MPL 2.0: https://github.com/libplctag/libplctag/blob/c55bc5876d938dda1c609750cde5ae4812d7b8a8/LICENSE.MPL.txt
- LGPL 2.1: https://github.com/libplctag/libplctag/blob/c55bc5876d938dda1c609750cde5ae4812d7b8a8/LICENSE.LGPL.txt

Per MPL 2.0 §3.2, source code for the libplctag files is available at the upstream commit linked above. Per LGPL 2.1, the library is dynamically linked, and recipients may relink against a modified version by replacing `plctag.dll` in the distribution.

---

## open62541

- **Upstream:** https://github.com/open62541/open62541
- **Pinned commit:** [de932080cf1264748b5b757dd7e87e422c4df0aa](https://github.com/open62541/open62541/commit/de932080cf1264748b5b757dd7e87e422c4df0aa)
- **Used as:** prebuilt binary in `lib/open62541.dll` (and `lib/open62541.lib`)
- **License:** **MPL 2.0**
- **SPDX:** `MPL-2.0`

Full license text at the pinned commit:
- https://github.com/open62541/open62541/blob/de932080cf1264748b5b757dd7e87e422c4df0aa/LICENSE

Per MPL 2.0 §3.2, source code for the open62541 files is available at the upstream commit linked above.

---

## TcAdsDll.dll (Beckhoff TwinCAT)

- **Source:** Beckhoff TwinCAT installation (proprietary)
- **Used as:** runtime delay-loaded DLL on Windows; resolved from the local TwinCAT install via the Windows registry (see [src/tcads_loader.cpp](src/tcads_loader.cpp))
- **License:** Beckhoff proprietary — covered by the user's TwinCAT EULA

This DLL is **not redistributed** with this project. It must be present on the end-user's machine via a TwinCAT installation, and is loaded at runtime from the install path discovered in the registry.

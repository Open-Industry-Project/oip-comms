#ifndef OIP_TCADS_LOADER_H
#define OIP_TCADS_LOADER_H

// Probes the environment for Beckhoff's TcAdsDll.dll and tries to LoadLibrary
// it from a known path so the delayed import resolves at first ADS call.
//
// Lookup order (Windows):
//   1. OIP_TCADSDLL_PATH environment variable (full path to the DLL)
//   2. Registry: derive install root from HKLM\SOFTWARE\WOW6432Node\Beckhoff\
//      TwinCAT3\System\ConnectionProvider, append Common64\TcAdsDll.dll
//   3. Standard install paths under Program Files (x86)\Beckhoff and
//      Program Files\Beckhoff
//   4. Bare LoadLibrary("TcAdsDll.dll"), which relies on the host process's
//      DLL search path (usually PATH)
//
// On non-Windows builds (Linux, macOS) the standalone Beckhoff library is
// statically linked into our DLL, so this is a no-op and the availability
// check always returns true.
void preload_tc_ads_dll();

// Returns whether TcAdsDll.dll could be located. ADS code paths must check
// this and bail with a graceful error if false; calling any TcAdsDll function
// without it will trigger a delay-load failure.
bool is_tc_ads_dll_available();

#endif

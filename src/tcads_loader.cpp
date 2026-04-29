#include "tcads_loader.h"

#ifdef _WIN32

#include <windows.h>
#include <delayimp.h>
#include <cstdlib>
#include <cstring>
#include <string>

static bool g_tc_ads_available = false;

static bool try_load(const std::string &path)
{
	if (path.empty()) return false;
	if (LoadLibraryA(path.c_str())) {
		g_tc_ads_available = true;
		return true;
	}
	return false;
}

// Read ConnectionProvider value from HKLM\SOFTWARE\WOW6432Node\Beckhoff\TwinCAT3\System
// and derive the install root. ConnectionProvider points at e.g.
//   "C:\Program Files (x86)\Beckhoff\TwinCAT\3.1\System\TcComPortConnection.dll"
// The install root is the path before "\3.1\System\".
static std::string tc_install_root_from_registry()
{
	HKEY key;
	if (RegOpenKeyExA(HKEY_LOCAL_MACHINE,
			"SOFTWARE\\WOW6432Node\\Beckhoff\\TwinCAT3\\System",
			0, KEY_READ | KEY_WOW64_32KEY, &key) != ERROR_SUCCESS) {
		return std::string();
	}
	char buf[MAX_PATH];
	DWORD size = sizeof(buf);
	DWORD type = 0;
	LONG status = RegQueryValueExA(key, "ConnectionProvider", nullptr, &type,
			reinterpret_cast<LPBYTE>(buf), &size);
	RegCloseKey(key);
	if (status != ERROR_SUCCESS || type != REG_SZ) {
		return std::string();
	}
	std::string p(buf);
	const std::string marker = "\\3.1\\System\\";
	const auto pos = p.find(marker);
	if (pos == std::string::npos) {
		return std::string();
	}
	return p.substr(0, pos);
}

// Delay-load failure hook. Called by the runtime when the delayed TcAdsDll
// import can't be resolved. Returning nullptr from a name-resolution failure
// makes the offending call fail with an EXCEPTION_INVALID_HANDLE rather than
// terminating the process; combined with our is_tc_ads_dll_available() guard
// in init_ads_device this should never actually fire, but if it does, we
// at least don't take the host process down with us.
static FARPROC WINAPI tc_ads_delay_load_hook(unsigned reason, DelayLoadInfo *info)
{
	if (info && info->szDll && _stricmp(info->szDll, "TcAdsDll.dll") == 0) {
		if (reason == dliFailLoadLib || reason == dliFailGetProc) {
			return nullptr;
		}
	}
	return nullptr;
}

// MSVC delay-load failure hook registration. The runtime calls this name.
extern "C" const PfnDliHook __pfnDliFailureHook2 = tc_ads_delay_load_hook;

void preload_tc_ads_dll()
{
	if (g_tc_ads_available) return;

	// 1. Explicit override
	if (const char *env = std::getenv("OIP_TCADSDLL_PATH")) {
		if (try_load(env)) return;
	}

	// 2. Registry-derived path
	const std::string root = tc_install_root_from_registry();
	if (!root.empty()) {
		if (try_load(root + "\\Common64\\TcAdsDll.dll")) return;
	}

	// 3. Standard install paths
	const char *standards[] = {
		"C:\\Program Files (x86)\\Beckhoff\\TwinCAT\\Common64\\TcAdsDll.dll",
		"C:\\Program Files\\Beckhoff\\TwinCAT\\Common64\\TcAdsDll.dll",
	};
	for (const char *path : standards) {
		if (try_load(path)) return;
	}

	// 4. Final fallback: ordinary search (PATH, system dirs)
	if (LoadLibraryA("TcAdsDll.dll")) {
		g_tc_ads_available = true;
	}
}

bool is_tc_ads_dll_available()
{
	return g_tc_ads_available;
}

#else // non-Windows: standalone Beckhoff lib is statically linked, always available

void preload_tc_ads_dll() {}
bool is_tc_ads_dll_available() { return true; }

#endif

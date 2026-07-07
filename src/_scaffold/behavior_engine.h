// BehaviorEngine — embeds the web's portable PART BEHAVIOR runtime (oip-behavior.js) in a QuickJS
// runtime: the BIDIRECTIONAL counterpart of SoftPlcEngine. Unlike the PLC (pure string in/out), a
// behavior calls OUT mid-tick, so this registers globalThis.__sim + globalThis.__tags as native
// functions that forward to a host-provided SimHost. The engine itself stays Godot-agnostic (no
// godot-cpp here, exactly like SoftPlcEngine); the Godot side implements SimHost.
// See oip-web/docs/embedded-behavior-vm.md and oip-web/src/core/embeddedBehavior.ts.
//
// STARTING SCAFFOLD — modeled on soft_plc.{h,cpp}; NOT yet compiled. Verify the QuickJS API calls
// against the vendored quickjs version. Spike scope: setSurfaceVelocity + raycast + bool tags only.
#pragma once

#include <string>

// The native side implements this; __sim/__tags forward to it. Coarse-cadence only (per part per
// tick) — never call this per-body-per-contact (keep the hot loop in the engine).
struct SimHost {
	virtual ~SimHost() = default;

	// __sim (the SimInterface subset the spike needs)
	virtual void set_surface_velocity(int body, int sub, double vx, double vy, double vz) = 0;
	// returns true if the ray hit anything — the belt_driver fixture only checks `hit != null`
	virtual bool raycast(double ox, double oy, double oz, double dx, double dy, double dz, double max_dist) = 0;

	// __tags (spike: BOOL tags only; add numeric read/write when a behavior needs it)
	virtual bool tag_read_bool(const std::string &name) = 0;
	virtual void tag_write_bool(const std::string &name, bool value) = 0;
};

class BehaviorEngine {
	struct Impl;
	Impl *impl;

public:
	BehaviorEngine();
	~BehaviorEngine();

	// Must be set before load_bundle()/update() — the C functions read it via JS_GetContextOpaque.
	void set_host(SimHost *host);

	// Evaluate the bundle (dist-embedded/oip-behavior.js) and register __sim/__tags. false + error() on fail.
	bool load_bundle(const std::string &js);

	int create(const std::string &type, const std::string &params_json, const std::string &bodies_json); // >0 handle, or 0 + error()
	void update(int handle, double dt); // behavior calls __sim/__tags during this
	void destroy(int handle);
	std::string error(); // last error (cleared on read)
};

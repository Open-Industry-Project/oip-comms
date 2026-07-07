// BehaviorRuntime — the Godot-bound (godot-cpp) host for the portable behavior runtime. Owns a
// BehaviorEngine + implements SimHost (via composition). The TS behavior calls __sim.setSurfaceVelocity
// and the adapter applies it DIRECTLY to a registered StaticBody3D's constant_linear_velocity — so the
// conveyance is driven by the TS behavior, not by any GDScript part logic.
// GDScript API: load() / register_body() / attach() / tick() / set_tag() / get_tag().
// Spike scope: see _scaffold/behavior_engine.README.md.
#pragma once

#include "behavior_engine.h"

#include <godot_cpp/classes/ref_counted.hpp>
#include <godot_cpp/classes/static_body3d.hpp>
#include <godot_cpp/variant/array.hpp>
#include <godot_cpp/variant/dictionary.hpp>
#include <godot_cpp/variant/string.hpp>

#include <map>

namespace godot {

class BehaviorRuntime : public RefCounted {
	GDCLASS(BehaviorRuntime, RefCounted)

	// SimHost implemented by composition (BehaviorRuntime IS a RefCounted; HAS a host).
	struct Host : SimHost {
		BehaviorRuntime *o = nullptr;
		void set_surface_velocity(int body, int sub, double vx, double vy, double vz) override;
		bool raycast(double ox, double oy, double oz, double dx, double dy, double dz, double max_dist) override;
		bool tag_read_bool(const std::string &name) override;
		void tag_write_bool(const std::string &name, bool value) override;
	} host;

	BehaviorEngine engine;
	Dictionary tags;                            // __tags backing store, settable from GDScript
	std::map<int, StaticBody3D *> bodies;       // behavior body-handle -> real surface body the adapter drives

protected:
	static void _bind_methods();

public:
	BehaviorRuntime();

	bool load(const String &bundle_path);
	void register_body(int handle, StaticBody3D *body);
	int attach(const String &type, const Dictionary &params, const Array &bodies_arg); // -> handle (>0) or 0
	void tick(int handle, double dt);
	void set_tag(const String &name, bool value);
	bool get_tag(const String &name) const;
};

} // namespace godot

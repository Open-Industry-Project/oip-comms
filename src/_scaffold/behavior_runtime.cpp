// See behavior_runtime.h. The adapter now applies surface velocity DIRECTLY to a registered
// StaticBody3D (constant_linear_velocity) — the TS behavior is what drives conveyance.
#include "behavior_runtime.h"

#include <godot_cpp/classes/file_access.hpp>
#include <godot_cpp/classes/json.hpp>
#include <godot_cpp/core/class_db.hpp>
#include <godot_cpp/variant/vector3.hpp>

using namespace godot;

void BehaviorRuntime::Host::set_surface_velocity(int body, int /*sub*/, double vx, double vy, double vz) {
	std::map<int, StaticBody3D *>::iterator it = o->bodies.find(body);
	if (it != o->bodies.end() && it->second != nullptr) {
		// exactly what a conveyor deck does: a static body with a constant linear velocity drags
		// resting RigidBodies along it. Here the velocity comes from the TS behavior, not GDScript.
		it->second->set_constant_linear_velocity(Vector3((real_t)vx, (real_t)vy, (real_t)vz));
	}
}

bool BehaviorRuntime::Host::raycast(double, double, double, double, double, double, double) {
	return false; // spike stub — the belt_driver fixture only checks hit != null
}

bool BehaviorRuntime::Host::tag_read_bool(const std::string &name) {
	return (bool)o->tags.get(String(name.c_str()), false);
}

void BehaviorRuntime::Host::tag_write_bool(const std::string &name, bool value) {
	o->tags[String(name.c_str())] = value;
}

BehaviorRuntime::BehaviorRuntime() {
	host.o = this;
	engine.set_host(&host);
}

bool BehaviorRuntime::load(const String &bundle_path) {
	Ref<FileAccess> f = FileAccess::open(bundle_path, FileAccess::READ);
	if (f.is_null()) {
		return false;
	}
	const String js = f->get_as_text();
	return engine.load_bundle(js.utf8().get_data());
}

void BehaviorRuntime::register_body(int handle, StaticBody3D *body) {
	bodies[handle] = body;
}

int BehaviorRuntime::attach(const String &type, const Dictionary &params, const Array &bodies_arg) {
	const String params_json = JSON::stringify(params);
	const String bodies_json = JSON::stringify(bodies_arg);
	return engine.create(type.utf8().get_data(), params_json.utf8().get_data(), bodies_json.utf8().get_data());
}

void BehaviorRuntime::tick(int handle, double dt) {
	engine.update(handle, dt);
}

void BehaviorRuntime::set_tag(const String &name, bool value) {
	tags[name] = value;
}

bool BehaviorRuntime::get_tag(const String &name) const {
	return (bool)tags.get(name, false);
}

void BehaviorRuntime::_bind_methods() {
	ClassDB::bind_method(D_METHOD("load", "bundle_path"), &BehaviorRuntime::load);
	ClassDB::bind_method(D_METHOD("register_body", "handle", "body"), &BehaviorRuntime::register_body);
	ClassDB::bind_method(D_METHOD("attach", "type", "params", "bodies"), &BehaviorRuntime::attach);
	ClassDB::bind_method(D_METHOD("tick", "handle", "dt"), &BehaviorRuntime::tick);
	ClassDB::bind_method(D_METHOD("set_tag", "name", "value"), &BehaviorRuntime::set_tag);
	ClassDB::bind_method(D_METHOD("get_tag", "name"), &BehaviorRuntime::get_tag);
}

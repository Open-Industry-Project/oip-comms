// See behavior_engine.h. Modeled on soft_plc.cpp. STARTING SCAFFOLD — not yet compiled; verify the
// QuickJS API against the vendored version (signatures are stable in quickjs-ng but double-check).
#include "behavior_engine.h"

extern "C" {
#include "quickjs.h"
}

// ---- helpers to read a {x,y,z} arg object and the SimHost from the context ----
static double prop(JSContext *ctx, JSValueConst obj, const char *key) {
	JSValue v = JS_GetPropertyStr(ctx, obj, key);
	double d = 0;
	JS_ToFloat64(ctx, &d, v);
	JS_FreeValue(ctx, v);
	return d;
}

// __sim.setSurfaceVelocity(body, sub, {x,y,z})
static JSValue js_set_surface_velocity(JSContext *ctx, JSValueConst, int argc, JSValueConst *argv) {
	SimHost *host = static_cast<SimHost *>(JS_GetContextOpaque(ctx));
	if (!host || argc < 3) {
		return JS_UNDEFINED;
	}
	int32_t body = 0, sub = 0;
	JS_ToInt32(ctx, &body, argv[0]);
	JS_ToInt32(ctx, &sub, argv[1]);
	host->set_surface_velocity(body, sub, prop(ctx, argv[2], "x"), prop(ctx, argv[2], "y"), prop(ctx, argv[2], "z"));
	return JS_UNDEFINED;
}

// __sim.raycast({x,y,z}, {x,y,z}, maxDist) -> a (truthy) object on hit, else null
static JSValue js_raycast(JSContext *ctx, JSValueConst, int argc, JSValueConst *argv) {
	SimHost *host = static_cast<SimHost *>(JS_GetContextOpaque(ctx));
	if (!host || argc < 3) {
		return JS_NULL;
	}
	double max_dist = 0;
	JS_ToFloat64(ctx, &max_dist, argv[2]);
	bool hit = host->raycast(prop(ctx, argv[0], "x"), prop(ctx, argv[0], "y"), prop(ctx, argv[0], "z"),
			prop(ctx, argv[1], "x"), prop(ctx, argv[1], "y"), prop(ctx, argv[1], "z"), max_dist);
	// the belt_driver fixture only checks `hit !== null`; a real adapter would return {body,point,normal,distance}
	return hit ? JS_NewObject(ctx) : JS_NULL;
}

// __tags.read(name) -> bool
static JSValue js_tag_read(JSContext *ctx, JSValueConst, int argc, JSValueConst *argv) {
	SimHost *host = static_cast<SimHost *>(JS_GetContextOpaque(ctx));
	if (!host || argc < 1) {
		return JS_FALSE;
	}
	const char *name = JS_ToCString(ctx, argv[0]);
	bool v = host->tag_read_bool(name ? name : "");
	if (name) {
		JS_FreeCString(ctx, name);
	}
	return JS_NewBool(ctx, v);
}

// __tags.write(name, value)  (spike: bool)
static JSValue js_tag_write(JSContext *ctx, JSValueConst, int argc, JSValueConst *argv) {
	SimHost *host = static_cast<SimHost *>(JS_GetContextOpaque(ctx));
	if (!host || argc < 2) {
		return JS_UNDEFINED;
	}
	const char *name = JS_ToCString(ctx, argv[0]);
	host->tag_write_bool(name ? name : "", JS_ToBool(ctx, argv[1]) == 1);
	if (name) {
		JS_FreeCString(ctx, name);
	}
	return JS_UNDEFINED;
}

struct BehaviorEngine::Impl {
	JSRuntime *rt = nullptr;
	JSContext *ctx = nullptr;
	JSValue api = JS_UNDEFINED; // globalThis.OipBehavior
	std::string last_error;

	JSValue call(const char *method, int argc, JSValue *argv) {
		if (!JS_IsObject(api)) {
			last_error = "behavior: bundle not loaded";
			return JS_UNDEFINED;
		}
		JSValue fn = JS_GetPropertyStr(ctx, api, method);
		JSValue ret = JS_Call(ctx, fn, api, argc, argv);
		JS_FreeValue(ctx, fn);
		if (JS_IsException(ret)) {
			JSValue ex = JS_GetException(ctx);
			const char *msg = JS_ToCString(ctx, ex);
			last_error = msg ? std::string(msg) : std::string("behavior: unknown JS exception");
			if (msg) {
				JS_FreeCString(ctx, msg);
			}
			JS_FreeValue(ctx, ex);
			JS_FreeValue(ctx, ret);
			return JS_UNDEFINED;
		}
		return ret;
	}

	// Register globalThis.__sim and globalThis.__tags before the bundle's behaviors run.
	void install_host_globals() {
		JSValue global = JS_GetGlobalObject(ctx);

		JSValue sim = JS_NewObject(ctx);
		JS_SetPropertyStr(ctx, sim, "setSurfaceVelocity", JS_NewCFunction(ctx, js_set_surface_velocity, "setSurfaceVelocity", 3));
		JS_SetPropertyStr(ctx, sim, "raycast", JS_NewCFunction(ctx, js_raycast, "raycast", 3));
		JS_SetPropertyStr(ctx, global, "__sim", sim);

		JSValue tags = JS_NewObject(ctx);
		JS_SetPropertyStr(ctx, tags, "read", JS_NewCFunction(ctx, js_tag_read, "read", 1));
		JS_SetPropertyStr(ctx, tags, "write", JS_NewCFunction(ctx, js_tag_write, "write", 2));
		JS_SetPropertyStr(ctx, global, "__tags", tags);

		JS_FreeValue(ctx, global);
	}
};

BehaviorEngine::BehaviorEngine() {
	impl = new Impl();
	impl->rt = JS_NewRuntime();
	impl->ctx = JS_NewContext(impl->rt);
}

BehaviorEngine::~BehaviorEngine() {
	if (!JS_IsUndefined(impl->api)) {
		JS_FreeValue(impl->ctx, impl->api);
	}
	if (impl->ctx) {
		JS_FreeContext(impl->ctx);
	}
	if (impl->rt) {
		JS_FreeRuntime(impl->rt);
	}
	delete impl;
}

void BehaviorEngine::set_host(SimHost *host) {
	JS_SetContextOpaque(impl->ctx, host); // the C functions read this
}

bool BehaviorEngine::load_bundle(const std::string &js) {
	impl->install_host_globals(); // __sim/__tags must exist before behaviors call them
	JSValue r = JS_Eval(impl->ctx, js.c_str(), js.size(), "oip-behavior.js", JS_EVAL_TYPE_GLOBAL);
	if (JS_IsException(r)) {
		JSValue ex = JS_GetException(impl->ctx);
		const char *msg = JS_ToCString(impl->ctx, ex);
		impl->last_error = msg ? std::string(msg) : std::string("behavior: eval exception");
		if (msg) {
			JS_FreeCString(impl->ctx, msg);
		}
		JS_FreeValue(impl->ctx, ex);
		JS_FreeValue(impl->ctx, r);
		return false;
	}
	JS_FreeValue(impl->ctx, r);

	JSValue global = JS_GetGlobalObject(impl->ctx);
	if (!JS_IsUndefined(impl->api)) {
		JS_FreeValue(impl->ctx, impl->api);
	}
	impl->api = JS_GetPropertyStr(impl->ctx, global, "OipBehavior");
	JS_FreeValue(impl->ctx, global);
	if (!JS_IsObject(impl->api)) {
		impl->last_error = "behavior: globalThis.OipBehavior missing after eval";
		return false;
	}
	return true;
}

int BehaviorEngine::create(const std::string &type, const std::string &params_json, const std::string &bodies_json) {
	JSValue argv[3];
	argv[0] = JS_NewStringLen(impl->ctx, type.c_str(), type.size());
	argv[1] = JS_NewStringLen(impl->ctx, params_json.c_str(), params_json.size());
	argv[2] = JS_NewStringLen(impl->ctx, bodies_json.c_str(), bodies_json.size());
	JSValue ret = impl->call("create", 3, argv);
	for (JSValue &a : argv) {
		JS_FreeValue(impl->ctx, a);
	}
	int32_t handle = 0;
	JS_ToInt32(impl->ctx, &handle, ret);
	JS_FreeValue(impl->ctx, ret);
	return handle;
}

void BehaviorEngine::update(int handle, double dt) {
	JSValue argv[2];
	argv[0] = JS_NewInt32(impl->ctx, handle);
	argv[1] = JS_NewFloat64(impl->ctx, dt);
	JSValue ret = impl->call("update", 2, argv);
	JS_FreeValue(impl->ctx, argv[0]);
	JS_FreeValue(impl->ctx, argv[1]);
	JS_FreeValue(impl->ctx, ret);
}

void BehaviorEngine::destroy(int handle) {
	JSValue arg = JS_NewInt32(impl->ctx, handle);
	JSValue ret = impl->call("destroy", 1, &arg);
	JS_FreeValue(impl->ctx, arg);
	JS_FreeValue(impl->ctx, ret);
}

std::string BehaviorEngine::error() {
	if (JS_IsObject(impl->api)) {
		JSValue ret = impl->call("error", 0, nullptr);
		const char *s = JS_ToCString(impl->ctx, ret);
		std::string e = s ? std::string(s) : std::string("");
		if (s) {
			JS_FreeCString(impl->ctx, s);
		}
		JS_FreeValue(impl->ctx, ret);
		if (!e.empty()) {
			return e;
		}
	}
	std::string e = impl->last_error;
	impl->last_error.clear();
	return e;
}

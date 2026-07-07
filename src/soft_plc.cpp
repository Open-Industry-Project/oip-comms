#include "soft_plc.h"

extern "C" {
#include "quickjs.h"
}

struct SoftPlcEngine::Impl {
	JSRuntime *rt = nullptr;
	JSContext *ctx = nullptr;
	JSValue api = JS_UNDEFINED; // globalThis.OipPlc
	std::string last_error;

	JSValue call(const char *method, int argc, JSValue *argv) {
		if (!JS_IsObject(api)) {
			last_error = "soft_plc: bundle not loaded";
			return JS_UNDEFINED;
		}
		JSValue fn = JS_GetPropertyStr(ctx, api, method);
		JSValue ret = JS_Call(ctx, fn, api, argc, argv);
		JS_FreeValue(ctx, fn);
		if (JS_IsException(ret)) {
			capture_exception();
			JS_FreeValue(ctx, ret);
			return JS_UNDEFINED;
		}
		return ret;
	}

	void capture_exception() {
		JSValue ex = JS_GetException(ctx);
		const char *msg = JS_ToCString(ctx, ex);
		last_error = msg ? std::string(msg) : std::string("soft_plc: unknown JS exception");
		if (msg) {
			JS_FreeCString(ctx, msg);
		}
		JS_FreeValue(ctx, ex);
	}

	std::string to_std(JSValue v, const char *fallback) {
		const char *s = JS_ToCString(ctx, v);
		std::string out = s ? std::string(s) : std::string(fallback);
		if (s) {
			JS_FreeCString(ctx, s);
		}
		return out;
	}
};

SoftPlcEngine::SoftPlcEngine() {
	impl = new Impl();
	impl->rt = JS_NewRuntime();
	impl->ctx = JS_NewContext(impl->rt);
}

SoftPlcEngine::~SoftPlcEngine() {
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

bool SoftPlcEngine::load_bundle(const std::string &js) {
	JSValue r = JS_Eval(impl->ctx, js.c_str(), js.size(), "oip-plc.js", JS_EVAL_TYPE_GLOBAL);
	if (JS_IsException(r)) {
		impl->capture_exception();
		JS_FreeValue(impl->ctx, r);
		return false;
	}
	JS_FreeValue(impl->ctx, r);

	JSValue global = JS_GetGlobalObject(impl->ctx);
	if (!JS_IsUndefined(impl->api)) {
		JS_FreeValue(impl->ctx, impl->api);
	}
	impl->api = JS_GetPropertyStr(impl->ctx, global, "OipPlc");
	JS_FreeValue(impl->ctx, global);
	if (!JS_IsObject(impl->api)) {
		impl->last_error = "soft_plc: globalThis.OipPlc missing after eval";
		return false;
	}
	return true;
}

int SoftPlcEngine::create(const std::string &source) {
	JSValue arg = JS_NewStringLen(impl->ctx, source.c_str(), source.size());
	JSValue ret = impl->call("create", 1, &arg);
	JS_FreeValue(impl->ctx, arg);
	int32_t handle = 0;
	JS_ToInt32(impl->ctx, &handle, ret);
	JS_FreeValue(impl->ctx, ret);
	return handle;
}

std::string SoftPlcEngine::meta(int handle) {
	JSValue arg = JS_NewInt32(impl->ctx, handle);
	JSValue ret = impl->call("meta", 1, &arg);
	JS_FreeValue(impl->ctx, arg);
	std::string out = impl->to_std(ret, "{}");
	JS_FreeValue(impl->ctx, ret);
	return out;
}

std::string SoftPlcEngine::step(int handle, const std::string &inputs_json, double dt) {
	JSValue argv[3];
	argv[0] = JS_NewInt32(impl->ctx, handle);
	argv[1] = JS_NewStringLen(impl->ctx, inputs_json.c_str(), inputs_json.size());
	argv[2] = JS_NewFloat64(impl->ctx, dt);
	JSValue ret = impl->call("step", 3, argv);
	JS_FreeValue(impl->ctx, argv[0]);
	JS_FreeValue(impl->ctx, argv[1]);
	JS_FreeValue(impl->ctx, argv[2]);
	std::string out = impl->to_std(ret, "{}");
	JS_FreeValue(impl->ctx, ret);
	return out;
}

std::string SoftPlcEngine::watch(int handle) {
	JSValue arg = JS_NewInt32(impl->ctx, handle);
	JSValue ret = impl->call("watch", 1, &arg);
	JS_FreeValue(impl->ctx, arg);
	std::string out = impl->to_std(ret, "{}");
	JS_FreeValue(impl->ctx, ret);
	return out;
}

void SoftPlcEngine::destroy(int handle) {
	JSValue arg = JS_NewInt32(impl->ctx, handle);
	JSValue ret = impl->call("destroy", 1, &arg);
	JS_FreeValue(impl->ctx, arg);
	JS_FreeValue(impl->ctx, ret);
}

std::string SoftPlcEngine::error() {
	if (JS_IsObject(impl->api)) {
		JSValue ret = impl->call("error", 0, nullptr);
		std::string e = impl->to_std(ret, "");
		JS_FreeValue(impl->ctx, ret);
		if (!e.empty()) {
			return e;
		}
	}
	std::string e = impl->last_error;
	impl->last_error.clear();
	return e;
}

#include "register_types.h"
#ifdef WEB_ENABLED
#include "oip_comms_web.h"
#else
#include "oip_comms.h"
#include "tcads_loader.h"
#include "behavior_runtime.h" // _scaffold/ (on CPPPATH) — behavior-VM spike
#endif

#include <gdextension_interface.h>
#include <godot_cpp/core/class_db.hpp>
#include <godot_cpp/core/defs.hpp>
#include <godot_cpp/godot.hpp>
#include <godot_cpp/classes/engine.hpp>

using namespace godot;

OIPComms *_oip_comms;

void initialize_gdextension_types(ModuleInitializationLevel p_level)
{
	if (p_level != MODULE_INITIALIZATION_LEVEL_SCENE) {
		return;
	}
	GDREGISTER_CLASS(OIPComms);
#ifndef WEB_ENABLED
	GDREGISTER_CLASS(BehaviorRuntime);
#endif

	_oip_comms = memnew(OIPComms);
	Engine::get_singleton()->register_singleton("OIPComms", _oip_comms);
}

void uninitialize_gdextension_types(ModuleInitializationLevel p_level) {
	if (p_level != MODULE_INITIALIZATION_LEVEL_SCENE) {
		return;
	}

	Engine::get_singleton()->unregister_singleton("OIPComms");
	memdelete(_oip_comms);
}

extern "C"
{
	// Initialization
	GDExtensionBool GDE_EXPORT oip_comms_init(GDExtensionInterfaceGetProcAddress p_get_proc_address, GDExtensionClassLibraryPtr p_library, GDExtensionInitialization *r_initialization)
	{
#ifndef WEB_ENABLED
		preload_tc_ads_dll();
#endif

		GDExtensionBinding::InitObject init_obj(p_get_proc_address, p_library, r_initialization);
		init_obj.register_initializer(initialize_gdextension_types);
		init_obj.register_terminator(uninitialize_gdextension_types);
		init_obj.set_minimum_library_initialization_level(MODULE_INITIALIZATION_LEVEL_SCENE);

		return init_obj.init();
	}
}

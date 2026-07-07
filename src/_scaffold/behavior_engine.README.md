# BehaviorEngine spike — getting a Godot part to run TS behavior

Goal: make one belt visibly convey cargo, driven by the portable TS `belt_driver` behavior in
`oip-behavior.js` (built in oip-web: `npm run build-embedded-behavior`). This is the bidirectional
counterpart of the soft_plc embedding. **`behavior_engine.{h,cpp}` is an unverified scaffold** — see
caveats at the bottom.

> Location: these files live in `src/_scaffold/` (outside `Glob("src/*.cpp")`) so they do **not**
> affect your oip-comms build until you opt them in (see Build).

## Minimal spike scope (keep it tiny)
- `__sim.setSurfaceVelocity(body, sub, v)` → set the belt's existing speed (no full PhysicsServer needed).
- `__sim.raycast(...)` → stub `return false` for the first run (the belt moving is the visible proof).
- `__tags` → a small `Dictionary` you set from GDScript (`run = true` to start the belt).

## Steps

1. **Bundle in** — copy `oip-web/dist-embedded/oip-behavior.js` → `Open-Industry-Project/oip-behavior.js`
   (the same way `oip-plc.js` is vendored), load it from `res://oip-behavior.js`.

2. **A Godot-bound wrapper that implements `SimHost`** (new `RefCounted` class, godot-cpp; behavior is
   physics-coupled so this lives sim-side, not in the comms transport):

   ```cpp
   // behavior_runtime.h (sketch) — register in register_types.cpp like other classes
   class BehaviorRuntime : public RefCounted, public SimHost {
     GDCLASS(BehaviorRuntime, RefCounted)
     BehaviorEngine engine;
     Dictionary tags;            // __tags backing store, settable from GDScript
   protected:
     static void _bind_methods();
   public:
     bool load(const String &bundle_path);                 // FileAccess -> engine.load_bundle
     int attach(const String &type, const Dictionary &params, const Array &bodies); // -> engine.create(JSON.stringify...)
     void tick(int handle, double dt) { engine.update(handle, dt); }
     void set_tag(const String &n, bool v) { tags[n] = v; }
     bool get_tag(const String &n) { return tags.get(n, false); }
     // SimHost:
     void set_surface_velocity(int body, int, double vx, double, double) override { emit_signal("surface_velocity", body, vx); }
     bool raycast(double,double,double,double,double,double,double) override { return false; } // spike stub
     bool tag_read_bool(const std::string &n) override { return (bool)tags.get(String(n.c_str()), false); }
     void tag_write_bool(const std::string &n, bool v) override { tags[String(n.c_str())] = v; }
   };
   ```
   In `attach`, call `engine.set_host(this)` and `engine.load_bundle(...)` once, then
   `engine.create(type, JSON::stringify(params), JSON::stringify(bodies))`.

3. **GDScript wiring** on (or beside) a belt:

   ```gdscript
   var rt := BehaviorRuntime.new()
   var handle := -1

   func _ready() -> void:
       rt.load("res://oip-behavior.js")
       rt.surface_velocity.connect(_on_surface_velocity)   # signal(body:int, vx:float)
       handle = rt.attach("belt_driver", {"speed": 0.5}, [0])  # body id 0 = this belt
       rt.set_tag("run", true)                               # start it

   func _physics_process(dt: float) -> void:
       rt.tick(handle, dt)                                   # behavior calls __sim/__tags here

   func _on_surface_velocity(_body: int, vx: float) -> void:
       speed = vx                                            # belt_conveyor's existing speed -> surface velocity
   ```

   Result: with `run = true`, the TS `belt_driver` sets the surface velocity each tick → the belt
   conveys cargo. That is a Godot part driven by a TypeScript behavior module. Flip `run` off → it stops.

## Build (opt-in — not auto-built)
`src/_scaffold/` is outside the non-recursive `Glob("src/*.cpp")` in `SConstruct`, so nothing here
compiles until you add it. When ready, extend the `sources` list (~line 128):

```python
sources = (
    Glob("src/*.cpp")
    + Glob("src/_scaffold/*.cpp")   # behavior engine scaffold
    + Glob("ads/AdsLib/*.cpp")
    + ...
)
```

`behavior_engine.cpp` finds `quickjs.h` via the existing `thirdparty/quickjs/` CPPPATH and
`behavior_engine.h` from its own dir. When you include the header from `oip_comms.cpp` (or a sim
module), use `_scaffold/behavior_engine.h` or add `src/_scaffold/` to CPPPATH. Build with
`oip-comms/venv/Scripts/scons.exe platform=windows target=template_debug`; expect to fix
QuickJS-API/compile details on the first build (see caveats).

## Caveats (read before trusting this)
- **Not compiled by me.** The QuickJS calls mirror `soft_plc.cpp` (proven) plus standard
  `JS_NewCFunction`/`JS_NewObject`/`JS_SetContextOpaque` usage — verify signatures against the
  vendored quickjs-ng. `JS_ToBool` return convention and `JS_FreeValue` placement are the usual gotchas.
- **Spike-only marshaling:** vectors are read field-by-field; tags are bool-only; `raycast` returns a
  bare object. A real adapter returns `{body,point,normal,distance}` and handles numeric tags.
- **Lifetime:** the `__sim`/`__tags` C functions read `SimHost*` via `JS_GetContextOpaque` — the host
  must outlive the engine (the `BehaviorRuntime` owns both, so it does).

## Alternative: GodotJS (no C++)
If you'd rather not touch the C++/build, with GodotJS the host is ~50 lines of TS/GDScript: load
`oip-behavior.js`, set `globalThis.__sim`/`__tags` to objects that call Godot APIs, drive
`OipBehavior.create/update`. Same bundle, same contract — only the host implementation differs.

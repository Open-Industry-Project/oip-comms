#pragma once

#include <string>

class SoftPlcEngine {
	struct Impl;
	Impl *impl;

public:
	SoftPlcEngine();
	~SoftPlcEngine();

	// Evaluate the engine bundle (dist-embedded/oip-plc.js). Returns false + sets error() on failure.
	bool load_bundle(const std::string &js);

	int create(const std::string &source);            // >0 handle, or 0 + error()
	std::string meta(int handle);                       // {"inputs":[...],"outputs":[...]}
	std::string step(int handle, const std::string &inputs_json, double dt); // {tag: value, ...}
	std::string watch(int handle);                      // {var: value, ...} of EVERY variable
	void destroy(int handle);
	std::string error();                                // last error (cleared on read)
};

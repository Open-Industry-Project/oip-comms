#include "oip_comms_web.h"

#include <godot_cpp/classes/engine.hpp>
#include <godot_cpp/classes/file_access.hpp>
#include <godot_cpp/classes/json.hpp>
#include <godot_cpp/classes/object.hpp>
#include <godot_cpp/classes/scene_tree.hpp>
#include <godot_cpp/classes/time.hpp>
#include <godot_cpp/core/class_db.hpp>
#include <godot_cpp/variant/packed_string_array.hpp>
#include <godot_cpp/variant/utility_functions.hpp>

#include <cassert>
#include <cstring>

using namespace godot;

static constexpr uint64_t MQTT_RETRY_MS = 2000;
static constexpr uint64_t MQTT_PING_MS = 15000;
static constexpr uint64_t MQTT_CONNECT_TIMEOUT_MS = 5000;

static Object *javascript_bridge() {
	Engine *engine = Engine::get_singleton();
	return engine != nullptr && engine->has_singleton("JavaScriptBridge")
			? engine->get_singleton("JavaScriptBridge") : nullptr;
}

void OIPComms::mqtt_string(std::vector<uint8_t> &out, const String &value) {
	CharString utf8 = value.utf8();
	const uint16_t length = (uint16_t)utf8.length();
	out.push_back((uint8_t)(length >> 8));
	out.push_back((uint8_t)length);
	out.insert(out.end(), utf8.get_data(), utf8.get_data() + length);
}

PackedByteArray OIPComms::mqtt_packet(uint8_t header, const std::vector<uint8_t> &body) {
	std::vector<uint8_t> bytes = { header };
	size_t remaining = body.size();
	do {
		uint8_t digit = remaining % 128;
		remaining /= 128;
		if (remaining > 0) digit |= 0x80;
		bytes.push_back(digit);
	} while (remaining > 0);
	bytes.insert(bytes.end(), body.begin(), body.end());

	PackedByteArray packet;
	packet.resize((int64_t)bytes.size());
	std::memcpy(packet.ptrw(), bytes.data(), bytes.size());
	return packet;
}

bool OIPComms::mqtt_remaining_length(const PackedByteArray &packet, int &offset, int &length) {
	int multiplier = 1;
	length = 0;
	for (int count = 0; count < 4 && offset < packet.size(); count++) {
		uint8_t digit = packet[offset++];
		length += (digit & 0x7f) * multiplier;
		if ((digit & 0x80) == 0) return true;
		multiplier *= 128;
	}
	return false;
}

String OIPComms::web_mqtt_url(const String &gateway) {
	String url = gateway.strip_edges();
	if (url.is_empty()) return String();
	String lower = url.to_lower();
	if (lower.begins_with("ws://") || lower.begins_with("wss://")) return url;
	return lower.contains("://") ? String() : "ws://" + url;
}

void OIPComms::mqtt_connect(TagGroup &group, const String &name) {
	String url = web_mqtt_url(group.gateway);
	if (url.is_empty()) {
		group.unsupported_reported = true;
		print("Web MQTT group '" + name + "' needs a ws:// or wss:// broker URL; browsers cannot use raw MQTT TCP", true);
		return;
	}

	group.socket.instantiate();
	PackedStringArray protocols;
	protocols.append("mqtt");
	group.socket->set_supported_protocols(protocols);
	Error error = group.socket->connect_to_url(url);
	if (error != OK) {
		group.socket = Ref<WebSocketPeer>();
		group.retry_after_ms = Time::get_singleton()->get_ticks_msec() + MQTT_RETRY_MS;
		print("MQTT WebSocket connection could not start for '" + name + "' (error " + itos(error) + ")", true);
	}
}

void OIPComms::mqtt_subscribe(TagGroup &group) {
	for (auto &entry : group.tags) {
		if (entry.second.subscribed) continue;
		std::vector<uint8_t> body;
		const uint16_t id = group.packet_id++;
		body.push_back((uint8_t)(id >> 8));
		body.push_back((uint8_t)id);
		mqtt_string(body, entry.first);
		body.push_back(0); // QoS 0
		if (group.socket->send(mqtt_packet(0x82, body)) != OK) {
			print("MQTT WebSocket subscribe failed for '" + entry.first + "'", true);
			return;
		}
		entry.second.subscribed = true;
	}
	if (!group.initialized) {
		group.initialized = true;
		group.initialized_signal_emitted = true;
		for (const auto &candidate : tag_groups) {
			if (&candidate.second == &group) {
				emit_signal("tag_group_initialized", candidate.first);
				break;
			}
		}
	}
}

void OIPComms::mqtt_receive(TagGroup &group, const PackedByteArray &packet) {
	int start = 0;
	while (start < packet.size()) {
		const uint8_t header = packet[start++];
		int remaining = 0;
		if (!mqtt_remaining_length(packet, start, remaining) || remaining < 0 || start + remaining > packet.size()) {
			print("MQTT WebSocket received a malformed packet", true);
			return;
		}
		const int end = start + remaining;
		const uint8_t type = header >> 4;

		if (type == 2) { // CONNACK
			if (remaining != 2 || packet[start + 1] != 0) {
				const int code = remaining == 2 ? packet[start + 1] : -1;
				print("MQTT broker rejected WebSocket connection (code " + itos(code) + ")", true);
				group.socket->close(1000, "MQTT CONNACK rejected");
				return;
			}
			group.connected = true;
			mqtt_subscribe(group);
		} else if (type == 3 && remaining >= 2) { // PUBLISH
			const int topic_size = (packet[start] << 8) | packet[start + 1];
			int payload = start + 2 + topic_size;
			const int qos = (header >> 1) & 0x03;
			if (qos > 0) payload += 2;
			if (topic_size >= 0 && payload <= end) {
				String topic = String::utf8((const char *)packet.ptr() + start + 2, topic_size);
				auto found = group.tags.find(topic);
				if (found != group.tags.end()) {
					found->second.value.assign(packet.ptr() + payload, packet.ptr() + end);
					found->second.has_data = true;
				}
			}
		}
		group.last_packet_ms = Time::get_singleton()->get_ticks_msec();
		start = end;
	}
}

void OIPComms::mqtt_publish(TagGroup &group, const String &topic, const void *value, size_t size) {
	if (!group.connected || group.socket.is_null()) return;
	std::vector<uint8_t> body;
	mqtt_string(body, topic);
	const uint8_t *bytes = reinterpret_cast<const uint8_t *>(value);
	body.insert(body.end(), bytes, bytes + size);
	if (group.socket->send(mqtt_packet(0x30, body)) != OK)
		print("MQTT WebSocket publish failed for '" + topic + "'", true);
}

bool OIPComms::soft_load_bundle(TagGroup &group) {
	if (!soft_loaded_bundle.is_empty()) {
		if (soft_loaded_bundle == group.gateway) return true;
		print("Web soft PLC groups must use the same engine bundle", true);
		return false;
	}
	Ref<FileAccess> file = FileAccess::open(group.gateway, FileAccess::READ);
	if (file.is_null()) {
		print("soft_plc: cannot open bundle '" + group.gateway + "'", true);
		return false;
	}
	Object *bridge = javascript_bridge();
	if (bridge == nullptr) {
		print("soft_plc: JavaScriptBridge unavailable", true);
		return false;
	}
	bridge->call("eval", file->get_as_text(), true);
	if (!(bool)bridge->call("eval", "typeof OipPlc === 'object'", true)) {
		print("soft_plc: OipPlc missing after loading '" + group.gateway + "'", true);
		return false;
	}
	// ponytail: one shared browser runtime; isolate bundles per group if multiple engines are ever needed.
	soft_loaded_bundle = group.gateway;
	return true;
}

void OIPComms::soft_destroy(TagGroup &group) {
	Object *bridge = javascript_bridge();
	if (group.soft_handle > 0 && bridge != nullptr)
		bridge->call("eval", "OipPlc.destroy(" + itos(group.soft_handle) + ")", true);
	group.soft_handle = 0;
	group.initialized = false;
	group.soft_last_physics_frame = 0;
	group.soft_watch.clear();
}

void OIPComms::soft_process(TagGroup &group, const String &name) {
	if (group.soft_program.is_empty()) return;
	Object *bridge = javascript_bridge();
	if (!soft_load_bundle(group) || bridge == nullptr) return;

	if (!group.initialized) {
		Variant handle = bridge->call("eval", "OipPlc.create(" + JSON::stringify(group.soft_program) + ")", true);
		group.soft_handle = (int64_t)handle;
		if (group.soft_handle <= 0) {
			print("soft_plc compile error: " + String(bridge->call("eval", "OipPlc.error()", true)), true);
			return;
		}
		Variant meta_value = JSON::parse_string(String(bridge->call("eval", "OipPlc.meta(" + itos(group.soft_handle) + ")", true)));
		if (meta_value.get_type() == Variant::DICTIONARY) {
			Dictionary meta = meta_value;
			Array inputs = meta.get("inputs", Array());
			Array outputs = meta.get("outputs", Array());
			for (int i = 0; i < inputs.size(); i++) {
				auto tag = group.tags.find(inputs[i]);
				if (tag != group.tags.end()) tag->second.is_input = true;
			}
			for (int i = 0; i < outputs.size(); i++) {
				auto tag = group.tags.find(outputs[i]);
				if (tag != group.tags.end()) tag->second.is_output = true;
			}
		}
		group.initialized = true;
	}

	Dictionary inputs;
	for (const auto &tag : group.tags) {
		if (tag.second.is_input && tag.second.soft_value.get_type() != Variant::NIL)
			inputs[tag.first] = tag.second.soft_value;
	}
	const uint64_t physics_frame = Engine::get_singleton()->get_physics_frames();
	const double ticks = Engine::get_singleton()->get_physics_ticks_per_second();
	const double delta = group.soft_last_physics_frame == 0 || ticks <= 0
			? group.polling_interval / 1000.0
			: (physics_frame - group.soft_last_physics_frame) / ticks;
	group.soft_last_physics_frame = physics_frame;
	String inputs_json = JSON::stringify(inputs);
	String expression = "OipPlc.step(" + itos(group.soft_handle) + "," + JSON::stringify(inputs_json) + "," + String::num(delta) + ")";
	Variant output_value = JSON::parse_string(String(bridge->call("eval", expression, true)));
	if (output_value.get_type() == Variant::DICTIONARY) {
		Dictionary outputs = output_value;
		Array keys = outputs.keys();
		for (int i = 0; i < keys.size(); i++) {
			auto tag = group.tags.find(keys[i]);
			if (tag != group.tags.end()) tag->second.soft_value = outputs[keys[i]];
		}
	}
	if (group.soft_watch_enabled) {
		Variant watch = JSON::parse_string(String(bridge->call("eval", "OipPlc.watch(" + itos(group.soft_handle) + ")", true)));
		group.soft_watch = watch.get_type() == Variant::DICTIONARY ? Dictionary(watch) : Dictionary();
	}
	(void)name;
}

void OIPComms::reset_connection(TagGroup &group) {
	if (group.protocol == "soft_plc") soft_destroy(group);
	group.socket = Ref<WebSocketPeer>();
	group.mqtt_connect_sent = false;
	group.connected = false;
	group.initialized = false;
	group.initialized_signal_emitted = false;
	group.retry_after_ms = Time::get_singleton()->get_ticks_msec() + MQTT_RETRY_MS;
	for (auto &tag : group.tags) {
		tag.second.subscribed = false;
		tag.second.has_data = false;
	}
}

void OIPComms::process() {
	const uint64_t now_usec = Time::get_singleton()->get_ticks_usec();
	const double delta_ms = last_ticks == 0 ? 0.0 : (now_usec - last_ticks) / 1000.0;
	last_ticks = now_usec;
	if (!enable_comms || !sim_running) return;

	const uint64_t now_ms = now_usec / 1000;
	for (auto &entry : tag_groups) {
		TagGroup &group = entry.second;
		group.elapsed += delta_ms;
		bool poll_due = false;
		if (group.elapsed >= group.polling_interval) {
			emit_signal("tag_group_polled", entry.first);
			group.elapsed = 0.0;
			poll_due = true;
		}

		if (group.protocol == "soft_plc") {
			if (!group.initialized_signal_emitted) {
				group.initialized_signal_emitted = true;
				emit_signal("tag_group_initialized", entry.first);
			}
			if (poll_due) soft_process(group, entry.first);
			continue;
		}
		if (group.protocol != "mqtt") {
			if (!group.unsupported_reported) {
				group.unsupported_reported = true;
				print("Protocol '" + group.protocol + "' cannot connect directly from a browser; use an MQTT-over-WebSocket or HTTP/WebSocket gateway", true, false);
			}
			continue;
		}

		if (group.socket.is_null()) {
			if (!group.unsupported_reported && now_ms >= group.retry_after_ms) mqtt_connect(group, entry.first);
			continue;
		}

		group.socket->poll();
		WebSocketPeer::State state = group.socket->get_ready_state();
		if (state == WebSocketPeer::STATE_OPEN && !group.mqtt_connect_sent) {
			std::vector<uint8_t> body = { 0, 4, 'M', 'Q', 'T', 'T', 4 };
			String user;
			String password;
			int colon = group.credentials.find(":");
			if (colon < 0) user = group.credentials.strip_edges();
			else {
				user = group.credentials.substr(0, colon);
				password = group.credentials.substr(colon + 1);
			}
			uint8_t flags = 0x02; // clean session
			if (!user.is_empty() || !password.is_empty()) flags |= 0x80;
			if (!password.is_empty()) flags |= 0x40;
			body.push_back(flags);
			body.push_back(0);
			body.push_back(20); // keep alive seconds
			mqtt_string(body, group.client_id);
			if (!user.is_empty() || !password.is_empty()) mqtt_string(body, user);
			if (!password.is_empty()) mqtt_string(body, password);
			if (group.socket->send(mqtt_packet(0x10, body)) == OK) {
				group.mqtt_connect_sent = true;
				group.last_packet_ms = now_ms;
			}
		}

		while (group.socket->get_available_packet_count() > 0)
			mqtt_receive(group, group.socket->get_packet());

		if (group.mqtt_connect_sent && !group.connected && now_ms - group.last_packet_ms >= MQTT_CONNECT_TIMEOUT_MS)
			group.socket->close(1000, "MQTT CONNACK timeout");
		if (group.connected && now_ms - group.last_packet_ms >= MQTT_PING_MS) {
			group.socket->send(mqtt_packet(0xc0, {}));
			group.last_packet_ms = now_ms;
		}
		if (group.socket->get_ready_state() == WebSocketPeer::STATE_CLOSED) {
			String detail = "MQTT WebSocket closed for '" + entry.first + "' (code "
					+ itos(group.socket->get_close_code()) + ")";
			String reason = group.socket->get_close_reason();
			if (!reason.is_empty()) detail += ": " + reason;
			print(detail, true);
			reset_connection(group);
		}
	}
}

bool OIPComms::tag_exists(const String &group, const String &tag) const {
	auto found_group = tag_groups.find(group);
	return found_group != tag_groups.end() && found_group->second.tags.find(tag) != found_group->second.tags.end();
}

void OIPComms::print(const Variant &message, bool error, bool fatal) {
	String text = "OIPComms: " + String(message);
	if (error) {
		UtilityFunctions::printerr(text);
		last_error = message;
		if (fatal) {
			if (!comms_error) emit_signal("comms_error");
			comms_error = true;
		}
	} else if (enable_log) {
		UtilityFunctions::print(text);
	}
}

void OIPComms::register_tag_group(const String name, int polling_interval, const String protocol,
		const String gateway, const String path, const String cpu) {
	if (name.is_empty()) return;
	if (tag_groups.find(name) == tag_groups.end()) tag_group_order.push_back(name);
	TagGroup group;
	group.polling_interval = polling_interval;
	group.elapsed = polling_interval;
	group.protocol = protocol.to_lower();
	group.gateway = gateway;
	group.client_id = path.strip_edges();
	if (group.client_id.is_empty()) group.client_id = "oip_web_" + name + "_" + itos(Time::get_singleton()->get_ticks_msec());
	group.credentials = cpu;
	tag_groups[name] = group;
}

bool OIPComms::register_tag(const String group, const String tag, int data_type) {
	auto found = tag_groups.find(group);
	if (found == tag_groups.end() || tag.is_empty()) return false;
	if (found->second.protocol == "mqtt" && (tag.contains("+") || tag.contains("#"))) {
		print("MQTT tag '" + tag + "' must be a concrete topic", true);
		return false;
	}
	found->second.tags[tag].data_type = data_type;
	return true;
}

void OIPComms::set_soft_plc_program(const String group, const String source) {
	auto found = tag_groups.find(group);
	if (found == tag_groups.end() || found->second.protocol != "soft_plc") return;
	soft_destroy(found->second);
	found->second.soft_program = source;
}

void OIPComms::set_soft_plc_watch_enabled(const String group, bool enabled) {
	auto found = tag_groups.find(group);
	if (found != tag_groups.end() && found->second.protocol == "soft_plc")
		found->second.soft_watch_enabled = enabled;
}

Dictionary OIPComms::get_soft_plc_watch(const String group) {
	auto found = tag_groups.find(group);
	return found != tag_groups.end() ? found->second.soft_watch : Dictionary();
}

String OIPComms::compile_soft_plc(const String group, const String source) {
	auto found = tag_groups.find(group);
	if (found == tag_groups.end()) return "tag group '" + group + "' does not exist";
	if (!soft_load_bundle(found->second)) return last_error;
	Object *bridge = javascript_bridge();
	if (bridge == nullptr) return "JavaScriptBridge unavailable";
	Variant result = bridge->call("eval", "OipPlc.create(" + JSON::stringify(source) + ")", true);
	int handle = (int64_t)result;
	if (handle <= 0) return String(bridge->call("eval", "OipPlc.error()", true));
	bridge->call("eval", "OipPlc.destroy(" + itos(handle) + ")", true);
	return "";
}

bool OIPComms::get_enable_comms() { return enable_comms; }
void OIPComms::set_enable_comms(bool value) { enable_comms = value; }
bool OIPComms::get_sim_running() { return sim_running; }
void OIPComms::set_sim_running(bool value) {
	sim_running = value;
	if (value) {
		SceneTree *tree = Object::cast_to<SceneTree>(Engine::get_singleton()->get_main_loop());
		if (tree != nullptr && !tree->is_connected("process_frame", callable_mp(this, &OIPComms::process)))
			tree->connect("process_frame", callable_mp(this, &OIPComms::process));
		comms_error = false;
		last_error = "";
	} else {
		for (auto &entry : tag_groups) reset_connection(entry.second);
	}
}
bool OIPComms::get_enable_log() { return enable_log; }
void OIPComms::set_enable_log(bool value) { enable_log = value; emit_signal("enable_comms_changed"); }
String OIPComms::get_comms_error() { return last_error; }

Array OIPComms::get_tag_groups() {
	Array result;
	for (const String &name : tag_group_order) result.push_back(name);
	return result;
}

void OIPComms::clear_tag_groups() {
	if (sim_running) return;
	for (auto &entry : tag_groups) soft_destroy(entry.second);
	tag_groups.clear();
	tag_group_order.clear();
}

bool OIPComms::browse_connect(const String endpoint) {
	(void)endpoint;
	print("OPC UA browsing is unavailable in browsers because opc.tcp is a raw TCP protocol; use a WebSocket/HTTP gateway", true);
	return false;
}
void OIPComms::browse_disconnect() {}
bool OIPComms::browse_is_session_alive() { return false; }
Array OIPComms::browse_children(const String node_id) { (void)node_id; return Array(); }
Dictionary OIPComms::browse_node_info(const String node_id) { (void)node_id; return Dictionary(); }

#define OIP_RW(name, type) \
	type OIPComms::read_##name(const String group, const String tag) { \
		if (!enable_comms || !sim_running || !tag_exists(group, tag)) return (type)0; \
		const TagGroup &found = tag_groups[group]; const Tag &stored = found.tags.find(tag)->second; \
		if (found.protocol == "soft_plc") return stored.soft_value.get_type() == Variant::NIL ? (type)0 : (type)stored.soft_value; \
		if (!stored.has_data || stored.value.size() < sizeof(type)) return (type)0; \
		type value; std::memcpy(&value, stored.value.data(), sizeof(value)); return value; \
	} \
	void OIPComms::write_##name(const String group, const String tag, type value) { \
		if (!enable_comms || !sim_running || !tag_exists(group, tag)) return; \
		TagGroup &found = tag_groups[group]; \
		if (found.protocol == "mqtt") mqtt_publish(found, tag, &value, sizeof(value)); \
		else if (found.protocol == "soft_plc" && !found.tags[tag].is_output) found.tags[tag].soft_value = value; \
	}
OIP_RW(bit, bool)
OIP_RW(uint64, uint64_t)
OIP_RW(int64, int64_t)
OIP_RW(uint32, uint32_t)
OIP_RW(int32, int32_t)
OIP_RW(uint16, uint16_t)
OIP_RW(int16, int16_t)
OIP_RW(uint8, uint8_t)
OIP_RW(int8, int8_t)
OIP_RW(float64, double)
OIP_RW(float32, float)
#undef OIP_RW

void OIPComms::_bind_methods() {
	ClassDB::bind_method(D_METHOD("register_tag_group", "tag_group_name", "polling_interval", "protocol", "gateway", "path", "cpu"), &OIPComms::register_tag_group);
	ClassDB::bind_method(D_METHOD("set_soft_plc_program", "tag_group_name", "source"), &OIPComms::set_soft_plc_program);
	ClassDB::bind_method(D_METHOD("set_soft_plc_watch_enabled", "tag_group_name", "enabled"), &OIPComms::set_soft_plc_watch_enabled);
	ClassDB::bind_method(D_METHOD("get_soft_plc_watch", "tag_group_name"), &OIPComms::get_soft_plc_watch);
	ClassDB::bind_method(D_METHOD("compile_soft_plc", "tag_group_name", "source"), &OIPComms::compile_soft_plc);
	ClassDB::bind_method(D_METHOD("register_tag", "tag_group_name", "tag_name", "data_type"), &OIPComms::register_tag, DEFVAL(TAG_TYPE_BOOL));
	ClassDB::bind_method(D_METHOD("set_enable_comms", "value"), &OIPComms::set_enable_comms);
	ClassDB::bind_method(D_METHOD("get_enable_comms"), &OIPComms::get_enable_comms);
	ClassDB::bind_method(D_METHOD("set_sim_running", "value"), &OIPComms::set_sim_running);
	ClassDB::bind_method(D_METHOD("get_sim_running"), &OIPComms::get_sim_running);
	ClassDB::bind_method(D_METHOD("set_enable_log", "value"), &OIPComms::set_enable_log);
	ClassDB::bind_method(D_METHOD("get_enable_log"), &OIPComms::get_enable_log);
	ClassDB::bind_method(D_METHOD("get_comms_error"), &OIPComms::get_comms_error);
#define OIP_BIND_RW(name) \
	ClassDB::bind_method(D_METHOD("read_" #name, "tag_group_name", "tag_name"), &OIPComms::read_##name); \
	ClassDB::bind_method(D_METHOD("write_" #name, "tag_group_name", "tag_name", "value"), &OIPComms::write_##name);
	OIP_BIND_RW(bit)
	OIP_BIND_RW(uint64)
	OIP_BIND_RW(int64)
	OIP_BIND_RW(uint32)
	OIP_BIND_RW(int32)
	OIP_BIND_RW(uint16)
	OIP_BIND_RW(int16)
	OIP_BIND_RW(uint8)
	OIP_BIND_RW(int8)
	OIP_BIND_RW(float64)
	OIP_BIND_RW(float32)
#undef OIP_BIND_RW
	ClassDB::bind_method(D_METHOD("get_tag_groups"), &OIPComms::get_tag_groups);
	ClassDB::bind_method(D_METHOD("clear_tag_groups"), &OIPComms::clear_tag_groups);
	ClassDB::bind_method(D_METHOD("browse_connect", "endpoint"), &OIPComms::browse_connect);
	ClassDB::bind_method(D_METHOD("browse_disconnect"), &OIPComms::browse_disconnect);
	ClassDB::bind_method(D_METHOD("browse_is_alive"), &OIPComms::browse_is_session_alive);
	ClassDB::bind_method(D_METHOD("browse_children", "node_id"), &OIPComms::browse_children);
	ClassDB::bind_method(D_METHOD("browse_node_info", "node_id"), &OIPComms::browse_node_info);
	ADD_SIGNAL(MethodInfo("tag_group_polled", PropertyInfo(Variant::STRING, "tag_group_name")));
	ADD_SIGNAL(MethodInfo("tag_group_initialized", PropertyInfo(Variant::STRING, "tag_group_name")));
	ADD_SIGNAL(MethodInfo("opc_ua_session_recovered", PropertyInfo(Variant::STRING, "tag_group_name")));
	ADD_SIGNAL(MethodInfo("comms_error"));
	ADD_SIGNAL(MethodInfo("tag_groups_registered"));
	ADD_SIGNAL(MethodInfo("enable_comms_changed"));
	BIND_ENUM_CONSTANT(TAG_TYPE_BOOL);
	BIND_ENUM_CONSTANT(TAG_TYPE_INT8);
	BIND_ENUM_CONSTANT(TAG_TYPE_UINT8);
	BIND_ENUM_CONSTANT(TAG_TYPE_INT16);
	BIND_ENUM_CONSTANT(TAG_TYPE_UINT16);
	BIND_ENUM_CONSTANT(TAG_TYPE_INT32);
	BIND_ENUM_CONSTANT(TAG_TYPE_UINT32);
	BIND_ENUM_CONSTANT(TAG_TYPE_INT64);
	BIND_ENUM_CONSTANT(TAG_TYPE_UINT64);
	BIND_ENUM_CONSTANT(TAG_TYPE_FLOAT32);
	BIND_ENUM_CONSTANT(TAG_TYPE_FLOAT64);
}

OIPComms::OIPComms() {
#ifndef NDEBUG
	std::vector<uint8_t> test_body(130);
	PackedByteArray test_packet = mqtt_packet(0x30, test_body);
	int test_offset = 1;
	int test_length = 0;
	assert(test_packet[1] == 0x82 && test_packet[2] == 0x01);
	assert(mqtt_remaining_length(test_packet, test_offset, test_length) && test_length == 130 && test_offset == 3);
	assert(web_mqtt_url("").is_empty());
	assert(web_mqtt_url("localhost:1883") == "ws://localhost:1883");
	assert(web_mqtt_url("wss://broker.example/mqtt") == "wss://broker.example/mqtt");
	assert(web_mqtt_url("tcp://localhost:1883").is_empty());
#endif
}

OIPComms::~OIPComms() {
	SceneTree *tree = Object::cast_to<SceneTree>(Engine::get_singleton()->get_main_loop());
	if (tree != nullptr && tree->is_connected("process_frame", callable_mp(this, &OIPComms::process)))
		tree->disconnect("process_frame", callable_mp(this, &OIPComms::process));
}

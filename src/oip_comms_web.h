#ifndef OIP_COMMS_WEB_H
#define OIP_COMMS_WEB_H

#include <godot_cpp/classes/node.hpp>
#include <godot_cpp/classes/web_socket_peer.hpp>
#include <godot_cpp/core/binder_common.hpp>
#include <godot_cpp/variant/array.hpp>
#include <godot_cpp/variant/dictionary.hpp>
#include <godot_cpp/variant/packed_byte_array.hpp>
#include <godot_cpp/variant/variant.hpp>

#include <cstddef>
#include <cstdint>
#include <map>
#include <vector>

namespace godot {

class OIPComms : public Node {
	GDCLASS(OIPComms, Node)

	struct Tag {
		bool subscribed = false;
		bool has_data = false;
		std::vector<uint8_t> value;
		Variant soft_value;
		int data_type = 0;
		bool is_input = false;
		bool is_output = false;
	};

	struct TagGroup {
		int polling_interval = 100;
		double elapsed = 0.0;
		String protocol;
		String gateway;
		String client_id;
		String credentials;
		std::map<String, Tag> tags;
		Ref<WebSocketPeer> socket;
		bool mqtt_connect_sent = false;
		bool connected = false;
		bool initialized = false;
		bool initialized_signal_emitted = false;
		bool unsupported_reported = false;
		uint16_t packet_id = 1;
		uint64_t retry_after_ms = 0;
		uint64_t last_packet_ms = 0;
		String soft_program;
		int soft_handle = 0;
		bool soft_watch_enabled = false;
		Dictionary soft_watch;
		uint64_t soft_last_physics_frame = 0;
	};

	std::map<String, TagGroup> tag_groups;
	std::vector<String> tag_group_order;
	bool enable_comms = true;
	bool sim_running = false;
	bool enable_log = false;
	bool comms_error = false;
	String last_error;
	uint64_t last_ticks = 0;
	String soft_loaded_bundle;

	static PackedByteArray mqtt_packet(uint8_t header, const std::vector<uint8_t> &body);
	static void mqtt_string(std::vector<uint8_t> &out, const String &value);
	static bool mqtt_remaining_length(const PackedByteArray &packet, int &offset, int &length);
	static String web_mqtt_url(const String &gateway);
	void mqtt_connect(TagGroup &group, const String &name);
	void mqtt_subscribe(TagGroup &group);
	void mqtt_receive(TagGroup &group, const PackedByteArray &packet);
	void mqtt_publish(TagGroup &group, const String &topic, const void *value, size_t size);
	bool soft_load_bundle(TagGroup &group);
	void soft_process(TagGroup &group, const String &name);
	void soft_destroy(TagGroup &group);
	void reset_connection(TagGroup &group);
	void print(const Variant &message, bool error = false, bool fatal = true);
	bool tag_exists(const String &group, const String &tag) const;

protected:
	static void _bind_methods();

public:
	enum TagType {
		TAG_TYPE_BOOL,
		TAG_TYPE_INT8,
		TAG_TYPE_UINT8,
		TAG_TYPE_INT16,
		TAG_TYPE_UINT16,
		TAG_TYPE_INT32,
		TAG_TYPE_UINT32,
		TAG_TYPE_INT64,
		TAG_TYPE_UINT64,
		TAG_TYPE_FLOAT32,
		TAG_TYPE_FLOAT64,
	};

	void register_tag_group(const String name, int polling_interval, const String protocol,
			const String gateway, const String path, const String cpu);
	bool register_tag(const String group, const String tag, int data_type = TAG_TYPE_BOOL);
	void set_soft_plc_program(const String group, const String source);
	void set_soft_plc_watch_enabled(const String group, bool enabled);
	Dictionary get_soft_plc_watch(const String group);
	String compile_soft_plc(const String group, const String source);
	bool get_enable_comms();
	void set_enable_comms(bool value);
	bool get_sim_running();
	void set_sim_running(bool value);
	bool get_enable_log();
	void set_enable_log(bool value);
	String get_comms_error();
	Array get_tag_groups();
	void clear_tag_groups();
	bool browse_connect(const String endpoint);
	void browse_disconnect();
	bool browse_is_session_alive();
	Array browse_children(const String node_id);
	Dictionary browse_node_info(const String node_id);
	void process();

#define OIP_DECLARE_FUNC(name, type) \
	type read_##name(const String group, const String tag); \
	void write_##name(const String group, const String tag, type value);
	OIP_DECLARE_FUNC(bit, bool)
	OIP_DECLARE_FUNC(uint64, uint64_t)
	OIP_DECLARE_FUNC(int64, int64_t)
	OIP_DECLARE_FUNC(uint32, uint32_t)
	OIP_DECLARE_FUNC(int32, int32_t)
	OIP_DECLARE_FUNC(uint16, uint16_t)
	OIP_DECLARE_FUNC(int16, int16_t)
	OIP_DECLARE_FUNC(uint8, uint8_t)
	OIP_DECLARE_FUNC(int8, int8_t)
	OIP_DECLARE_FUNC(float64, double)
	OIP_DECLARE_FUNC(float32, float)
#undef OIP_DECLARE_FUNC

	OIPComms();
	~OIPComms();
};

} // namespace godot

VARIANT_ENUM_CAST(godot::OIPComms::TagType);

#endif

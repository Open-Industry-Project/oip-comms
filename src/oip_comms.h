#ifndef OIP_COMMS_H
#define OIP_COMMS_H

#include "libplctag.h"
#include "open62541.h"
#include "S7Com.hpp"

#include <godot_cpp/classes/node.hpp>
#include <godot_cpp/classes/thread.hpp>
#include <map>
#include <string>
#include <vector>
#include <queue>

#include "oip_blocking_queue.h"

struct AdsTagGroupImpl;
struct RtdeTagGroupImpl;

namespace godot {

class OIPComms : public Node {
	GDCLASS(OIPComms, Node)

private:
	int timeout = 5000;
	double startup_timer = 0.0f;
	double register_wait_time = 500.0f;
	bool comms_error = false;
	String last_error = "";

	struct PlcTag {
		bool initialized = false;
		int32_t tag_pointer = -1;
		int elem_count;

		// tag becomes dirty when a write happens before the next polled read
		// TBD - in the future expose an API so that "immediate reads" can occur
		// a little tricky with the current blocking queue/thread implementation
		bool dirty;
	};

	struct OpcUaTag {
		bool initialized = false;
		UA_NodeId node_id;
		UA_Variant value;
	};

	struct TagGroup {
		int polling_interval;
		double time;
		size_t init_count;
		bool init_count_emitted;
		bool has_error = false;

		String protocol;

		// gateway is a multi-purpose field:
		//   PLC:    IP address of the PLC, e.g. "192.168.1.200"
		//   OPC UA: server endpoint URL, e.g. "opc.tcp://192.168.56.104:62541"
		//   ADS:    remote PLC IPv4 address, e.g. "192.168.1.10"
		String gateway;

		// path:
		//   PLC:    rack/slot number, e.g. "1,2"
		//   OPC UA: unused (namespace is part of the tag name, e.g. "ns=2;i=5")
		//   ADS:    remote AmsNetId, e.g. "5.34.142.165.1.1"
		String path;

		// cpu:
		//   PLC:    CPU type string passed to libplctag
		//   ADS:    AMS port as a decimal string (e.g. "851" for the PLC runtime)
		String cpu;
		std::map<String, PlcTag> plc_tags;

		UA_Client *client;
		std::map<String, OpcUaTag> opc_ua_tags;

		// Defined in oip_comms.cpp; AdsLib pulls winsock2.h, which can't reach this header.
		AdsTagGroupImpl *ads_impl;

		RtdeTagGroupImpl *rtde_impl;
	};
	std::map<String, TagGroup> tag_groups;
	std::vector<String> tag_group_order;

	struct WriteRequest {
		uint8_t instruction;
		String tag_group_name;
		String tag_name;
		Variant value;
	};
	std::queue<WriteRequest> write_queue;

	Ref<Thread> work_thread;
	bool work_thread_running = true;

	Ref<Thread> watchdog_thread;
	bool watchdog_thread_running = true;

	OIPBlockingQueue tag_group_queue;

	UA_Client *browse_client = nullptr;

	uint64_t last_ticks = 0;

	bool scene_signals_set = false;

	bool enable_comms = true;
	bool sim_running = false;

	bool enable_log = false;

	void watchdog();
	void process_work();

	void process_tag_group(const String &tag_group_name);
	void process_plc_tag_group(const String &tag_group_name);
	void process_opc_ua_tag_group(const String &tag_group_name);
	void process_ads_tag_group(const String &tag_group_name);
	void process_rtde_tag_group(const String &tag_group_name);

	bool init_plc_tag(const String &tag_group_name, const String &tag_name);

	bool init_opc_ua_client(const String &tag_group_name);
	bool init_opc_ua_tag(const String &tag_group_name, const String &tag_path);

	bool init_ads_device(const String &tag_group_name);
	bool init_ads_tag(const String &tag_group_name, const String &tag_name);

	bool init_rtde_session(const String &tag_group_name);

	bool opc_ua_client_connected(const String &tag_group_name);
	bool ads_device_connected(const String &tag_group_name);
	bool rtde_session_started(const String &tag_group_name);
	bool tag_group_exists(const String &tag_group_name);
	bool tag_exists(const String &tag_group_name, const String &tag_name);

	void queue_tag_group(const String &tag_group_name);

	void flush_all_writes();
	void flush_one_write();

	void process_write(const WriteRequest &write_req);

	// process individual PLC read
	bool process_plc_read(PlcTag &tag, const String &tag_name);

	void opc_write(const String &tag_group_name, const String &tag_path);

#define OIP_DECLARE_OPC_SET(a)void opc_tag_set_##a(const String &tag_group_name, const String &tag_path, const godot::Variant value);

	OIP_DECLARE_OPC_SET(bit)
	OIP_DECLARE_OPC_SET(uint64)
	OIP_DECLARE_OPC_SET(int64)
	OIP_DECLARE_OPC_SET(uint32)
	OIP_DECLARE_OPC_SET(int32)
	OIP_DECLARE_OPC_SET(uint16)
	OIP_DECLARE_OPC_SET(int16)
	OIP_DECLARE_OPC_SET(uint8)
	OIP_DECLARE_OPC_SET(int8)
	OIP_DECLARE_OPC_SET(float64)
	OIP_DECLARE_OPC_SET(float32)

#define OIP_DECLARE_ADS_SET(a)void ads_tag_set_##a(const String &tag_group_name, const String &tag_path, const godot::Variant value);

	OIP_DECLARE_ADS_SET(bit)
	OIP_DECLARE_ADS_SET(uint64)
	OIP_DECLARE_ADS_SET(int64)
	OIP_DECLARE_ADS_SET(uint32)
	OIP_DECLARE_ADS_SET(int32)
	OIP_DECLARE_ADS_SET(uint16)
	OIP_DECLARE_ADS_SET(int16)
	OIP_DECLARE_ADS_SET(uint8)
	OIP_DECLARE_ADS_SET(int8)
	OIP_DECLARE_ADS_SET(float64)
	OIP_DECLARE_ADS_SET(float32)

	void rtde_tag_set(const String &tag_group_name, const String &tag_path, const godot::Variant value);

	void cleanup_tag_groups();
	void cleanup_tag_group(const String &tag_group_name);

	void print(const Variant &message, bool error = false);

protected:
	static void _bind_methods();

public:
	void register_tag_group(const String p_tag_group_name, const int p_polling_interval, const String p_protocol, const String p_gateway, const String p_path, const String p_cpu);
	bool register_tag(const String p_tag_group_name, const String p_tag_name, const int p_elem_count);

	bool get_enable_comms();
	void set_enable_comms(bool value);

	bool get_sim_running();
	void set_sim_running(bool value);

	bool get_enable_log();
	void set_enable_log(bool value);

	String get_comms_error();

	Array get_tag_groups();

#define OIP_DECLARE_FUNC(a, b)                                          \
	b read_##a(const String p_tag_group_name, const String p_tag_name); \
	void write_##a(const String p_tag_group_name, const String p_tag_name, const b p_value);

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

	void clear_tag_groups();

	bool browse_connect(const String p_endpoint);
	void browse_disconnect();
	Array browse_children(const String p_node_id);
	Dictionary browse_node_info(const String p_node_id);

	void process();

	OIPComms();
	~OIPComms();
};

} //namespace godot

#endif

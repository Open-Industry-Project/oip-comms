#ifndef OIP_RTDE_CLIENT_H
#define OIP_RTDE_CLIENT_H

// Universal Robots Real-Time Data Exchange (RTDE v2) client.
// Wire format: [u16 BE size][u8 type][payload], size includes the 3-byte header.
// Lifecycle: open -> negotiate_version -> setup_outputs [-> setup_inputs]
//            -> start -> poll() loop / send_input() on writes.

#include <cstdint>
#include <string>
#include <vector>

namespace oip_rtde {

enum FieldType : uint8_t {
	FIELD_UNKNOWN = 0,
	FIELD_BOOL,
	FIELD_UINT8,
	FIELD_UINT32,
	FIELD_UINT64,
	FIELD_INT32,
	FIELD_DOUBLE,
	FIELD_VECTOR3D,
	FIELD_VECTOR6D,
	FIELD_VECTOR6INT32,
	FIELD_VECTOR6UINT32,
	FIELD_NOT_FOUND,
	FIELD_IN_USE,
};

size_t field_size(FieldType ft);
int field_arity(FieldType ft);
size_t field_scalar_size(FieldType ft);
FieldType field_element_type(FieldType ft);

FieldType parse_field_type(const std::string &s);
const char *field_type_name(FieldType ft);

class Client {
public:
	Client();
	~Client();

	Client(const Client &) = delete;
	Client &operator=(const Client &) = delete;

	bool open(const std::string &host, uint16_t port = 30004);
	void close();

	bool is_open() const { return socket_ != -1; }
	const std::string &last_error() const { return last_error_; }

	bool negotiate_version();

	bool setup_outputs(double frequency, const std::vector<std::string> &names,
			std::vector<FieldType> &types_out, uint8_t &recipe_id_out);
	bool setup_inputs(const std::vector<std::string> &names,
			std::vector<FieldType> &types_out, uint8_t &recipe_id_out);

	bool start();

	int poll(uint8_t &out_recipe_id, std::vector<uint8_t> &out_payload);

	bool send_input(uint8_t recipe_id, const uint8_t *payload, size_t payload_size);

private:
	intptr_t socket_ = -1;
	std::string last_error_;

	std::vector<uint8_t> recv_buf_;

	bool send_frame(uint8_t type, const uint8_t *payload, size_t payload_size);
	int parse_buffered_frame(uint8_t &out_type, std::vector<uint8_t> &out_payload);
	int try_extract_frame(uint8_t &out_type, std::vector<uint8_t> &out_payload);
	bool recv_frame(uint8_t &out_type, std::vector<uint8_t> &out_payload);

	bool set_blocking(bool blocking);

	void store_socket_error(const char *prefix);
};

} // namespace oip_rtde

#endif

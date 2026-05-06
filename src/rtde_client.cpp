#include "rtde_client.h"

#include <cstring>
#include <cstdio>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <winsock2.h>
#include <ws2tcpip.h>
typedef SOCKET native_socket_t;
#define OIP_INVALID_SOCKET INVALID_SOCKET
#define OIP_SOCKET_ERROR SOCKET_ERROR
#define oip_close_socket(s) closesocket(s)
#define oip_last_socket_error() WSAGetLastError()
#define OIP_EWOULDBLOCK WSAEWOULDBLOCK
#else
#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <netdb.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <unistd.h>
typedef int native_socket_t;
#define OIP_INVALID_SOCKET (-1)
#define OIP_SOCKET_ERROR (-1)
#define oip_close_socket(s) ::close(s)
#define oip_last_socket_error() errno
#define OIP_EWOULDBLOCK EAGAIN
#endif

// Suppress SIGPIPE: Linux supports MSG_NOSIGNAL on send(); macOS/BSD don't,
// they need SO_NOSIGPIPE on the socket; Windows has no SIGPIPE concept.
#if defined(MSG_NOSIGNAL)
#define OIP_SEND_FLAGS MSG_NOSIGNAL
#else
#define OIP_SEND_FLAGS 0
#endif

namespace oip_rtde {

namespace {

#ifdef _WIN32
struct WsaInit {
	WsaInit() {
		WSADATA wsa;
		WSAStartup(MAKEWORD(2, 2), &wsa);
	}
	~WsaInit() {
		WSACleanup();
	}
};
WsaInit g_wsa;
#endif

constexpr uint8_t MT_REQUEST_PROTOCOL_VERSION = 86;     // 'V'
constexpr uint8_t MT_DATA_PACKAGE = 85;                 // 'U'
constexpr uint8_t MT_CONTROL_PACKAGE_SETUP_OUTPUTS = 79;// 'O'
constexpr uint8_t MT_CONTROL_PACKAGE_SETUP_INPUTS = 73; // 'I'
constexpr uint8_t MT_CONTROL_PACKAGE_START = 83;        // 'S'

void store_be64(uint8_t *p, uint64_t v) {
	p[0] = (uint8_t)(v >> 56); p[1] = (uint8_t)(v >> 48);
	p[2] = (uint8_t)(v >> 40); p[3] = (uint8_t)(v >> 32);
	p[4] = (uint8_t)(v >> 24); p[5] = (uint8_t)(v >> 16);
	p[6] = (uint8_t)(v >> 8);  p[7] = (uint8_t)v;
}

std::string join_csv(const std::vector<std::string> &names) {
	std::string out;
	for (size_t i = 0; i < names.size(); ++i) {
		if (i) out += ",";
		out += names[i];
	}
	return out;
}

void split_csv(const std::string &s, std::vector<std::string> &out) {
	out.clear();
	size_t start = 0;
	for (size_t i = 0; i <= s.size(); ++i) {
		if (i == s.size() || s[i] == ',') {
			out.emplace_back(s.substr(start, i - start));
			start = i + 1;
		}
	}
}

} // namespace

size_t field_size(FieldType ft) {
	switch (ft) {
		case FIELD_BOOL:
		case FIELD_UINT8: return 1;
		case FIELD_UINT32:
		case FIELD_INT32: return 4;
		case FIELD_UINT64:
		case FIELD_DOUBLE: return 8;
		case FIELD_VECTOR3D: return 24;
		case FIELD_VECTOR6D: return 48;
		case FIELD_VECTOR6INT32:
		case FIELD_VECTOR6UINT32: return 24;
		default: return 0;
	}
}

int field_arity(FieldType ft) {
	switch (ft) {
		case FIELD_VECTOR3D: return 3;
		case FIELD_VECTOR6D:
		case FIELD_VECTOR6INT32:
		case FIELD_VECTOR6UINT32: return 6;
		case FIELD_UNKNOWN:
		case FIELD_NOT_FOUND: return 0;
		default: return 1;
	}
}

size_t field_scalar_size(FieldType ft) {
	switch (ft) {
		case FIELD_BOOL:
		case FIELD_UINT8: return 1;
		case FIELD_UINT32:
		case FIELD_INT32:
		case FIELD_VECTOR6INT32:
		case FIELD_VECTOR6UINT32: return 4;
		case FIELD_UINT64:
		case FIELD_DOUBLE:
		case FIELD_VECTOR3D:
		case FIELD_VECTOR6D: return 8;
		default: return 0;
	}
}

FieldType field_element_type(FieldType ft) {
	switch (ft) {
		case FIELD_VECTOR3D:
		case FIELD_VECTOR6D: return FIELD_DOUBLE;
		case FIELD_VECTOR6INT32: return FIELD_INT32;
		case FIELD_VECTOR6UINT32: return FIELD_UINT32;
		default: return ft;
	}
}

FieldType parse_field_type(const std::string &s) {
	if (s == "BOOL") return FIELD_BOOL;
	if (s == "UINT8") return FIELD_UINT8;
	if (s == "UINT32") return FIELD_UINT32;
	if (s == "UINT64") return FIELD_UINT64;
	if (s == "INT32") return FIELD_INT32;
	if (s == "DOUBLE") return FIELD_DOUBLE;
	if (s == "VECTOR3D") return FIELD_VECTOR3D;
	if (s == "VECTOR6D") return FIELD_VECTOR6D;
	if (s == "VECTOR6INT32") return FIELD_VECTOR6INT32;
	if (s == "VECTOR6UINT32") return FIELD_VECTOR6UINT32;
	if (s == "NOT_FOUND") return FIELD_NOT_FOUND;
	if (s == "IN_USE") return FIELD_IN_USE;
	return FIELD_UNKNOWN;
}

const char *field_type_name(FieldType ft) {
	switch (ft) {
		case FIELD_BOOL: return "BOOL";
		case FIELD_UINT8: return "UINT8";
		case FIELD_UINT32: return "UINT32";
		case FIELD_UINT64: return "UINT64";
		case FIELD_INT32: return "INT32";
		case FIELD_DOUBLE: return "DOUBLE";
		case FIELD_VECTOR3D: return "VECTOR3D";
		case FIELD_VECTOR6D: return "VECTOR6D";
		case FIELD_VECTOR6INT32: return "VECTOR6INT32";
		case FIELD_VECTOR6UINT32: return "VECTOR6UINT32";
		case FIELD_NOT_FOUND: return "NOT_FOUND";
		case FIELD_IN_USE: return "IN_USE";
		default: return "UNKNOWN";
	}
}

Client::Client() {}

Client::~Client() {
	close();
}

void Client::store_socket_error(const char *prefix) {
	char buf[64];
	std::snprintf(buf, sizeof(buf), "%s (errno %d)", prefix, (int)oip_last_socket_error());
	last_error_ = buf;
}

bool Client::open(const std::string &host, uint16_t port) {
	close();
	last_error_.clear();
	recv_buf_.clear();

	native_socket_t s = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
	if (s == OIP_INVALID_SOCKET) {
		store_socket_error("RTDE socket() failed");
		return false;
	}

	sockaddr_in addr;
	std::memset(&addr, 0, sizeof(addr));
	addr.sin_family = AF_INET;
	addr.sin_port = htons(port);

	if (inet_pton(AF_INET, host.c_str(), &addr.sin_addr) != 1) {
		addrinfo hints;
		std::memset(&hints, 0, sizeof(hints));
		hints.ai_family = AF_INET;
		hints.ai_socktype = SOCK_STREAM;
		addrinfo *res = nullptr;
		if (getaddrinfo(host.c_str(), nullptr, &hints, &res) != 0 || res == nullptr) {
			oip_close_socket(s);
			last_error_ = "RTDE: failed to resolve host '" + host + "'";
			return false;
		}
		addr.sin_addr = ((sockaddr_in *)res->ai_addr)->sin_addr;
		freeaddrinfo(res);
	}

	// Non-blocking connect with a 5s select timeout, otherwise a typo'd gateway
	// stalls the work thread for the OS-default ~75s SYN retry window and pauses
	// all tag-group polling.
#ifdef _WIN32
	u_long nb = 1;
	ioctlsocket(s, FIONBIO, &nb);
#else
	int fl = fcntl(s, F_GETFL, 0);
	fcntl(s, F_SETFL, fl | O_NONBLOCK);
#endif

	bool connected = connect(s, (sockaddr *)&addr, sizeof(addr)) != OIP_SOCKET_ERROR;
	if (!connected) {
		int err = oip_last_socket_error();
		bool in_progress =
#ifdef _WIN32
				err == WSAEWOULDBLOCK;
#else
				err == EINPROGRESS;
#endif
		if (!in_progress) {
			store_socket_error("RTDE connect() failed");
			oip_close_socket(s);
			return false;
		}
		fd_set wfds;
		FD_ZERO(&wfds);
		FD_SET(s, &wfds);
		timeval tv;
		tv.tv_sec = 5;
		tv.tv_usec = 0;
		int sel = select((int)s + 1, nullptr, &wfds, nullptr, &tv);
		if (sel <= 0) {
			last_error_ = sel == 0
					? std::string("RTDE: connect timeout to '") + host + "'"
					: "RTDE: select() during connect failed";
			oip_close_socket(s);
			return false;
		}
		int sockerr = 0;
#ifdef _WIN32
		int errlen = sizeof(sockerr);
#else
		socklen_t errlen = sizeof(sockerr);
#endif
		if (getsockopt(s, SOL_SOCKET, SO_ERROR, (char *)&sockerr, &errlen) < 0
				|| sockerr != 0) {
			char buf[96];
			std::snprintf(buf, sizeof(buf),
					"RTDE connect() failed (errno %d)", sockerr);
			last_error_ = buf;
			oip_close_socket(s);
			return false;
		}
	}

	// Restore blocking; the handshake recv_frame uses SO_RCVTIMEO instead.
#ifdef _WIN32
	nb = 0;
	ioctlsocket(s, FIONBIO, &nb);
#else
	fcntl(s, F_SETFL, fl);
#endif

	// Disable Nagle so input data packages aren't delayed by 40ms.
	int yes = 1;
	setsockopt(s, IPPROTO_TCP, TCP_NODELAY, (const char *)&yes, sizeof(yes));
#ifdef SO_NOSIGPIPE
	// macOS/BSD: per-socket SIGPIPE suppression (Linux uses MSG_NOSIGNAL on send()).
	setsockopt(s, SOL_SOCKET, SO_NOSIGPIPE, &yes, sizeof(yes));
#endif

	socket_ = (intptr_t)s;
	return true;
}

void Client::close() {
	if (socket_ != -1) {
		oip_close_socket((native_socket_t)socket_);
		socket_ = -1;
	}
	recv_buf_.clear();
}

bool Client::set_blocking(bool blocking) {
	if (socket_ == -1) return false;
#ifdef _WIN32
	u_long mode = blocking ? 0 : 1;
	if (ioctlsocket((native_socket_t)socket_, FIONBIO, &mode) != 0) {
		store_socket_error("RTDE ioctlsocket failed");
		return false;
	}
#else
	int flags = fcntl((int)socket_, F_GETFL, 0);
	if (flags == -1) {
		store_socket_error("RTDE fcntl(F_GETFL) failed");
		return false;
	}
	flags = blocking ? (flags & ~O_NONBLOCK) : (flags | O_NONBLOCK);
	if (fcntl((int)socket_, F_SETFL, flags) == -1) {
		store_socket_error("RTDE fcntl(F_SETFL) failed");
		return false;
	}
#endif
	return true;
}

bool Client::send_frame(uint8_t type, const uint8_t *payload, size_t payload_size) {
	if (socket_ == -1) return false;
	const size_t total = 3 + payload_size;
	if (total > 0xFFFF) {
		last_error_ = "RTDE frame too large";
		return false;
	}
	std::vector<uint8_t> buf(total);
	uint16_t size_be = htons((uint16_t)total);
	std::memcpy(buf.data(), &size_be, 2);
	buf[2] = type;
	if (payload_size) std::memcpy(buf.data() + 3, payload, payload_size);

	size_t sent = 0;
	while (sent < total) {
		int n = send((native_socket_t)socket_, (const char *)(buf.data() + sent),
				(int)(total - sent), OIP_SEND_FLAGS);
		if (n <= 0) {
			store_socket_error("RTDE send() failed");
			return false;
		}
		sent += (size_t)n;
	}
	return true;
}

int Client::parse_buffered_frame(uint8_t &out_type, std::vector<uint8_t> &out_payload) {
	if (recv_buf_.size() < 3) return 0;
	uint16_t size_be;
	std::memcpy(&size_be, recv_buf_.data(), 2);
	uint16_t size = ntohs(size_be);
	if (size < 3) {
		last_error_ = "RTDE: malformed frame size";
		return -1;
	}
	if (recv_buf_.size() < size) return 0;

	out_type = recv_buf_[2];
	out_payload.assign(recv_buf_.begin() + 3, recv_buf_.begin() + size);
	recv_buf_.erase(recv_buf_.begin(), recv_buf_.begin() + size);
	return 1;
}

int Client::try_extract_frame(uint8_t &out_type, std::vector<uint8_t> &out_payload) {
	if (socket_ == -1) return -1;

	uint8_t chunk[4096];
	for (;;) {
		int n = recv((native_socket_t)socket_, (char *)chunk, sizeof(chunk), 0);
		if (n > 0) {
			recv_buf_.insert(recv_buf_.end(), chunk, chunk + n);
			continue;
		}
		if (n == 0) {
			last_error_ = "RTDE: peer closed connection";
			return -1;
		}
		int err = oip_last_socket_error();
		if (err == OIP_EWOULDBLOCK
#ifndef _WIN32
				|| err == EWOULDBLOCK
#endif
		) {
			break;
		}
		store_socket_error("RTDE recv() failed");
		return -1;
	}

	return parse_buffered_frame(out_type, out_payload);
}

bool Client::recv_frame(uint8_t &out_type, std::vector<uint8_t> &out_payload) {
	if (socket_ == -1) return false;
	if (!set_blocking(true)) return false;

#ifdef _WIN32
	DWORD tv = 5000;
	setsockopt((native_socket_t)socket_, SOL_SOCKET, SO_RCVTIMEO,
			(const char *)&tv, sizeof(tv));
#else
	timeval tv;
	tv.tv_sec = 5;
	tv.tv_usec = 0;
	setsockopt((int)socket_, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
#endif

	// One blocking recv per iteration; parse_buffered_frame returns as soon as
	// a full frame is available. recv-until-WOULDBLOCK is wrong here because on
	// a blocking socket WOULDBLOCK only arrives after the SO_RCVTIMEO elapses.
	uint8_t chunk[4096];
	for (;;) {
		int r = parse_buffered_frame(out_type, out_payload);
		if (r == 1) return true;
		if (r == -1) return false;
		int n = recv((native_socket_t)socket_, (char *)chunk, sizeof(chunk), 0);
		if (n <= 0) {
			store_socket_error("RTDE recv() failed during handshake");
			return false;
		}
		recv_buf_.insert(recv_buf_.end(), chunk, chunk + n);
	}
}

bool Client::negotiate_version() {
	uint8_t payload[2];
	uint16_t version_be = htons(2);
	std::memcpy(payload, &version_be, 2);
	if (!send_frame(MT_REQUEST_PROTOCOL_VERSION, payload, 2)) return false;

	uint8_t type = 0;
	std::vector<uint8_t> reply;
	if (!recv_frame(type, reply)) return false;
	if (type != MT_REQUEST_PROTOCOL_VERSION || reply.size() < 1) {
		last_error_ = "RTDE: unexpected reply to protocol version request";
		return false;
	}
	if (reply[0] != 1) {
		last_error_ = "RTDE: controller rejected protocol version 2";
		return false;
	}
	return true;
}

bool Client::setup_outputs(double frequency, const std::vector<std::string> &names,
		std::vector<FieldType> &types_out, uint8_t &recipe_id_out) {
	const std::string csv = join_csv(names);
	std::vector<uint8_t> payload(8 + csv.size());

	uint64_t bits;
	std::memcpy(&bits, &frequency, sizeof(bits));
	store_be64(payload.data(), bits);
	std::memcpy(payload.data() + 8, csv.data(), csv.size());

	if (!send_frame(MT_CONTROL_PACKAGE_SETUP_OUTPUTS, payload.data(), payload.size()))
		return false;

	uint8_t type = 0;
	std::vector<uint8_t> reply;
	if (!recv_frame(type, reply)) return false;
	if (type != MT_CONTROL_PACKAGE_SETUP_OUTPUTS || reply.size() < 1) {
		last_error_ = "RTDE: unexpected reply to setup outputs";
		return false;
	}
	recipe_id_out = reply[0];
	std::string types_str((const char *)(reply.data() + 1), reply.size() - 1);
	std::vector<std::string> type_strs;
	split_csv(types_str, type_strs);
	if (type_strs.size() != names.size()) {
		last_error_ = "RTDE: setup outputs returned " + std::to_string(type_strs.size())
				+ " types for " + std::to_string(names.size()) + " names";
		return false;
	}
	types_out.clear();
	types_out.reserve(type_strs.size());
	std::string not_found_list;
	std::string in_use_list;
	std::string unknown_list;
	for (size_t i = 0; i < type_strs.size(); ++i) {
		FieldType ft = parse_field_type(type_strs[i]);
		types_out.push_back(ft);
		std::string *bucket = nullptr;
		if (ft == FIELD_NOT_FOUND) bucket = &not_found_list;
		else if (ft == FIELD_IN_USE) bucket = &in_use_list;
		else if (ft == FIELD_UNKNOWN) bucket = &unknown_list;
		if (bucket) {
			if (!bucket->empty()) *bucket += ", ";
			*bucket += names[i];
			if (ft == FIELD_UNKNOWN) {
				*bucket += " (got '";
				*bucket += type_strs[i];
				*bucket += "')";
			}
		}
	}
	if (!not_found_list.empty()) {
		last_error_ = "RTDE: unknown output variable(s): " + not_found_list;
		return false;
	}
	if (!in_use_list.empty()) {
		last_error_ = "RTDE: output variable(s) already in use by another client: " + in_use_list;
		return false;
	}
	if (!unknown_list.empty()) {
		last_error_ = "RTDE: unrecognized output type from controller: " + unknown_list;
		return false;
	}
	return true;
}

bool Client::setup_inputs(const std::vector<std::string> &names,
		std::vector<FieldType> &types_out, uint8_t &recipe_id_out) {
	const std::string csv = join_csv(names);
	if (!send_frame(MT_CONTROL_PACKAGE_SETUP_INPUTS,
			(const uint8_t *)csv.data(), csv.size()))
		return false;

	uint8_t type = 0;
	std::vector<uint8_t> reply;
	if (!recv_frame(type, reply)) return false;
	if (type != MT_CONTROL_PACKAGE_SETUP_INPUTS || reply.size() < 1) {
		last_error_ = "RTDE: unexpected reply to setup inputs";
		return false;
	}
	recipe_id_out = reply[0];
	std::string types_str((const char *)(reply.data() + 1), reply.size() - 1);
	std::vector<std::string> type_strs;
	split_csv(types_str, type_strs);
	if (type_strs.size() != names.size()) {
		last_error_ = "RTDE: setup inputs returned " + std::to_string(type_strs.size())
				+ " types for " + std::to_string(names.size()) + " names";
		return false;
	}
	types_out.clear();
	types_out.reserve(type_strs.size());
	std::string not_found_list;
	std::string in_use_list;
	std::string unknown_list;
	for (size_t i = 0; i < type_strs.size(); ++i) {
		FieldType ft = parse_field_type(type_strs[i]);
		types_out.push_back(ft);
		std::string *bucket = nullptr;
		if (ft == FIELD_NOT_FOUND) bucket = &not_found_list;
		else if (ft == FIELD_IN_USE) bucket = &in_use_list;
		else if (ft == FIELD_UNKNOWN) bucket = &unknown_list;
		if (bucket) {
			if (!bucket->empty()) *bucket += ", ";
			*bucket += names[i];
			if (ft == FIELD_UNKNOWN) {
				*bucket += " (got '";
				*bucket += type_strs[i];
				*bucket += "')";
			}
		}
	}
	if (!not_found_list.empty()) {
		last_error_ = "RTDE: unknown input variable(s): " + not_found_list;
		return false;
	}
	if (!in_use_list.empty()) {
		last_error_ = "RTDE: input variable(s) already in use (typically by the controller's Fieldbus/PLC interface; try input_bit_register_64..127, input_int_register_24..47, or input_double_register_24..47 which are reserved for external RTDE clients): " + in_use_list;
		return false;
	}
	if (!unknown_list.empty()) {
		last_error_ = "RTDE: unrecognized input type from controller: " + unknown_list;
		return false;
	}
	return true;
}

bool Client::start() {
	if (!send_frame(MT_CONTROL_PACKAGE_START, nullptr, 0)) return false;
	uint8_t type = 0;
	std::vector<uint8_t> reply;
	if (!recv_frame(type, reply)) return false;
	if (type != MT_CONTROL_PACKAGE_START || reply.size() < 1 || reply[0] != 1) {
		last_error_ = "RTDE: controller rejected start";
		return false;
	}
	// Non-blocking lets poll() drain without stalling the work thread.
	return set_blocking(false);
}

int Client::poll(uint8_t &out_recipe_id, std::vector<uint8_t> &out_payload) {
	if (socket_ == -1) return -1;
	bool got_one = false;
	uint8_t latest_recipe_id = 0;
	std::vector<uint8_t> latest_payload;

	for (;;) {
		uint8_t type = 0;
		std::vector<uint8_t> payload;
		int r = try_extract_frame(type, payload);
		if (r < 0) {
			close();
			return -1;
		}
		if (r == 0) break;
		if (type == MT_DATA_PACKAGE && payload.size() >= 1) {
			latest_recipe_id = payload[0];
			latest_payload.assign(payload.begin() + 1, payload.end());
			got_one = true;
		}
	}

	if (got_one) {
		out_recipe_id = latest_recipe_id;
		out_payload = std::move(latest_payload);
		return 1;
	}
	return 0;
}

bool Client::send_input(uint8_t recipe_id, const uint8_t *payload, size_t payload_size) {
	std::vector<uint8_t> buf(1 + payload_size);
	buf[0] = recipe_id;
	if (payload_size) std::memcpy(buf.data() + 1, payload, payload_size);
	return send_frame(MT_DATA_PACKAGE, buf.data(), buf.size());
}

} // namespace oip_rtde

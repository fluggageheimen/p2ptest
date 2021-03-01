#pragma once

#include "tools.h"


struct WinSock {
	bool started = false;

	WinSock();
	~WinSock();

	static int getLastError();
};

struct net_uint16_t {
	uint16_t value;

	net_uint16_t(uint16_t val = 0) { set(val); }
	uint16_t get() const;
	void set(uint16_t val);
};

struct net_uint32_t {
	uint32_t value;

	net_uint32_t(uint32_t val = 0) { set(val); }
	uint32_t get() const;
	void set(uint32_t val);
};


struct NetAddress {
	char data[16];

public:
	bool operator==(NetAddress const& other) const noexcept { return memcmp(data, other.data, sizeof(data)) == 0; }
	bool operator!=(NetAddress const& other) const noexcept { return memcmp(data, other.data, sizeof(data)) != 0; }

	int getport() const;
	void setport(int port);

	static NetAddress any(uint16_t port);
	static NetAddress ipv4(uint32_t ip, uint16_t port);
	static NetAddress ipv4(uint8_t b1, uint8_t b2, uint8_t b3, uint8_t b4, uint16_t port);
};

std::string toString(NetAddress const& addr);


struct Socket {
	uintptr_t handle;

public:
	Socket();
	~Socket();

	bool bind(NetAddress const& address) const;
	bool valid() const;

	int sendto(NetAddress const& to, void const* buf, int len, int flags) const;
	int sendto(NetAddress const& to, void const* buf, int len, int flags, NetAddress* src) const;

	int recv(void* buf, int len, int flags) const;
	int recvfrom(void* buf, int len, int flags, NetAddress& from) const;

	NetAddress sockname() const;
};


int resolve_url(bool ipv4, char const* url, int port, NetAddress& address);
int resolve_url(bool ipv4, char const* url, NetAddress& address);

NetAddress resolve_local_address(Socket const& sock);
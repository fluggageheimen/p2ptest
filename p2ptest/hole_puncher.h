#pragma once

#include "socket.h"

#include <unordered_map>
#include <functional>
#include <vector>


namespace std {
	template<>
	struct hash<NetAddress> {
		size_t operator()(NetAddress const& address) const noexcept { return memhash(address.data, sizeof(address.data)); }
	};
}


class HolePuncher {
public:
	static const int RESEND_PERIOD_MS = 1000;

public:
	HolePuncher(bool autoping) : m_autoping(autoping), m_resendTimer(RESEND_PERIOD_MS) {}

	void addRemoteHost(PoolHandle id, Array<NetAddress const> addresses, size_t timeout, std::function<void(NetAddress const&)> callback);
	void delRemoteHost(PoolHandle id);

	void onPingReceived(Socket const& socket, NetAddress const& src, CBytes bytes);
	void onPongReceived(Socket const& socket, NetAddress const& src, CBytes bytes);

	void update(Socket const& socket);

private:
	static const uint32_t PING_MSGID = 0;
	static const uint32_t PONG_MSGID = 1;

	struct PendingHost {
		std::vector<NetAddress> addresses;
		NetAddress validAddress = NetAddress::any(0);
		std::function<void(NetAddress const&)> callback;
	};

private:
	bool m_autoping;
	Timer m_resendTimer;
	PoolMirror<PendingHost> m_pendings;
};
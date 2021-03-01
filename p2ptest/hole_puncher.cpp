#include "hole_puncher.h"
#include "log.h"


static int sendPingMsg(Socket const& socket, NetAddress const& target, int msgId, PoolHandle id)
{
#pragma pack(push, 1)
	struct PingMsg {
		net_uint16_t msgId;
		PoolHandle id;
	} pingMsg{ (uint16_t)msgId, id };
#pragma pack(pop)

	log(2, "Send [%s '%d/%d'] message to '%s'.", msgId == 0 ? "PING" : "PONG", id.index, id.nonce, toString(target).c_str());
	return socket.sendto(target, &pingMsg, sizeof(pingMsg), 0);
}


void HolePuncher::addRemoteHost(PoolHandle id, Array<NetAddress const> addresses, size_t timeout, std::function<void(NetAddress const&)> callback)
{
	if (id.isValid()) {
		auto& host = m_pendings.make(id);
		host.addresses = std::vector<NetAddress>(addresses.begin, addresses.end);
		host.callback = callback;
	}
}

void HolePuncher::delRemoteHost(PoolHandle id)
{
	if (m_pendings[id] != nullptr) {
		m_pendings.destroy(id);
	}
}

void HolePuncher::update(Socket const& socket)
{
	if (!m_resendTimer.shedule()) {
		return;
	}

	for (auto remoteHost : m_pendings) {
		if (!remoteHost) continue;

		if (m_autoping && remoteHost->addresses.empty()) {
			NetAddress fakeAddr = NetAddress::ipv4(8, 8, 8, 8, 48800);
			sendPingMsg(socket, fakeAddr, PING_MSGID, PoolHandle());
		}
		for (auto& address : remoteHost->addresses) {
			sendPingMsg(socket, address, PING_MSGID, remoteHost.handle);
		}
	}
}

void HolePuncher::onPingReceived(Socket const& socket, NetAddress const& src, CBytes bytes)
{
	PoolHandle id = *(PoolHandle*)(bytes.begin + 2);
	log(2, "Receive punching message from '%s': [PING '%u/%u'].", toString(src).c_str(), id.index, id.nonce);
	sendPingMsg(socket, src, PONG_MSGID, id);
}

void HolePuncher::onPongReceived(Socket const&, NetAddress const& src, CBytes bytes)
{
	PoolHandle id = *(PoolHandle*)(bytes.begin + 2);
	log(2, "Receive punching message from '%s': [PONG '%u/%u'].", toString(src).c_str(), id.index, id.nonce);

	auto host = m_pendings[id];
	if (host != nullptr) {
		host->validAddress = src;
		if (host->callback) {
			auto callback = std::move(host->callback);
			callback(src);
		}
	} else {
		log(2, "Recv [PONG]: peer [%u/%u] not found.", id.index, id.nonce);
	}
}
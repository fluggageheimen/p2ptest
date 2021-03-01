#include "host.h"
#include "log.h"


NetHost::NetHost(bool isMaster, Socket& socket, StunClient::Result const& natInfo, std::vector<INetClient*> clients)
	: m_master(isMaster), m_puncher(isMaster), m_socket(socket), m_clients(std::move(clients)), peersInfoChanged(true)
{
	m_selfAddresses[0] = natInfo.grayAddress;
	m_selfAddresses[1] = natInfo.whiteAddress;
	m_state.type = isMaster ? State::Idle : State::NotConnected;
}


void NetHost::queryPeerInfos(std::function<void(PeerId, PeerInfo const&)> const& callback)
{
    for (auto peer : m_peers) {
        if (!peer || peer->nickname[0] == '\0') continue;
        callback(peer.handle, *peer.value);
    }
}


PeerId NetHost::connect(Array<NetAddress const> addresses, std::function<void(int)> const& onFailed)
{
	if (addresses.empty() || m_state.type != State::NotConnected) {
		return PeerId();
	}

	NetAddress altAddress = addresses.count() > 1 ? addresses[1] : NetAddress::any(0);
	PeerId peerId = addPeer(NetAddress::any(0), addresses[0], altAddress);

	m_connFailedCallback = onFailed;
	m_puncher.addRemoteHost(peerId, addresses, CONNECT_INIT_TIMEOUT_MS, [this, peerId](NetAddress const& address){
		m_state.type = State::WaitResponce;
		m_state.waitResponce.address = address;
		m_state.waitResponce.failReason = CONNECTION_RESPONCE_TIMEOUT;
		m_state.waitResponce.timer = Timer(CONNECT_RETRY_TIMEOUT_MS);
		m_state.waitResponce.retries = 0;

		m_peers.at(peerId).addresses[0] = address;
		sendRequest(address);
	});
	return peerId;
}

void NetHost::onConnectionFailed(int reason)
{
	if (m_connFailedCallback) {
		m_connFailedCallback(reason);
	}
	for (auto peer : m_peers) {
		if (peer) delPeer(peer.handle);
	}
	m_state.type = State::NotConnected;
}

bool NetHost::send(PeerId dst, CBytes data)
{
	return false;
}

void NetHost::receive()
{
	NetAddress src;
	int count = m_socket.recvfrom(m_recvBuffer, sizeof(m_recvBuffer), 0, src);
	if (count >= 2) {
		CBytes bytes(m_recvBuffer, m_recvBuffer + count);

		net_uint16_t msgId = *(net_uint16_t*)m_recvBuffer;
		switch (msgId.get()) {
		case MsgId::Ping: m_puncher.onPingReceived(m_socket, src, bytes); break;
		case MsgId::Pong: m_puncher.onPongReceived(m_socket, src, bytes); break;
		case MsgId::Heartbeat: break;
		case MsgId::Request:  onRequest(src, bytes); break;
		case MsgId::Reject:   onReject(src, bytes); break;
		case MsgId::Response: onResponce(src, bytes); break;
		case MsgId::Join:     onJoin(src, bytes); break;
		case MsgId::JoinOk:   onJoinOk(src, bytes); break;
		case MsgId::PingA:    onPingA(src, bytes); break;
		default:
			log(2, "NetHost: invalid message received [id%u], skip. count=%d", msgId.get(), count);
		}
	}
}

void NetHost::update()
{
	receive();
	m_puncher.update(m_socket);

	if (m_state.type == State::WaitResponce) {
		if (m_state.waitResponce.timer.expired()) {
			if (m_state.waitResponce.retries != CONNECT_MAX_RETRIES) {
				log(2, "NetHost: connection responce not received, retrying to send request.");
				sendRequest(m_state.waitResponce.address);
				m_state.waitResponce.retries += 1;
			} else {
				log(2, "NetHost: connection failed, master not responded.");
				onConnectionFailed(m_state.waitResponce.failReason);
			}
		}
	}
}

PeerId NetHost::findPeerByAddress(NetAddress const& address)
{
	for (auto peer : m_peers) {
		if (peer && peer->addresses[0] == address) {
			return peer.handle;
		}
	}
	return PeerId();
}

PeerId NetHost::findPeerByNonce(int nonce)
{
	for (auto peer : m_peers) {
		if (peer && peer.handle.nonce == nonce) {
			return peer.handle;
		}
	}
	return PeerId();
}

PeerId NetHost::addPeer(NetAddress const& hostAddress, NetAddress const& grayAddress, NetAddress const& whiteAddress)
{
	peersInfoChanged = true;

	PeerInfo peerInfo;
	peerInfo.addresses[0] = hostAddress;
	peerInfo.addresses[1] = grayAddress;
	peerInfo.addresses[2] = whiteAddress;
	peerInfo.status = PeerInfo::Connecting;
    memset(peerInfo.nickname, 0, sizeof(peerInfo.nickname));
	return m_peers.alloc(std::move(peerInfo));
}

void NetHost::delPeer(PeerId peerId)
{
	peersInfoChanged = true;
	if (peerId.isValid()) {
		m_puncher.delRemoteHost(peerId);
		m_peers.dealloc(peerId);
	}
}

void NetHost::onReject(NetAddress const& src, CBytes data)
{
	log(2, "NetHost: receive 'Reject' message.");

	MsgResponceHeader* header = (MsgResponceHeader*)data.begin;
	if (header->length.get() == RejectReason::NotMaster) {
		onConnectionFailed(CONNECTION_NOT_MASTER);
	}
	if (header->length.get() == RejectReason::InvalidMessageFormat) {
		m_state.waitResponce.failReason = CORRUPTED_CHANNEL;
		m_state.waitResponce.timer.activate();
	}
}

void NetHost::onRequest(NetAddress const& src, CBytes data)
{
	log(2, "NetHost: receive 'Request' message from '%s'.", toString(src).c_str());

	if (!m_master || m_state.type != State::Idle) {
		MsgResponceHeader header{ MsgId::Reject, RejectReason::NotMaster };
		m_socket.sendto(src, &header, sizeof(header), 0);
		return;
	}
	if (data.size() != sizeof(MsgInitRequest)) {
		log(1, "NetHost: 'Request' message has invalid format.");
		MsgResponceHeader header{ MsgId::Reject, RejectReason::InvalidMessageFormat };
		m_socket.sendto(src, &header, sizeof(header), 0);
		return;
	}
    MsgInitRequest* request = (MsgInitRequest*)data.begin;

	delPeer(findPeerByAddress(src));
	PeerId peerId = addPeer(src, request->addresses[0], request->addresses[1]);
    memcpy(m_peers[peerId]->nickname, request->nickname, sizeof(request->nickname));
    m_peers[peerId]->status = PeerInfo::Status::Connected;

	uint16_t clientsCount = (uint16_t)m_peers.count() - 1;
	Buffer buffer(sizeof(MsgResponceHeader) + clientsCount * sizeof(MsgResponceFragment));
	MsgResponceHeader* header = (MsgResponceHeader*)buffer.begin;
	MsgResponceFragment* fragment = (MsgResponceFragment*)(header + 1);

	header->msgId = MsgId::Response;
	header->length = clientsCount;
    memcpy(header->nickname, nickname, sizeof(nickname));
	for (auto peer : m_peers) {
		if (peer && peerId != peer.handle) {
			log(2, "NetHost: send connected client '%s'.", toString(peer->addresses[0]).c_str());

			MsgRequest ping = *request;
			ping.msgId = MsgId::PingA;
			m_socket.sendto(peer->addresses[0], &ping, sizeof(ping), 0);

			memcpy(fragment->addresses, peer->addresses, sizeof(peer->addresses));
            memcpy(fragment->nickname, peer->nickname, sizeof(peer->nickname));
			fragment += 1;
		}
	}
	m_socket.sendto(src, buffer.begin, (int)buffer.size(), 0);
}


void NetHost::onResponce(NetAddress const& src, CBytes data)
{
	log(2, "NetHost: receive 'Response' message.");

	if (data.size() < sizeof(MsgResponceHeader)) {
		log(1, "NetHost: 'Response' message has invalid format.");
	}

	MsgResponceHeader* header = (MsgResponceHeader*)data.begin;
	if (data.size() != sizeof(MsgResponceHeader) + header->length.get() * sizeof(MsgResponceFragment)) {
		log(1, "NetHost: 'Response' message has invalid format.");
	}

	m_state.type = State::WaitClients;
	m_state.waitClients.count = header->length.get();
	m_puncher.delRemoteHost(findPeerByAddress(src));

	MsgResponceFragment* fragment = (MsgResponceFragment*)(header + 1);
	for (size_t i = 0; i < m_state.waitClients.count; ++i) {
		log(2, "NetHost: initiate connect to client '%s'.", toString(fragment->addresses[0]).c_str());

		PeerId peerId = addPeer(fragment->addresses[0], fragment->addresses[1], fragment->addresses[2]);
		m_puncher.addRemoteHost(peerId, fragment->addresses, CONNECT_INIT_TIMEOUT_MS, [this](const NetAddress& addr) {
			log(2, "NetHost: send 'Join' message to '%s'.", toString(addr).c_str());
			//sendShortMessage(addr, MsgId::Join);

            MsgJoin msgjoin;
            memcpy(msgjoin.nickname, nickname, sizeof(nickname));
            m_socket.sendto(addr, &msgjoin, sizeof(msgjoin), 0);

			m_state.waitClients.count -= 1;
			if (m_state.waitClients.count == 0) {
				m_state.type = State::Idle;
			}
		});

        memcpy(m_peers[peerId]->nickname, fragment->nickname, sizeof(fragment->nickname));
		fragment += 1;
	}

	setPeerStatus(src, PeerInfo::Connected);

    auto const peer = m_peers[findPeerByAddress(src)];
    if (peer != nullptr) {
        memcpy(peer->nickname, header->nickname, sizeof(header->nickname));
    }
}

void NetHost::onJoin(NetAddress const& src, CBytes data)
{
	log(2, "NetHost: receive 'Join' message from '%s'.", toString(src).c_str());

    MsgJoin* msg = (MsgJoin*)data.begin;
    if (data.size() != sizeof(MsgJoin)) {
        log(1, "NetHost: 'Join' message has invalid format.");
    }

	PeerId peerId = findPeerByAddress(src);
	if (!peerId.isValid()) {
		peerId = addPeer(src);
	}

	peersInfoChanged = true;
	m_peers.at(peerId).status = PeerInfo::Connected;
    memcpy(m_peers.at(peerId).nickname, msg->nickname, sizeof(msg->nickname));
	sendShortMessage(src, MsgId::JoinOk);
}

void NetHost::onJoinOk(NetAddress const& src, CBytes data)
{
	log(2, "NetHost: receive 'JoinOk' message from '%s'.", toString(src).c_str());

	m_puncher.delRemoteHost(findPeerByAddress(src));
	setPeerStatus(src, PeerInfo::Connected);
}

void NetHost::onPingA(NetAddress const& src, CBytes data)
{
	MsgRequest* request = (MsgRequest*)data.begin;
	log(2, "NetHost: receive 'PingA' message from '%s'. Ping host '%s'/'%s'", toString(src).c_str(),
		toString(request->addresses[0]).c_str(), toString(request->addresses[1]).c_str());
	
	PeerId peerId = addPeer(NetAddress::any(0), request->addresses[0], request->addresses[1]);
	m_puncher.addRemoteHost(peerId, request->addresses, 0, [this, peerId](NetAddress const& address){
		m_puncher.delRemoteHost(peerId);
	});
}

void NetHost::sendRequest(NetAddress const& target)
{
	log(2, "NetHost: send 'Request' message to '%s'.", toString(target).c_str());

    MsgInitRequest request;
	request.addresses[0] = m_selfAddresses[0];
	request.addresses[1] = m_selfAddresses[1];
    memcpy(request.nickname, nickname, sizeof(nickname));
	m_socket.sendto(target, &request, sizeof(request), 0);
}

void NetHost::sendShortMessage(NetAddress const& target, uint16_t msgid)
{	
	net_uint16_t msgjoin = msgid;
	m_socket.sendto(target, &msgjoin, sizeof(msgjoin), 0);
}

void NetHost::setPeerStatus(NetAddress const& addr, PeerInfo::Status status)
{
	auto peer = m_peers[findPeerByAddress(addr)];
	if (peer != nullptr) {
		peer->status = status;
	}
	peersInfoChanged = true;
}
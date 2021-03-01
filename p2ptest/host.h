#pragma once

#include "socket.h"
#include "stun_client.h"
#include "hole_puncher.h"
#include "tools.h"

#include <functional>
#include <vector>
#include <chrono>


using PeerId = PoolHandle;

struct INetClient {
	virtual void onPeerConnected(PeerId peer) = 0;
	virtual void onPeerDisconnected(PeerId peer) = 0;
	virtual void onMessageReceived(PeerId peer, int id, CBytes msg) = 0;
};


class NetHost {
public:
    char nickname[32];

	const static int CONNECT_MAX_RETRIES = 5;
	const static int CONNECT_INIT_TIMEOUT_MS = 1000;
	const static int CONNECT_RETRY_TIMEOUT_MS = 1000;

	enum ConnFailReason {
		INITIATE_CONNECTION_TIMEOUT,
		CONNECTION_RESPONCE_TIMEOUT,
		CONNECTION_NOT_MASTER,
		CORRUPTED_CHANNEL,
	};

	struct PeerInfo {
		enum Status { Connecting, Connected, Inactive, Offline, Disconnecting };
		char nickname[32];
		NetAddress addresses[3];
		Status status;
	};

	bool peersInfoChanged;

public:
	NetHost(bool isMaster, Socket& socket, StunClient::Result const& natInfo, std::vector<INetClient*> clients);

	PeerId connect(Array<NetAddress const> addresses, std::function<void(int)> const& onFailed);
	PeerId findPeerByAddress(NetAddress const& address);
	PeerId findPeerByNonce(int nonce);

	void queryPeerInfos(std::function<void(PeerId, PeerInfo const&)> const& callback);

	bool send(PeerId dst, CBytes data);
	void update();

private:
	struct MsgId {
		enum { Ping, Pong, Heartbeat, Request, Reject, Response, PingA, PingB, Join, JoinOk, Leave, Data };
	};
	struct RejectReason {
		enum { NotMaster, InvalidMessageFormat, AlreadyRegistered };
	};

	struct State {
		enum Type {
			Idle, NotConnected, WaitResponce, WaitClients,
		} type;

		union {
			struct {
				int failReason;
				NetAddress address;
				Timer timer;
				int retries;
			} waitResponce;
			struct {
				size_t count;
			} waitClients;
		};

		State() {}
	};

    struct MsgRequest {
        net_uint16_t msgId = { MsgId::Request };
        NetAddress addresses[2];
    };
    struct MsgInitRequest : MsgRequest {
        char nickname[32];
    };

	struct MsgResponceHeader {
		net_uint16_t msgId;
		net_uint16_t length;
        char nickname[32];
	};
	struct MsgResponceFragment {
		NetAddress addresses[3];
        char nickname[32];
	};

    struct MsgJoin {
        net_uint16_t msgId = { MsgId::Join };
        char nickname[32];
    };

private:
	bool m_master;
	State m_state;

	std::vector<INetClient*> m_clients;
	NetAddress m_selfAddresses[2];
	HolePuncher m_puncher;
	Socket& m_socket;

	Pool<PeerInfo> m_peers;
	uint8_t m_recvBuffer[2048];

	std::function<void(int)> m_connFailedCallback;

private:
	PeerId addPeer(NetAddress const& hostAddress, NetAddress const& grayAddr = NetAddress::any(0), NetAddress const& whiteAddr = NetAddress::any(0));
	void delPeer(PeerId peerId);

	void onConnectionFailed(int reason);

	void onReject(NetAddress const& src, CBytes data);
	void onRequest(NetAddress const& src, CBytes data);
	void onResponce(NetAddress const& src, CBytes data);
	void onJoinOk(NetAddress const& src, CBytes data);
	void onJoin(NetAddress const& src, CBytes data);
	void onPingA(NetAddress const& src, CBytes data);

	void receive();
	void sendRequest(NetAddress const& target);
	void sendPingMessage(NetAddress const& target, uint16_t msgid);
	void sendShortMessage(NetAddress const& target, uint16_t msgid);

	void setPeerStatus(NetAddress const& addr, PeerInfo::Status status);
};
#include "config.h"

#include "hole_puncher.h"
#include "stun_client.h"
#include "host.h"
#include "log.h"

#include <thread>
#include <atomic>
#include "ui.h"


class NetHostClient : public INetClient {
	virtual void onPeerConnected(PeerId peer) override { log(0, "Peer [%d/%d] connected.", peer.index, peer.nonce); }
	virtual void onPeerDisconnected(PeerId peer) override { log(0, "Peer [%d/%d] disconnected.", peer.index, peer.nonce); }
	virtual void onMessageReceived(PeerId peer, int id, CBytes msg) override { log(0, "Msg [%d/%d]: %s", toString(msg).c_str()); }
};


int netw_main(Config cfg, ConsoleUi* ui)
{
	Socket socket;
	if (!socket.valid()) {
		ui->onFatalErrorWinApi("Internal error: unable to create empty socket by reason '%s' [code 0x%08X].", WinSock::getLastError());
		return 0;
	}
	if (!socket.bind(cfg.endpoint)) {
        ui->onFatalErrorWinApi("Socket binding has failed: %s [code 0x%08X].", WinSock::getLastError());
		return 0;
	}

	auto natInfo = StunClient::resolve(socket);
	ui->setNatInfo(natInfo);

	if (natInfo.type == NatType::Symmetric) {
		//ui->onWarning("NAT type is 'Symmetric': connections with other peers can be impossible!");
        log(0, "NAT type is 'Symmetric': connections with other peers can be impossible!");
	}
	
	ui->askUserConfig(cfg);

	NetHostClient netClient;
	NetHost host(cfg.isMaster(), socket, natInfo, { &netClient });
    strcpy_s(host.nickname, sizeof(host.nickname), cfg.nickname.c_str());

	if (!cfg.isMaster()) {
		ui->setServerStatus(ConsoleUi::PeerStatus::Connecting);

		NetAddress addresses[2] = { cfg.localServerAddress, cfg.remoteServerAddress };
		host.connect(addresses, [ui](int code) {
            ui->setServerStatus(ConsoleUi::PeerStatus::Offline);
			log(0, "Connection failed: error code - %d", code);
		});
	}

	while (true) {
		host.update();

		if (host.peersInfoChanged) {
            ui->setServerStatus(ConsoleUi::PeerStatus::Connected);

			host.queryPeerInfos([ui](PeerId id, NetHost::PeerInfo const& peer){
                ui->setClient(id, peer.nickname, (ConsoleUi::PeerStatus)peer.status);
			});
			host.peersInfoChanged = false;
		}

		std::this_thread::sleep_for(std::chrono::microseconds(10));
	}

	return 0;
}


int main(int argc, char const* argv[])
{
	system("cls");
    WinSock winSock;
    ConsoleUi conui;
    if (!winSock.started) {
        conui.onFatalErrorWinApi("Internal error: unable to start WinSock by reason '%s' [code 0x%08X].", WinSock::getLastError());
        return 0;
    }

    conui.config = Config(argc, argv);
    if (conui.config.mode == Config::Mode::Help) {
        return 0;
    }

	std::thread networkThread(netw_main, conui.config, &conui);
    while (conui.update());
	networkThread.join();


	/*
	while (true) {
		int cmd = input.type.load();
		if (cmd == Input::Type::Quit) {
			break;
		}
		if (cmd == Input::Type::SendTo) {
			PeerId peer = host.findPeerByNonce(input.target);
			if (peer.isValid()) {
				host.send(peer, { input.message, input.message + strlen(input.message) });
			} else {
				printf("message cannot be sent: invalid peer");
			}
		}

		host.update();
	}*/
	return 0;
}
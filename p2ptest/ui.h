#pragma once

#include "config.h"
#include "stun_client.h"

#include <atomic>


struct ConsoleInput {
	enum Type { None, Up, Down, Left, Right, Enter, Esc, Char, String };
	enum Mode { SingleMode, StringMode };

public:
	std::atomic_int type;
	std::atomic_int mode;
	char string[64];
	int character;

public:
	ConsoleInput() { type.store(Type::None); mode.store(Mode::SingleMode); }

	void update();
	bool hasInput() { return type.load() != Type::None; }

	Type get(int& ch);
};

class ConsoleUi {
public:
	enum PeerStatus { Connecting, Connected, Inactive, Offline, Disconnecting };

	enum Flags {
		UpdateHeader = 0x001,
		UpdateBoard = 0x002,
		UpdateLogs = 0x004,

		AskMode = 0x100,
		AskAddress1 = 0x200,
		AskAddress2 = 0x400,
		AskNickname = 0x800,
	};

	struct Command {
		enum Type {
			Idle, NatInfo, UpdateConfig, UpdatingConfig, ConfigUpdated, UpdateUsers,
			OnBoardMain,
			Message, Critical,
			LogMsg, LogWarning, LogError,
		};
		union {
			struct { char buffer[128]; } message;
			StunClient::Result natInfo;
		};
	};

	struct Board {
		enum AskUnits { ASK_MODE, ASK_NICK, ASK_ADDR1, ASK_ADDR2, ASK_CONFIRM };

		enum Mode { Init, Ask, Chat } mode;
		union {
			struct {
				int active;
				bool editor;
			} ask;
		};
	};

    struct Client {
        char nickname[32];
        PeerStatus status;
        PoolHandle id;
    };

	Config config;
public:
	ConsoleUi();

	void onFatalError(char const* fmt, ...);
	void onFatalErrorWinApi(char const* fmt, uint32_t code);

	void setNatInfo(StunClient::Result const& result);
	void setServerStatus(PeerStatus status);
	void askUserConfig(Config& cfg);

	void setClient(PoolHandle id, char const* nickname, PeerStatus status);

	bool update();

private:
	std::atomic_int m_cmdtype;
	ConsoleInput m_input;
	Command m_cmd;

	uint32_t m_flags;
	StunClient::Result m_natInfo;
	Board m_boardstate;

    PeerStatus m_connStatus;
    std::vector<Client> m_clients;

private:
	void wait_command(int cmd = Command::Idle);
	void update_header();
	void update_board();
	void update_logs();

	void update_ask_board();
    void update_chat_board();
};
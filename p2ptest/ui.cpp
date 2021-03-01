#include "ui.h"
#include <stdio.h>
#include <stdarg.h>
#include <conio.h>
#include <chrono>
#include <thread>

#include <windows.h>


#define FOREGROUND_WHITE   FOREGROUND_BLUE | FOREGROUND_GREEN | FOREGROUND_RED
#define FOREGROUND_YELLOW  FOREGROUND_GREEN | FOREGROUND_RED


static void scroll_up(int& active, int max)
{
	if (active == 0) {
		active = max;
	} else if (active > max) {
		active = max;
	} else {
		active -= 1;
	}
}

static void scroll_down(int& active, int max)
{
	if (active >= max) {
		active = 0;
	} else {
		active += 1;
	}
}

static int peer_status_clr(ConsoleUi::PeerStatus status)
{
	switch (status){
    case ConsoleUi::Offline:       return FOREGROUND_RED;
    case ConsoleUi::Inactive:      return FOREGROUND_GREEN;
    case ConsoleUi::Disconnecting: return FOREGROUND_RED | FOREGROUND_INTENSITY;
	case ConsoleUi::Connecting:    return FOREGROUND_YELLOW | FOREGROUND_INTENSITY;
	case ConsoleUi::Connected:     return FOREGROUND_GREEN | FOREGROUND_INTENSITY;
	default:
		return FOREGROUND_WHITE;
	}
}

static char const* peer_status_str(ConsoleUi::PeerStatus status)
{
    switch (status) {
    case ConsoleUi::Offline:       return "offline";
    case ConsoleUi::Inactive:      return "inactive";
    case ConsoleUi::Disconnecting: return "disconnecting";
    case ConsoleUi::Connecting:    return "connecting";
    case ConsoleUi::Connected:     return "connected";
    default:
        return "unknown";
    }
}


int clrprintf(int color, char const* fmt, ...) {
	HANDLE console = GetStdHandle(STD_OUTPUT_HANDLE);
	SetConsoleTextAttribute(console, color);

	va_list args;
	va_start(args, fmt);
	int result = vprintf(fmt, args);
	va_end(args);

	SetConsoleTextAttribute(console, FOREGROUND_BLUE | FOREGROUND_GREEN | FOREGROUND_RED);
	return result;
}

int print_nat_type(NatType type)
{
	switch (type) {
	case NatType::Unknown:            return printf("Unknown");
	case NatType::Open:               return clrprintf(FOREGROUND_GREEN, "Open");
	case NatType::FullCone:           return clrprintf(FOREGROUND_GREEN, "Full cone");
	case NatType::AddressRestricted:  return clrprintf(FOREGROUND_RED | FOREGROUND_GREEN, "Address restricted");
	case NatType::PortRestricted:     return clrprintf(FOREGROUND_RED | FOREGROUND_GREEN, "Port restricted");
	case NatType::Symmetric:          return clrprintf(FOREGROUND_RED, "Symmetric");
	case NatType::Blocked:            return clrprintf(FOREGROUND_RED, "Blocked");
	default:                          return printf("Invalid");
	}
}

ConsoleUi::ConsoleUi()
{
	m_flags = 0;
	m_flags |= Flags::UpdateHeader;
	m_flags |= Flags::UpdateBoard;
	m_flags |= Flags::UpdateLogs;

	m_natInfo.type = NatType::Unknown;
	m_natInfo.grayAddress = config.endpoint;
	m_natInfo.whiteAddress = NetAddress::any(0);

    m_connStatus = PeerStatus::Offline;
}

bool ConsoleUi::update()
{
	int cmd = m_cmdtype.load();
	if (cmd == Command::ConfigUpdated) {
		return true;
	}

	switch (cmd) {
    case Command::UpdateUsers:
        m_flags |= Flags::UpdateBoard;
        break;
	case Command::Message:
		break;
	case Command::NatInfo:
		m_flags |= Flags::UpdateHeader;
		m_natInfo = m_cmd.natInfo;
		break;
	case Command::UpdateConfig:
		auto& ask = m_boardstate.ask;

		ask.active = 0;
		ask.editor = false;

		if (!config.isValid()) {
			m_boardstate.mode = Board::Ask;
			m_flags |= Flags::UpdateBoard;

			m_cmdtype.store(Command::UpdatingConfig);
			cmd = Command::UpdatingConfig;
		} else {
			m_boardstate.mode = Board::Chat;
			m_flags |= Flags::UpdateBoard;

			m_cmdtype.store(Command::ConfigUpdated);
            return true;
		}
		break;
	}

	if (cmd == Command::UpdatingConfig && m_input.hasInput()) {
		m_flags |= Flags::UpdateBoard;
	}

	if (m_flags & Flags::UpdateHeader) {
		update_header();
	}
	if (m_flags & Flags::UpdateBoard) {
		update_board();
	}
	if (m_flags & Flags::UpdateLogs) {
		update_logs();
	}

    if (cmd != Command::Idle && cmd != Command::UpdatingConfig) {
        m_cmdtype.store(Command::Idle);
    }
	if (cmd != Command::UpdatingConfig || !m_boardstate.ask.editor) {
		//SetConsoleCursorPosition(GetStdHandle(STD_OUTPUT_HANDLE), COORD{ 0, 20 });
	}

    return true;
}

void ConsoleUi::update_header()
{
	SetConsoleCursorPosition(GetStdHandle(STD_OUTPUT_HANDLE), COORD{ 0, 0});

	const bool hasNatType = (m_natInfo.type != NatType::Unknown);
	printf("========================================================================================================\n");
	printf("                                         P2P Chat Client ");

    
    if (config.isMaster()) {
        printf("[%s]", "master");
    } else {
        clrprintf(peer_status_clr(m_connStatus), "[%s]", peer_status_str(m_connStatus));
    }

    printf(" \n\n");

	if (config.nickname.empty()) {
		printf("   <anonymous> | ");
	} else {
		printf("   @%s  | ", config.nickname.c_str());
	}

	print_nat_type(m_natInfo.type);
	printf(" NAT: %s [local:%s]                       \n", hasNatType ? toString(m_natInfo.whiteAddress).c_str() : "<unresolved>", toString(m_natInfo.grayAddress).c_str());

	printf("========================================================================================================\n");
	m_flags &= ~Flags::UpdateHeader;
}

void ConsoleUi::update_board()
{
	SetConsoleCursorPosition(GetStdHandle(STD_OUTPUT_HANDLE), COORD{ 0, 6 });
	m_flags &= ~Flags::UpdateBoard;

	if (m_boardstate.mode == Board::Init) {
		printf("\n\n\n\n\n\n");
		printf("                   Not connected. Wait NAT status...           ");
		printf("\n\n\n\n\n\n");
	} else if (m_boardstate.mode == Board::Ask) {
		update_ask_board();
	} else if (m_boardstate.mode == Board::Chat) {
        update_chat_board();
	}
}

void ConsoleUi::update_ask_board()
{
	int ch = 0;
	ConsoleInput::Type inp = m_input.get(ch);
	auto& ask = m_boardstate.ask;

	if (inp == ConsoleInput::Up) scroll_up(ask.active, Board::ASK_CONFIRM);
	if (inp == ConsoleInput::Down) scroll_down(ask.active, Board::ASK_CONFIRM);
	if (inp == ConsoleInput::String) ask.editor = false;

	int lines = 0;
	int editorpos = 0;
	{
		int modeopt = (int)config.mode;
		if (ask.active == Board::ASK_MODE) {
			if (inp == ConsoleInput::Left) scroll_up(modeopt, 1);
			if (inp == ConsoleInput::Right || ch == '\r') scroll_down(modeopt, 1);
			config.mode = (Config::Mode)modeopt;
		}

		int color = (ask.active == Board::ASK_MODE) ? FOREGROUND_YELLOW : FOREGROUND_WHITE;

		                  clrprintf(color, "        Application mode\n");
		if (modeopt == 0) clrprintf(color, "     [Ordinary]     Master                \n\n");
		if (modeopt == 1) clrprintf(color, "      Ordinary     [Master]               \n\n");
		if (modeopt == 2) {
			clrprintf(color, "      Ordinary      Master    ");
			clrprintf(FOREGROUND_RED, "[Undefined]\n\n");
		}
		lines += 3;
	}
	{
		int color = FOREGROUND_WHITE;
		if (ask.active == Board::ASK_NICK) {
			if (inp == ConsoleInput::Left) ask.editor = false;
			if (inp == ConsoleInput::Right || inp == '\r') ask.editor = true;
			if (inp == ConsoleInput::String) {
				config.nickname = m_input.string;
				m_input.mode.store(ConsoleInput::SingleMode);
				m_flags |= Flags::UpdateHeader;
				ask.editor = false;
			}
			color = ask.editor ? FOREGROUND_GREEN : FOREGROUND_YELLOW;
			editorpos = lines;
		}
		const bool showfield = !ask.editor || (ask.active != Board::ASK_NICK);
		clrprintf(color, "   Nickname:             ");
		if (config.nickname.empty()) {
			clrprintf(FOREGROUND_RED, "%s                           \n", showfield ? "<not set>" : "");
		} else {
			clrprintf(color, "%s                           \n", showfield ? config.nickname.c_str() : "");
		}
		lines += 1;
	}
	{
		int color = FOREGROUND_WHITE;
		if (ask.active == Board::ASK_ADDR1) {
			if (inp == ConsoleInput::Left) ask.editor = false;
			if (inp == ConsoleInput::Right || ch == '\r') ask.editor = true;
			if (inp == ConsoleInput::String) {
				resolve_url(true, m_input.string, config.remoteServerAddress);
				m_input.mode.store(ConsoleInput::SingleMode);
			}
			color = ask.editor ? FOREGROUND_GREEN : FOREGROUND_YELLOW;
			editorpos = lines;
		}
		const bool showfield = !ask.editor || (ask.active != Board::ASK_ADDR1);
		const int addrcolor = (!config.isMaster() && config.remoteServerAddress.getport() == 0) ? FOREGROUND_RED : color;
		clrprintf(color, "   Server address:       ");
		clrprintf(addrcolor, "%s                            \n", showfield ? toString(config.remoteServerAddress).c_str() : "");
		lines += 1;
	}
	{
		int color = FOREGROUND_WHITE;
		if (ask.active == Board::ASK_ADDR2) {
			if (inp == ConsoleInput::Left) ask.editor = false;
			if (inp == ConsoleInput::Right || ch == '\r') ask.editor = true;
			if (inp == ConsoleInput::String) {
				resolve_url(true, m_input.string, config.localServerAddress);
				m_input.mode.store(ConsoleInput::SingleMode);
			}
			color = ask.editor ? FOREGROUND_GREEN : FOREGROUND_YELLOW;
			editorpos = lines;
		}
		const bool showfield = !ask.editor || (ask.active != Board::ASK_ADDR2);
		clrprintf(color, "   Alt server address:   %s                            \n", showfield ? toString(config.localServerAddress).c_str() : "");
		lines += 1;
	}
	{
		char const* szError = nullptr;

		int color = FOREGROUND_WHITE;
		if (ask.active == Board::ASK_CONFIRM) {
			color = FOREGROUND_YELLOW;
			if (inp == ConsoleInput::Right || ch == '\r') {
				if (config.nickname.empty()) szError = "nickname not set";
				if (config.mode == Config::Mode::Unknown) szError = "mode not set. Select 'Master' or 'Ordinary'.";
				if (config.mode != Config::Mode::Master) {
					if (config.remoteServerAddress.getport() == 0) szError = "server address not set";
				}
				color = szError == nullptr ? FOREGROUND_GREEN : FOREGROUND_RED;
			}
		}

		printf("                   ");
		clrprintf(color, "[Confirm]\n\n\n\n");
		if (szError != nullptr) {
			clrprintf(FOREGROUND_RED, "    Error: ", szError);
			clrprintf(FOREGROUND_WHITE, "%s                                      \n", szError);
		} else if (color == FOREGROUND_GREEN) {
			m_boardstate.mode = Board::Chat;
			m_flags |= Flags::UpdateBoard;
			m_cmdtype.store(Command::ConfigUpdated);
		}
	}

	if (ask.editor && inp != ConsoleInput::String) {
		SetConsoleCursorPosition(GetStdHandle(STD_OUTPUT_HANDLE), COORD{ 25, 6 + (short)editorpos });
		m_input.mode.store(ConsoleInput::StringMode);
	} else {
		m_input.mode.store(ConsoleInput::SingleMode);
	}
}

void ConsoleUi::update_chat_board()
{
    for (int i = 0; i < 12; ++i) {
        if (m_clients.size() > i) {
            auto& client = m_clients[i];
            clrprintf(peer_status_clr(client.status), "   %16s ", client.nickname);
        } else {
            printf("                    ");
        }
        printf("|                                         \n");
    }

    SetConsoleCursorPosition(GetStdHandle(STD_OUTPUT_HANDLE), COORD{ 0, 20 });
}

void ConsoleUi::update_logs()
{
	SetConsoleCursorPosition(GetStdHandle(STD_OUTPUT_HANDLE), COORD{ 0, 20 });
	printf("--------------------------------------------------------------------------------------------------------\n");
	m_flags &= ~Flags::UpdateLogs;
}

void ConsoleUi::wait_command(int cmdwait)
{
	while (m_cmdtype.load() != cmdwait) {
		std::this_thread::sleep_for(std::chrono::milliseconds(1));
	}
}


void ConsoleUi::setNatInfo(StunClient::Result const& result)
{
	wait_command();
	m_cmd.natInfo = result;
	m_cmdtype.store(Command::NatInfo);
}

void ConsoleUi::setServerStatus(PeerStatus status)
{
    wait_command();
    m_connStatus = status;
    m_cmdtype.store(Command::NatInfo);
}

void ConsoleUi::setClient(PoolHandle id, char const* nickname, PeerStatus status)
{
    wait_command();

    Client* pClient = nullptr;
    for (auto& client : m_clients) {
        if (client.id == id) {
            pClient = &client;
            break;
        }
    }

    if (pClient == nullptr) {
        m_clients.emplace_back();
        pClient = &m_clients.back();
    }

    pClient->id = id;
    pClient->status = status;
    strcpy_s(pClient->nickname, sizeof(pClient->nickname), nickname);

    m_cmdtype.store(Command::UpdateUsers);
}

void ConsoleUi::askUserConfig(Config& cfg)
{
	wait_command();
	m_cmdtype.store(Command::UpdateConfig);

	while (m_cmdtype.load() != Command::ConfigUpdated) {
		m_input.update();
	}

	cfg = config;
	m_cmdtype.store(Command::UpdateUsers);
}

void ConsoleUi::onFatalError(char const* fmt, ...)
{
	wait_command();

	va_list args;
	va_start(args, fmt);
	vsnprintf(m_cmd.message.buffer, sizeof(m_cmd.message.buffer), fmt, args);
	va_end(args);

	m_cmdtype.store(Command::Message);
}

void ConsoleUi::onFatalErrorWinApi(char const* fmt, uint32_t code)
{
	wait_command();

	char buffer[128];
	FormatMessage(FORMAT_MESSAGE_FROM_SYSTEM, NULL, code, 0, buffer, sizeof(buffer), NULL);
	snprintf(m_cmd.message.buffer, sizeof(m_cmd.message.buffer), fmt, buffer, code);

	m_cmdtype.store(Command::Message);
}

void ConsoleInput::update()
{
	while (type.load() != Type::None) {
		std::this_thread::sleep_for(std::chrono::milliseconds(1));
	}

	character = 0;
	int newtype = Type::None;

	auto m = mode.load();
	if (m == Mode::SingleMode) {
		if (GetAsyncKeyState(VK_UP)) newtype = Type::Up;
		else if (GetAsyncKeyState(VK_DOWN)) newtype = Type::Down;
		else if (GetAsyncKeyState(VK_LEFT)) newtype = Type::Left;
		else if (GetAsyncKeyState(VK_RIGHT)) newtype = Type::Right;

		if (newtype != Type::None) {
			type.store(newtype);

			int tries = 0;
			while (mode.load() == Mode::SingleMode) {
				std::this_thread::sleep_for(std::chrono::milliseconds(10));
				if (tries++ > 50) return;
			}
			return;
		}
	}
	if (m == Mode::StringMode) {
		fgets(string, 64, stdin);
		string[strlen(string) - 1] = '\0';
		type.store(Type::String);
	}
}

ConsoleInput::Type ConsoleInput::get(int& ch)
{
	int t = type.load();
	if (t != Type::None) {
		ch = character;
		type.store(Type::None);
	}
	return Type(t);
}
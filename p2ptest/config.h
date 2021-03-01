#pragma once

#include "socket.h"
#include <string>


struct Config {
	enum class Mode { Ordinary, Master, Unknown, Help };
	Mode mode = Mode::Ordinary;

	NetAddress remoteServerAddress = NetAddress::any(0);
	NetAddress localServerAddress = NetAddress::any(0);
	NetAddress endpoint = NetAddress::any(48800);
	std::string nickname;

public:
	Config() = default;
	Config(int argc, char const* argv[]);

	bool isMaster() const { return mode == Mode::Master; }
	bool isValid() const;
};
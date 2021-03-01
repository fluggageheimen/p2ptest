#pragma once

#include "socket.h"

enum class NatType {
	Unknown,
	Open,
	FullCone,
	AddressRestricted,
	PortRestricted,
	Symmetric,
	Blocked,
};

namespace StunClient {
	const static int MAX_RETRIES = 3;
	const static int LONG_RETRY_TIMEOUT_MS = 1000;
	const static int SHORT_RETRY_TIMEOUT_MS = 100;

	struct Result {
		NatType type;
		NetAddress grayAddress;
		NetAddress whiteAddress;
	};

	Result resolve(Socket const& socket);
	Result resolve(Socket const& socket, NetAddress const& serverAddr);

} // namespace StunClient
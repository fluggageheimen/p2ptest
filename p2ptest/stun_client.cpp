#include "stun_client.h"
#include "log.h"

#include <winsock2.h>


namespace StunClient {
	static char stunClientRespBuffer[512];

	const static uint8_t CHANGE_IP_FLAG = 0x04;
	const static uint8_t CHANGE_PORT_FLAG = 0x02;

	enum MsgType {
		MESSAGE_TYPE_BIND_REQUEST = 0x0001,
		MESSAGE_TYPE_BIND_RESPONSE = 0x0101,
		MESSAGE_TYPE_BIND_ERROR = 0x0111,
	};

	enum AttrType {
		ATTR_TYPE_MAPPED_ADDRESS = 0x0001,
		ATTR_TYPE_RESPONSE_ADDRESS = 0x0002,
		ATTR_TYPE_CHANGE_REQUEST = 0x0003,
		ATTR_TYPE_SOURCE_ADDRESS = 0x0004,
		ATTR_TYPE_CHANGED_ADDRESS = 0x0005,
		ATTR_TYPE_RESPONSE_ORIGIN = 0x802b,
		ATTR_TYPE_OTHER_ADDRESS = 0x802c,
	};

	enum FamilyType {
		FAMILTY_TYPE_IPV4 = 0x01,
		FAMILTY_TYPE_IPV6 = 0x02,
	};

#pragma pack(push, 1)
	struct Message {
		struct Header {
			const static uint32_t MAGIC_COOKIE = 0x2112A442;

			uint16_t type;
			uint16_t length;
			uint32_t cookie;
			char id[12];

			Header(uint16_t type, uint16_t len) : type(type), length(len), cookie(MAGIC_COOKIE) {}

			bool valid() const { return cookie == MAGIC_COOKIE; }
			void toNetEndian();
			void toHostEndian();
		};
		struct AttrHeader {
			uint16_t type;
			uint16_t length;

			void toHostEndian();
		};
		struct BindRequest {
			Header header;
			uint16_t type;
			uint16_t length;
			uint8_t data[4];

			BindRequest(bool changeIp, bool changePort);
			void toNetEndian();
		};
		struct Address {
			uint8_t reserved;
			uint8_t family;
			uint16_t port;
			uint32_t ip;
		};
	};
#pragma pack(pop)

	struct Responce {
		NetAddress mappedAddr;
		NetAddress otherAddr;
		NetAddress respOrigin;
	};

	static void genRandomString(char* buf, int len)
	{
		static const char alphanum[] =
			"0123456789"
			"ABCDEFGHIJKLMNOPQRSTUVWXYZ"
			"abcdefghijklmnopqrstuvwxyz";

		for (int i = 0; i < len; i++) {
			buf[i] = alphanum[rand() % sizeof(alphanum)];
		}
	}

	void Message::Header::toNetEndian()
	{
		type = htons(type);
		length = htons(length);
		cookie = htonl(cookie);
	}

	void Message::Header::toHostEndian()
	{
		type = ntohs(type);
		length = ntohs(length);
		cookie = ntohl(cookie);
	}

	void Message::AttrHeader::toHostEndian()
	{
		type = ntohs(type);
		length = ntohs(length);
	}

	Message::BindRequest::BindRequest(bool changeIp, bool changePort)
		: header(MESSAGE_TYPE_BIND_REQUEST, 8), type(ATTR_TYPE_CHANGE_REQUEST), length(4)
	{
		genRandomString(header.id, sizeof(header.id));

		memset(data, 0, 4);
		data[3] = changeIp ? CHANGE_IP_FLAG : 0;
		if (changePort) {
			data[3] = data[3] | CHANGE_PORT_FLAG;
		}
	}

	void Message::BindRequest::toNetEndian()
	{
		type = htons(type);
		length = htons(length);
		header.toNetEndian();
	}


	static bool parseAddress(char const* data, int len, NetAddress& outAddr)
	{
		if (len < sizeof(Message::Address)) {
			return false;
		}

		Message::Address* addr = (Message::Address*)data;
		outAddr = NetAddress::ipv4(ntohl(addr->ip), ntohs(addr->port));

		return addr->family == FAMILTY_TYPE_IPV4;
	}

	static bool sendBindRequest(Socket const& socket, NetAddress const& serverAddr, size_t timeout, bool changeIp, bool changePort, Responce& outResponse)
	{
		Message::BindRequest msgBindRequest(changeIp, changePort);
		msgBindRequest.toNetEndian();

		int bytesReceived = 0;
		for (int retries = 0; retries < MAX_RETRIES; retries++) {
			const int bytesToSend = sizeof(msgBindRequest);
			if (socket.sendto(serverAddr, (char const*)&msgBindRequest, bytesToSend, 0) != bytesToSend) {
				return false;
			}

			auto grayAddress = socket.sockname();

			Timer timer(timeout);
			do {
				sleep(10);
				bytesReceived = socket.recv(stunClientRespBuffer, 512, 0);
				if (bytesReceived >= 0) break;
			} while (!timer.expired());

			if (bytesReceived >= 0) {
				grayAddress = socket.sockname();
				break;
			}
		}
		if (bytesReceived < (int)sizeof(Message::Header)) {
			return false;
		}

		Message::Header recvHeader = *(Message::Header*)stunClientRespBuffer;
		recvHeader.toHostEndian();

		if (!recvHeader.valid()) {
			log(2, "Failed to parse STUN message.");
			return false;
		}

		if (recvHeader.type != MESSAGE_TYPE_BIND_RESPONSE) {
			log(2, "Got unexpected message type from STUN server. Expecting binding response(%#x), got %#x", MESSAGE_TYPE_BIND_RESPONSE, recvHeader.type);
			return false;
		}

		if (memcmp(msgBindRequest.header.id, recvHeader.id, 12)) {
			log(2, "Got wrong transaction ID in STUN response.");
			return false;
		}

		char const* curBuffer = stunClientRespBuffer + sizeof(Message::Header);
		char const* endBuffer = stunClientRespBuffer + bytesReceived;
		while (curBuffer < endBuffer) {
			Message::AttrHeader attrHeader = *(Message::AttrHeader*)curBuffer;
			curBuffer += sizeof(Message::AttrHeader);

			attrHeader.toHostEndian();
			if (attrHeader.type == ATTR_TYPE_MAPPED_ADDRESS) {
				parseAddress(curBuffer, attrHeader.length, outResponse.mappedAddr);
			}
			if (attrHeader.type == ATTR_TYPE_OTHER_ADDRESS) {
				parseAddress(curBuffer, attrHeader.length, outResponse.otherAddr);
			}
			if (attrHeader.type == ATTR_TYPE_RESPONSE_ORIGIN) {
				parseAddress(curBuffer, attrHeader.length, outResponse.respOrigin);
			}

			// skip unknown attributes
			curBuffer += attrHeader.length;
		}
		return true;
	}


	Result resolve(Socket const& socket)
	{
		NetAddress serverAddr;
		int err = resolve_url(true, "stun.hydrapi.net", 3478, serverAddr);
		return (err == 0) ? resolve(socket, serverAddr) : Result();
	}

	Result resolve(Socket const& socket, NetAddress const& serverAddr)
	{
		Result result;
		result.type = NatType::Unknown;
		result.grayAddress = resolve_local_address(socket);

		Responce response;
		if (!sendBindRequest(socket, serverAddr, LONG_RETRY_TIMEOUT_MS, false, false, response)) {
			log(2, "StunClient: Got no response from STUN server. Invalid address or no internet access.");
			result.type = NatType::Blocked;
			return result;
		}
		result.whiteAddress = response.mappedAddr;

		NetAddress altServerAddr = response.otherAddr;
		altServerAddr.setport(response.respOrigin.getport());
		log(2, "StunClient: Got stun response. Local addr '%s', mapped addr '%s'. Alt server addr '%s'.", toString(result.grayAddress).c_str(),
			toString(response.mappedAddr).c_str(), toString(response.otherAddr).c_str());

		if (result.grayAddress == result.whiteAddress) {
			result.type = NatType::Open;
			return result;
		}

		if (response.otherAddr.getport() == 0) {
			log(2, "StunClient: No alternative STUN server, can't detect NAT type.");
			result.type = NatType::Unknown;
			return result;
		}

		if (sendBindRequest(socket, serverAddr, SHORT_RETRY_TIMEOUT_MS, true, true, response)) {
			log(2, "StunClient: Received response from changed server. Full cone NAT type.");
			result.type = NatType::FullCone;
			return result;
		}

		if (sendBindRequest(socket, serverAddr, SHORT_RETRY_TIMEOUT_MS, false, true, response)) {
			log(2, "StunClient: Received response from changed server on different port. Seems like address restricted NAT type.");
			result.type = NatType::AddressRestricted;
		} else {
			log(2, "StunClient: No response from changed server on different port. Seems like port restricted NAT type.");
			result.type = NatType::PortRestricted;
		}

		if (!sendBindRequest(socket, altServerAddr, LONG_RETRY_TIMEOUT_MS, false, false, response)) {
			log(2, "StunClient: Failed to get response from alternative server. Can't detect NAT type.");
			result.type = NatType::Unknown;
			return result;
		}

		if (response.mappedAddr != result.whiteAddress) {
			log(2, "StunClient: Received response from alternative server with different mapping(%s != %s). Symmetric NAT type.",
				toString(result.whiteAddress).c_str(), toString(response.mappedAddr).c_str());
			result.type = NatType::Symmetric;
		}
		return result;
	}

} // namespace StunClient
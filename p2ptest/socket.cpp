#include "socket.h"

#include <winsock2.h>
#include <ws2tcpip.h>
#include <mswsock.h>
#include <assert.h>
#include <time.h>

#pragma comment (lib, "Ws2_32.lib")


uint16_t net_uint16_t::get() const { return ntohs(value); }
uint32_t net_uint32_t::get() const { return ntohl(value); }

void net_uint16_t::set(uint16_t val) { value = htons(val); }
void net_uint32_t::set(uint32_t val) { value = htonl(val); }


static int _get_addr_from_addrinfo(addrinfo* pAddrInfo, int port, NetAddress& address)
{
	for (addrinfo* ptr = pAddrInfo; ptr != NULL; ptr = ptr->ai_next) {
		if (ptr->ai_family == AF_INET) {
			memcpy(address.data, ptr->ai_addr, sizeof(NetAddress));
			if (port != 0) {
				sockaddr_in* sa = (sockaddr_in*)address.data;
				sa->sin_port = htons(port);
			}
			return 0;
		} else if (ptr->ai_family == AF_INET6) {
			// TODO: ipv6
		}
	}
	return -1;
}

int resolve_url(bool ipv4, char const* url, int port, NetAddress& address)
{
	addrinfo hints;
	memset(&hints, 0, sizeof(hints));
	hints.ai_family = ipv4 ? AF_INET : AF_INET6;
	hints.ai_socktype = SOCK_DGRAM;
	hints.ai_flags = 0;

	addrinfo* result = NULL;
	int ret = getaddrinfo(url, NULL, &hints, &result);
	if (ret != 0) return ret;

	ret = _get_addr_from_addrinfo(result, port, address);
	freeaddrinfo(result);
	return ret;
}

int resolve_url(bool ipv4, char const* url, NetAddress& address)
{
	addrinfo* result = NULL;
	int ret = 0;

	addrinfo hints;
	memset(&hints, 0, sizeof(hints));
	hints.ai_family = ipv4 ? AF_INET : AF_INET6;
	hints.ai_socktype = SOCK_DGRAM;
	hints.ai_flags = 0;

	char const* delim = strrchr(url, ' ');
	if (delim != NULL) {
		std::string service(delim + 1);
		std::string node(url, (size_t)(delim - url));
		ret = getaddrinfo(node.c_str(), service.c_str(), &hints, &result);
	} else {
		ret = getaddrinfo(url, NULL, &hints, &result);
	}
	if (ret != 0) return ret;

	ret = _get_addr_from_addrinfo(result, 0, address);
	freeaddrinfo(result);
	return ret;
}

NetAddress resolve_local_address(Socket const& sock)
{
	auto sockfd = socket(AF_INET, SOCK_STREAM, 0);
	if (sockfd == INVALID_SOCKET) {
		return sock.sockname();
	}

	NetAddress remoteAddress;
	resolve_url(true, "stackoverflow.com", 80, remoteAddress);
	if (connect(sockfd, (sockaddr*)remoteAddress.data, sizeof(remoteAddress.data)) < 0) {
		closesocket(sockfd);
		return sock.sockname();
	}

	NetAddress localAddress;
	socklen_t localAddressLen = sizeof(localAddress.data);
	if (getsockname(sockfd, (sockaddr*)localAddress.data, &localAddressLen) < 0) {
		closesocket(sockfd);
		return sock.sockname();
	} else {
		sockaddr_in* addr = (sockaddr_in*)localAddress.data;
		if (addr->sin_family != AF_INET) {
			closesocket(sockfd);
			return sock.sockname();
		}
	}

	localAddress.setport(sock.sockname().getport());
	//shutdown(sockfd, SHUT_RDWR);
	closesocket(sockfd);
	return localAddress;
}


std::string toString(NetAddress const& address)
{
	sockaddr_in* sockaddr = (sockaddr_in*)address.data;
	IN_ADDR ipaddr;
	ipaddr.S_un.S_addr = ntohl(sockaddr->sin_addr.S_un.S_addr);
	auto& ip = sockaddr->sin_addr.S_un.S_un_b;

	char buffer[32];
	snprintf(buffer, 32, "%d.%d.%d.%d:%d", ip.s_b1, ip.s_b2, ip.s_b3, ip.s_b4, ntohs(sockaddr->sin_port));
	return std::string(buffer);
}


WinSock::WinSock()
{
	srand((uint32_t)time(NULL));

	WSADATA wsaData;
	if (WSAStartup(WINSOCK_VERSION, &wsaData)) {
		WSACleanup();
	}
	started = true;
}


WinSock::~WinSock()
{
	WSACleanup();
	started = false;
}

int WinSock::getLastError()
{
	return WSAGetLastError();
}


int NetAddress::getport() const
{
	sockaddr_in* inaddr = (sockaddr_in*)data;
	return ntohs(inaddr->sin_port);
}

void NetAddress::setport(int port)
{
	sockaddr_in* inaddr = (sockaddr_in*)data;
	inaddr->sin_port = htons(port);
}

NetAddress NetAddress::any(uint16_t port)
{
	NetAddress address;
	memset(&address, 0, sizeof(address));
	sockaddr_in* inaddr = (sockaddr_in*)address.data;
	inaddr->sin_family = AF_INET;
	inaddr->sin_port = htons(port);
	return address;
}

NetAddress NetAddress::ipv4(uint32_t ip, uint16_t port)
{
	NetAddress address;
	memset(&address, 0, sizeof(address));
	sockaddr_in* inaddr = (sockaddr_in*)address.data;
	inaddr->sin_addr.S_un.S_addr = htonl(ip);
	inaddr->sin_port = htons(port);
	inaddr->sin_family = AF_INET;
	return address;
}

NetAddress NetAddress::ipv4(uint8_t b1, uint8_t b2, uint8_t b3, uint8_t b4, uint16_t port)
{
	NetAddress address;
	memset(&address, 0, sizeof(address));
	sockaddr_in* inaddr = (sockaddr_in*)address.data;
	inaddr->sin_addr.S_un.S_un_b.s_b1 = b1;
	inaddr->sin_addr.S_un.S_un_b.s_b2 = b2;
	inaddr->sin_addr.S_un.S_un_b.s_b3 = b3;
	inaddr->sin_addr.S_un.S_un_b.s_b4 = b4;
	inaddr->sin_port = htons(port);
	inaddr->sin_family = AF_INET;
	return address;
}


Socket::Socket()
	: handle(socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP))
{
	u_long mode = 1;
	if (ioctlsocket(handle, FIONBIO, &mode) != NO_ERROR) {
		closesocket(handle);
		handle = INVALID_SOCKET;
	}

	setsockopt(handle, IPPROTO_IP, IP_PKTINFO, (char*)&mode, sizeof(mode));
	setsockopt(handle, IPPROTO_IP, IP_RECVDSTADDR, (char*)&mode, sizeof(mode));
}


Socket::~Socket()
{
	if (valid()) {
		closesocket(handle);
	}
}

bool Socket::valid() const
{
	return handle != INVALID_SOCKET;
}

bool Socket::bind(NetAddress const& address) const
{
	return ::bind(handle, (sockaddr*)&address, sizeof(address)) != SOCKET_ERROR;
}

int Socket::recv(void* buf, int len, int flags) const
{
	return ::recv(handle, (char*)buf, len, flags);
}

int Socket::recvfrom(void* buf, int len, int flags, NetAddress& from) const
{
	int fromlen = (int)sizeof(from.data);
	return ::recvfrom(handle, (char*)buf, len, flags, (sockaddr*)from.data, &fromlen);
}

int Socket::sendto(NetAddress const& to, void const* buf, int len, int flags) const
{
	return ::sendto(handle, (char const*)buf, len, flags, (sockaddr*)to.data, sizeof(to.data));
}

int Socket::sendto(NetAddress const& to, void const* buf, int len, int flags, NetAddress* src) const
{
	if (src == NULL) {
		return sendto(to, buf, len, flags);
	}
	thread_local char controldata[1024];

	WSABUF iov;
	iov.buf = (char*)buf;
	iov.len = len;
	
	WSAMSG msg;
	msg.name = (sockaddr*)to.data;
	msg.namelen = sizeof(to.data);
	msg.lpBuffers = &iov;
	msg.dwBufferCount = 1;
	msg.Control = WSABUF{ ARRAYSIZE(controldata), controldata };
	msg.dwFlags = 0;

	DWORD length = len;
	if (::WSASendMsg(handle, &msg, 0, &length, NULL, NULL)) {
		int err = WinSock::getLastError();
		return SOCKET_ERROR;
	}

	int addrsize = sizeof(sockaddr_in);
	::getsockname(handle, (sockaddr*)src->data, &addrsize);

	WSACMSGHDR* pCmsg = NULL;
	for (pCmsg = CMSG_FIRSTHDR(&msg); pCmsg != NULL; pCmsg = CMSG_NXTHDR(&msg, pCmsg)) {
		if ((pCmsg->cmsg_level == IPPROTO_IP) && (pCmsg->cmsg_type == IP_PKTINFO) && WSA_CMSG_DATA(pCmsg)) {
			struct in_pktinfo* pInfo = (in_pktinfo*)WSA_CMSG_DATA(pCmsg);

			sockaddr_in* addr = (sockaddr_in*)src->data;
			addr->sin_family = AF_INET;
			addr->sin_addr = pInfo->ipi_addr;
			break;
		}
	}
	return length;
}


NetAddress Socket::sockname() const
{
	NetAddress address;
	memset(&address, 0, sizeof(address));

	int length = sizeof(address);
	::getsockname(handle, (sockaddr*)&address, &length);
	return address;
}
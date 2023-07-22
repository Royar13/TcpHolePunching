#pragma once

#include <winsock2.h>
#include <ws2tcpip.h>
#include <wil/resource.h>
#include <mutex>
#include "Address.h"


class Client
{
public:
	int CreateSocket();

private:
	static const int c_maxAttempts = 4;
	static const int c_timeoutSec = 3;
	wil::unique_socket m_successfulPeerSocket;
	std::mutex m_updatePeerInfoMutex;

	void Connect(USHORT port, const Address& connectToAddress);
	void Accept(USHORT port);
};


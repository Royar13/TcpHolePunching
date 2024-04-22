#pragma once

#include "Address.h"

class Client
{
public:
	int CreateSocket(Address mediatorAddr);

private:
	static constexpr int c_maxAttempts = 10;
	static constexpr int c_connectTimeoutSec = 1;
	static constexpr int c_acceptTimeoutSec = c_maxAttempts * c_connectTimeoutSec;

	std::array<std::string, 4> m_threadLogs;
	wil::unique_socket m_successfulPeerSocket;
	std::mutex m_updatePeerInfoMutex;
	std::condition_variable m_cvConnected;

	void Connect(std::string& log, const Address& localAddress, const Address& connectToAddress);
	void Accept(std::string& log, USHORT port);
};


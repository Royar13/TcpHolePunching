#pragma once

#include <Ws2tcpip.h>
#include <winsock2.h>
#include <windows.h>
#include <string>

struct Address
{
	std::string ipAddress;
	USHORT port;

	Address(const std::string& ipAddress, USHORT port);

	operator std::string() const;

	static Address FromString(const std::string& address);
};


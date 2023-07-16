#pragma once

#include <Ws2tcpip.h>
#include <winsock2.h>
#include <windows.h>
#include <string>

struct SessionEndpoint
{
	std::string ipAddress;
	USHORT port;

	SessionEndpoint(const std::string& ipAddress, USHORT port);

	static SessionEndpoint FromString(const std::string& session);
};


#include "stdafx.h"
#include "Address.h"

using namespace std;

Address::Address(const std::string& ipAddress, USHORT port)
	:ipAddress(ipAddress), port(port)
{
}

Address::operator std::string() const
{
	return ipAddress + ":" + to_string(port);
}

Address Address::FromString(const std::string& address)
{
	auto pos = address.find(":");
	if (pos == string::npos) {
		throw exception("Unsupported address string");
	}
	string ipAddress = address.substr(0, pos);
	string port = address.substr(pos + 1);
	auto portNum = (USHORT)stoi(port);
	return Address(ipAddress, portNum);
}

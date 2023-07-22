#include "Address.h"

using namespace std;

Address::Address(const std::string& ipAddress, USHORT port)
	:ipAddress(ipAddress), port(port)
{
}

Address::operator std::string()
{
	return ipAddress + ":" + to_string(port);
}

Address Address::FromString(const std::string& session)
{
	auto pos = session.find(":");
	if (pos == string::npos) {
		throw exception("Unsupported session string");
	}
	string ipAddress = session.substr(0, pos);
	string port = session.substr(pos + 2);
	auto portNum = (USHORT)stoi(port);
	return Address(ipAddress, portNum);
}

#include "SessionEndpoint.h"

using namespace std;

SessionEndpoint::SessionEndpoint(const std::string& ipAddress, USHORT port)
	:ipAddress(ipAddress), port(port)
{
}

SessionEndpoint SessionEndpoint::FromString(const std::string& session)
{
	auto pos = session.find(":");
	if (pos == string::npos) {
		throw exception("Unsupported session string");
	}
	string ipAddress = session.substr(0, pos);
	string port = session.substr(pos + 2);
	auto portNum = (USHORT)stoi(port);
	return SessionEndpoint(ipAddress, portNum);
}

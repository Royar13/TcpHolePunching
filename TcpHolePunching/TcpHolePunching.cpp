#include "stdafx.h"
#pragma comment(lib, "Ws2_32.lib")

#include <iostream>
#include <winsock2.h>
#include <ws2tcpip.h>
#include <stdio.h>
#include "Client.h"
#include "Mediator.h"

using namespace std;

int main(int argc, char* argv[])
{
	if (argc != 3) {
		perror("Two arguments are expected");
		return 1;
	}
	string arg = argv[1];
	string arg2 = argv[2];

	WSADATA wsaData;

	// Initialize Winsock
	int iResult = WSAStartup(MAKEWORD(2, 2), &wsaData);
	if (iResult != 0) {
		printf("WSAStartup failed: %d\n", iResult);
		return 1;
	}

	if (arg == "client") {
		auto addr = Address::FromString(arg2);
		Client client;
		return client.CreateSocket(addr);
	}
	else if (arg == "server") {
		auto port = (USHORT)stoi(arg2);
		Mediator server;
		return server.CreateSocket(port);
	}
	else {
		cerr << "Unsupported argument" << endl;
		return 1;
	}
}


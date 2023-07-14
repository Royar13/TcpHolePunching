#pragma comment(lib, "Ws2_32.lib")

#include <iostream>
#include <winsock2.h>
#include <ws2tcpip.h>
#include <stdio.h>
#include "Client.h"
#include "Mediator.h"

using namespace std;

int wmain(int argc, wchar_t* argv[])
{
	if (argc != 2) {
		perror("One argument expected");
		exit(EXIT_FAILURE);
	}
	wstring arg = argv[1];

	WSADATA wsaData;

	// Initialize Winsock
	int iResult = WSAStartup(MAKEWORD(2, 2), &wsaData);
	if (iResult != 0) {
		printf("WSAStartup failed: %d\n", iResult);
		return 1;
	}

	if (arg == L"client") {
		Client client;
		client.CreateSocket();
	}
	else if (arg == L"server") {
		Mediator server;
		server.CreateSocket();
	}
	else {
		throw exception("Unsupported argument");
	}
}


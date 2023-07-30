#include "stdafx.h"
#include "Mediator.h"
#include "Address.h"

using namespace std;

#define DEFAULT_BUFLEN 512

int Mediator::CreateSocket(USHORT port)
{
	struct addrinfo* localAddrInfo = NULL, hints;

	ZeroMemory(&hints, sizeof(hints));
	hints.ai_family = AF_INET;
	hints.ai_socktype = SOCK_STREAM;
	hints.ai_protocol = IPPROTO_TCP;
	hints.ai_flags = AI_PASSIVE;

	auto cleanup = wil::scope_exit([&]
		{
			if (localAddrInfo != NULL)
				freeaddrinfo(localAddrInfo);
			WSACleanup();
		});

	// Resolve the local address and port to be used by the server
	auto iResult = getaddrinfo(NULL, to_string(port).c_str(), &hints, &localAddrInfo);
	if (iResult != 0) {
		printf("getaddrinfo failed: %d\n", iResult);
		return 1;
	}

	// Create socket
	wil::unique_socket listenSocket(socket(localAddrInfo->ai_family, localAddrInfo->ai_socktype, localAddrInfo->ai_protocol));
	if (listenSocket.get() == INVALID_SOCKET) {
		printf("Error at socket(): %ld\n", WSAGetLastError());
		return 1;
	}

	// Enable reuse address
	const char enable = 1;
	if (setsockopt(listenSocket.get(), SOL_SOCKET, SO_REUSEADDR, &enable, sizeof(enable)) == SOCKET_ERROR) {
		printf("Error at setsockopt() SO_REUSEADDR: %ld\n", WSAGetLastError());
		return 1;
	}

	// Bind socket to address
	iResult = bind(listenSocket.get(), localAddrInfo->ai_addr, (int)localAddrInfo->ai_addrlen);
	if (iResult == SOCKET_ERROR) {
		printf("bind failed with error: %d\n", WSAGetLastError());
		return 1;
	}
	freeaddrinfo(localAddrInfo);
	localAddrInfo = NULL;

	// Listen to client connections
	if (listen(listenSocket.get(), SOMAXCONN) == SOCKET_ERROR) {
		printf("Listen failed with error: %ld\n", WSAGetLastError());
		return 1;
	}

	wil::unique_socket clientSockets[2];
	array<array<unique_ptr<Address>, 2>, 2> addresses;
	for (int i = 0; i < 2; i++) {
		auto& clientSocket = clientSockets[i];
		sockaddr_in peerName;
		int nameLen = sizeof(peerName);
		// Accept a connection
		cout << "Accepting connection #" << to_string(i) << endl;
		clientSocket.reset(accept(listenSocket.get(), (sockaddr*)&peerName, &nameLen));
		if (clientSocket.get() == INVALID_SOCKET) {
			printf("accept failed: %d\n", WSAGetLastError());
			return 1;
		}
		char peerPublicIpAddress[INET_ADDRSTRLEN];
		if (inet_ntop(AF_INET, &(peerName.sin_addr), peerPublicIpAddress, INET_ADDRSTRLEN) == nullptr) {
			printf("Failed to convert IP address to string.");
			return 1;
		}
		auto peerPublicPort = peerName.sin_port;
		// Update public address of client
		addresses[i][1] = make_unique<Address>(peerPublicIpAddress, peerPublicPort);
		cout << "Client #" << to_string(i) << " connected: their public address is " << static_cast<string>(*addresses[i][1]) << endl;

		char recvbuf[DEFAULT_BUFLEN + 1];
		int recvbuflen = DEFAULT_BUFLEN;
		// Receive private address of client
		iResult = recv(clientSocket.get(), recvbuf, recvbuflen, 0);
		if (iResult > 0) {
			recvbuf[iResult] = '\0';
			addresses[i][0] = make_unique<Address>(Address::FromString(recvbuf));
			cout << "Received private address of client #" << to_string(i) << ": " << static_cast<string>(*addresses[i][0]) << endl;
		}
		else {
			printf("Connection closed before receiving any message");
			return 1;
		}
	}

	for (int i = 0; i < 2; i++) {
		auto& clientSocket = clientSockets[i];
		// Send the other client's public and private addresses
		string sendbuf = string(*addresses[1 - i][0]) + ";" + string(*addresses[1 - i][1]);
		iResult = send(clientSocket.get(), sendbuf.c_str(), (int)strlen(sendbuf.c_str()), 0);
		if (iResult == SOCKET_ERROR) {
			printf("Failed sending public and private addresses: %d\n", WSAGetLastError());
			return 1;
		}
		cout << "Sent private+public address of client #" << to_string(1 - i) << " to client #" << to_string(i) << endl;

		// Send a client its own public address
		string sendbufOwn = string(*addresses[i][1]);
		iResult = send(clientSocket.get(), sendbufOwn.c_str(), (int)strlen(sendbufOwn.c_str()), 0);
		if (iResult == SOCKET_ERROR) {
			printf("Failed sending client its own public address: %d\n", WSAGetLastError());
			return 1;
		}
		cout << "Sent public address of client #" << to_string(i) << " to itself" << endl;
	}


	return 0;
}

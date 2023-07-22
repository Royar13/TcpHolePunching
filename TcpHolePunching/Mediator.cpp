#include "Mediator.h"
#include <winsock2.h>
#include <ws2tcpip.h>
#include <string>
#include <memory>
#include <wil/resource.h>
#include <array>
#include "Address.h"

using namespace std;

#define PORT "8080"
#define DEFAULT_BUFLEN 512

int Mediator::CreateSocket()
{
	struct addrinfo* result = NULL, hints;

	ZeroMemory(&hints, sizeof(hints));
	hints.ai_family = AF_INET;
	hints.ai_socktype = SOCK_STREAM;
	hints.ai_protocol = IPPROTO_TCP;
	hints.ai_flags = AI_PASSIVE;

	auto cleanup = wil::scope_exit([&]
		{
			if (result != NULL)
				freeaddrinfo(result);
			WSACleanup();
		});

	// Resolve the local address and port to be used by the server
	auto iResult = getaddrinfo(NULL, PORT, &hints, &result);
	if (iResult != 0) {
		printf("getaddrinfo failed: %d\n", iResult);
		return 1;
	}

	wil::unique_socket listenSocket(socket(result->ai_family, result->ai_socktype, result->ai_protocol));
	if (listenSocket.get() == INVALID_SOCKET) {
		printf("Error at socket(): %ld\n", WSAGetLastError());
		return 1;
	}

	const char enable = 1;
	if (setsockopt(listenSocket.get(), SOL_SOCKET, SO_REUSEADDR, &enable, sizeof(enable)) == SOCKET_ERROR) {
		printf("Error at setsockopt() SO_REUSEADDR: %ld\n", WSAGetLastError());
		return 1;
	}

	iResult = bind(listenSocket.get(), result->ai_addr, (int)result->ai_addrlen);
	if (iResult == SOCKET_ERROR) {
		printf("bind failed with error: %d\n", WSAGetLastError());
		return 1;
	}
	freeaddrinfo(result);
	result = NULL;

	if (listen(listenSocket.get(), SOMAXCONN) == SOCKET_ERROR) {
		printf("Listen failed with error: %ld\n", WSAGetLastError());
		return 1;
	}

	wil::unique_socket clientSockets[2];
	array<array<unique_ptr<Address>, 2>, 2> addresses;
	printf("Accepting connections...\n");
	for (int i = 0; i < 2; i++) {
		auto& clientSocket = clientSockets[i];
		clientSocket.reset(accept(listenSocket.get(), NULL, NULL));
		if (clientSocket.get() == INVALID_SOCKET) {
			printf("accept failed: %d\n", WSAGetLastError());
			return 1;
		}

		sockaddr_in peerName;
		int nameLen = sizeof(peerName);
		iResult = getpeername(clientSocket.get(), (sockaddr*)&peerName, &nameLen);
		if (iResult == SOCKET_ERROR) {
			printf("Error at getsockname(): %ld\n", WSAGetLastError());
			return 1;
		}
		char peerPublicIpAddress[INET_ADDRSTRLEN];
		if (inet_ntop(AF_INET, &(peerName.sin_addr), peerPublicIpAddress, INET_ADDRSTRLEN) == nullptr) {
			printf("Failed to convert IP address to string.");
			return 1;
		}
		auto peerPublicPort = peerName.sin_port;
		addresses[i][1] = make_unique<Address>(peerPublicIpAddress, peerPublicPort);

		char recvbuf[DEFAULT_BUFLEN + 1];
		int recvbuflen = DEFAULT_BUFLEN;

		iResult = recv(clientSocket.get(), recvbuf, recvbuflen, 0);
		if (iResult > 0) {
			recvbuf[iResult] = '\0';
			addresses[i][0] = make_unique<Address>(Address::FromString(recvbuf));
			printf("Bytes received: %d\nMessage: %s\n", iResult, recvbuf);
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
	}


	return 0;
}

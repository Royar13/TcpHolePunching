#include "Mediator.h"
#include <winsock2.h>
#include <ws2tcpip.h>
#include <string>
#include <wil/resource.h>

using namespace std;

#define PORT "8080"
#define DEFAULT_BUFLEN 512

int Mediator::CreateSocket()
{
	struct addrinfo* result = NULL, * ptr = NULL, hints;

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

	printf("Accepting connections...");
	wil::unique_socket clientSocket(accept(listenSocket.get(), NULL, NULL));
	if (clientSocket.get() == INVALID_SOCKET) {
		printf("accept failed: %d\n", WSAGetLastError());
		return 1;
	}

	char recvbuf[DEFAULT_BUFLEN];
	int iSendResult;
	int recvbuflen = DEFAULT_BUFLEN;

	// Receive until the peer shuts down the connection
	do {
		iResult = recv(clientSocket.get(), recvbuf, recvbuflen, 0);
		if (iResult > 0) {
			printf("Bytes received: %d\n", iResult);

			// Echo the buffer back to the sender
			iSendResult = send(clientSocket.get(), recvbuf, iResult, 0);
			if (iSendResult == SOCKET_ERROR) {
				printf("send failed: %d\n", WSAGetLastError());
				return 1;
			}
			printf("Bytes sent: %d\n", iSendResult);
		}
		else if (iResult == 0)
			printf("Connection closing...\n");
		else {
			printf("recv failed: %d\n", WSAGetLastError());
			return 1;
		}

	} while (iResult > 0);

	return 0;
}

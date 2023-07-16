#include "Mediator.h"
#include <winsock2.h>
#include <ws2tcpip.h>
#include <string>
#include <memory>
#include <wil/resource.h>
#include "SessionEndpoint.h"

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

	unique_ptr<SessionEndpoint> sessions[2][2];
	printf("Accepting connections...\n");
	for (int i = 0; i < 2; i++) {
		wil::unique_socket clientSocket(accept(listenSocket.get(), NULL, NULL));
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
		sessions[i][1] = make_unique<SessionEndpoint>(peerPublicIpAddress, peerPublicPort);

		char recvbuf[DEFAULT_BUFLEN + 1];
		int iSendResult;
		int recvbuflen = DEFAULT_BUFLEN;

		// Receive until the peer shuts down the connection
		do {
			iResult = recv(clientSocket.get(), recvbuf, recvbuflen, 0);
			if (iResult > 0) {
				recvbuf[iResult] = '\0';
				auto clientSession = SessionEndpoint::FromString(recvbuf);
				sessions[i][0] = make_unique<SessionEndpoint>(clientSession.ipAddress, clientSession.port);
				printf("Bytes received: %d\nMessage: %s\n", iResult, recvbuf);

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
	}

	return 0;
}

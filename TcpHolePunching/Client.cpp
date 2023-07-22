#include "Client.h"
#include <Ws2tcpip.h>
#include <winsock2.h>
#include <string>
#include <wil/resource.h>
#include "Address.h"
#include <array>
#include <memory>

using namespace std;

#define PORT "8080"
#define DEFAULT_BUFLEN 512


array<unique_ptr<Address>, 2> ParseAddressesFromMediator(string addressStr) {
	auto pos = addressStr.find(";");
	if (pos == string::npos) {
		throw exception("Unsupported mediator address string");
	}
	string firstAddress = addressStr.substr(0, pos);
	string secondAddress = addressStr.substr(pos + 2);
	array<unique_ptr<Address>, 2> addresses;
	addresses[0] = make_unique<Address>(Address::FromString(firstAddress));
	addresses[1] = make_unique<Address>(Address::FromString(secondAddress));
	return addresses;
}

void Client::Connect(USHORT port, const Address& connectToAddress) {
	struct addrinfo* result = NULL,
		* ptr = NULL,
		hints;

	ZeroMemory(&hints, sizeof(hints));
	hints.ai_family = AF_UNSPEC;
	hints.ai_socktype = SOCK_STREAM;
	hints.ai_protocol = IPPROTO_TCP;

	int iResult = getaddrinfo(connectToAddress.ipAddress.c_str(), to_string(connectToAddress.port).c_str(), &hints, &result);
	if (iResult != 0) {
		printf("Connect: getaddrinfo for connectToAddress failed: %d\n", iResult);
		return;
	}

	ptr = result;
	wil::unique_socket connectSocket(socket(ptr->ai_family, ptr->ai_socktype, ptr->ai_protocol));
	bool connected = false;
	if (connectSocket.get() == INVALID_SOCKET) {
		printf("Connect: Error at socket(): %ld\n", WSAGetLastError());
		return;
	}

	iResult = getaddrinfo(NULL, to_string(port).c_str(), &hints, &result);
	if (iResult != 0) {
		printf("Connect: getaddrinfo for local address failed: %d\n", iResult);
		return;
	}

	const char enable = 1;
	if (setsockopt(connectSocket.get(), SOL_SOCKET, SO_REUSEADDR, &enable, sizeof(enable)) == SOCKET_ERROR) {
		printf("Connect: Error at setsockopt() SO_REUSEADDR: %ld\n", WSAGetLastError());
		return;
	}

	iResult = bind(connectSocket.get(), result->ai_addr, (int)result->ai_addrlen);
	if (iResult == SOCKET_ERROR) {
		printf("Connect: bind failed with error: %d\n", WSAGetLastError());
		return;
	}

	//set the socket in non-blocking
	unsigned long iMode = 1;
	iResult = ioctlsocket(connectSocket.get(), FIONBIO, &iMode);
	if (iResult == SOCKET_ERROR)
	{
		printf("Connect: ioctlsocket failed with error: %ld\n", iResult);
		return;
	}

	TIMEVAL Timeout;
	Timeout.tv_sec = c_timeoutSec;
	Timeout.tv_usec = 0;

	for (int i = 0; i < c_maxAttempts; i++) {
		iResult = connect(connectSocket.get(), ptr->ai_addr, (int)ptr->ai_addrlen);
		if (iResult == SOCKET_ERROR) {
			printf("Connect: Unable to connect to other client!\n");
			return;
		}

		fd_set Write, Err;
		FD_ZERO(&Write);
		FD_ZERO(&Err);
		FD_SET(connectSocket.get(), &Write);
		FD_SET(connectSocket.get(), &Err);

		// check if the socket is ready
		select(0, NULL, &Write, &Err, &Timeout);
		if (FD_ISSET(connectSocket.get(), &Write))
		{
			lock_guard<mutex> lock(m_updatePeerInfoMutex);
			m_successfulPeerSocket.swap(connectSocket);
			return;
		}
	}
}

void Client::Accept(USHORT port) {
	struct addrinfo* result = NULL, hints;

	ZeroMemory(&hints, sizeof(hints));
	hints.ai_family = AF_INET;
	hints.ai_socktype = SOCK_STREAM;
	hints.ai_protocol = IPPROTO_TCP;
	hints.ai_flags = AI_PASSIVE;

	// Resolve the local address and port to be used by the server
	auto iResult = getaddrinfo(NULL, to_string(port).c_str(), &hints, &result);
	if (iResult != 0) {
		printf("Accept: getaddrinfo failed: %d\n", iResult);
		return;
	}

	wil::unique_socket listenSocket(socket(result->ai_family, result->ai_socktype, result->ai_protocol));
	if (listenSocket.get() == INVALID_SOCKET) {
		printf("Accept: Error at socket(): %ld\n", WSAGetLastError());
		return;
	}

	const char enable = 1;
	if (setsockopt(listenSocket.get(), SOL_SOCKET, SO_REUSEADDR, &enable, sizeof(enable)) == SOCKET_ERROR) {
		printf("Accept: Error at setsockopt() SO_REUSEADDR: %ld\n", WSAGetLastError());
		return;
	}

	iResult = bind(listenSocket.get(), result->ai_addr, (int)result->ai_addrlen);
	if (iResult == SOCKET_ERROR) {
		printf("Accept: bind failed with error: %d\n", WSAGetLastError());
		return;
	}
	freeaddrinfo(result);
	result = NULL;

	//set the socket in non-blocking
	unsigned long iMode = 1;
	iResult = ioctlsocket(listenSocket.get(), FIONBIO, &iMode);
	if (iResult == SOCKET_ERROR)
	{
		printf("Accept: ioctlsocket failed with error: %ld\n", iResult);
		return;
	}

	if (listen(listenSocket.get(), SOMAXCONN) == SOCKET_ERROR) {
		printf("Accept: Listen failed with error: %ld\n", WSAGetLastError());
		return;
	}

	TIMEVAL Timeout;
	Timeout.tv_sec = c_timeoutSec;
	Timeout.tv_usec = 0;

	for (int i = 0; i < c_maxAttempts; i++) {
		sockaddr_in peerName;
		int nameLen = sizeof(peerName);
		wil::unique_socket clientSocket(accept(listenSocket.get(), (sockaddr*)&peerName, &nameLen));
		if (clientSocket.get() == INVALID_SOCKET) {
			printf("Accept: accept failed: %d\n", WSAGetLastError());
			return;
		}

		fd_set Read, Err;
		FD_ZERO(&Read);
		FD_ZERO(&Err);
		FD_SET(clientSocket.get(), &Read);
		FD_SET(clientSocket.get(), &Err);

		// check if the socket is ready
		select(0, &Read, NULL, &Err, &Timeout);
		if (FD_ISSET(clientSocket.get(), &Read))
		{
			lock_guard<mutex> lock(m_updatePeerInfoMutex);
			m_successfulPeerSocket.swap(clientSocket);
			return;
		}
	}
}

int Client::CreateSocket()
{
	struct addrinfo* result = NULL,
		* ptr = NULL,
		hints;

	ZeroMemory(&hints, sizeof(hints));
	hints.ai_family = AF_UNSPEC;
	hints.ai_socktype = SOCK_STREAM;
	hints.ai_protocol = IPPROTO_TCP;

	auto cleanup = wil::scope_exit([&]
		{
			if (result != NULL)
				freeaddrinfo(result);
			WSACleanup();
		});

	int iResult = getaddrinfo("127.0.0.1", PORT, &hints, &result);
	if (iResult != 0) {
		printf("getaddrinfo failed: %d\n", iResult);
		return 1;
	}

	ptr = result;
	wil::unique_socket connectSocket(socket(ptr->ai_family, ptr->ai_socktype, ptr->ai_protocol));
	if (connectSocket.get() == INVALID_SOCKET) {
		printf("Error at socket(): %ld\n", WSAGetLastError());
		return 1;
	}

	const char enable = 1;
	if (setsockopt(connectSocket.get(), SOL_SOCKET, SO_REUSEADDR, &enable, sizeof(enable)) == SOCKET_ERROR) {
		printf("Error at setsockopt() SO_REUSEADDR: %ld\n", WSAGetLastError());
		return 1;
	}

	iResult = connect(connectSocket.get(), ptr->ai_addr, (int)ptr->ai_addrlen);
	if (iResult == SOCKET_ERROR) {
		printf("Unable to connect to server!\n");
		return 1;
	}
	sockaddr_in socketName;
	int nameLen = sizeof(socketName);
	iResult = getsockname(connectSocket.get(), (sockaddr*)&socketName, &nameLen);
	if (iResult == SOCKET_ERROR) {
		printf("Error at getsockname(): %ld\n", WSAGetLastError());
		return 1;
	}
	char localpAddress[INET_ADDRSTRLEN];
	if (inet_ntop(AF_INET, &(socketName.sin_addr), localpAddress, INET_ADDRSTRLEN) == nullptr) {
		printf("Failed to convert IP address to string.");
		return 1;
	}

	int recvbuflen = DEFAULT_BUFLEN;
	Address privateAddress(localpAddress, socketName.sin_port);
	string sendbuf(privateAddress);
	char recvbuf[DEFAULT_BUFLEN + 1];

	// Send an initial buffer
	iResult = send(connectSocket.get(), sendbuf.c_str(), (int)strlen(sendbuf.c_str()), 0);
	if (iResult == SOCKET_ERROR) {
		printf("send failed: %d\n", WSAGetLastError());
		return 1;
	}

	printf("Bytes Sent: %ld\n", iResult);

	// shutdown the connection for sending since no more data will be sent
	// the client can still use the connectSocket for receiving data
	iResult = shutdown(connectSocket.get(), SD_SEND);
	if (iResult == SOCKET_ERROR) {
		printf("shutdown failed: %d\n", WSAGetLastError());
		return 1;
	}

	iResult = recv(connectSocket.get(), recvbuf, recvbuflen, 0);
	if (iResult == 0) {
		printf("Connection closed\n");
		return 1;
	}
	else if (iResult < 0) {
		printf("recv failed: %d\n", WSAGetLastError());
		return 1;
	}
	recvbuf[iResult] = '\0';
	printf("Bytes received: %d\nMessage: %s\n", iResult, recvbuf);
	auto addresses = ParseAddressesFromMediator(recvbuf);

	thread connectToPrivate(&Client::Connect, this, privateAddress.port, *addresses[0]);
	thread connectToPublic(&Client::Connect, this, privateAddress.port, *addresses[1]);

	connectToPrivate.join();
	connectToPublic.join();

	return 0;
}

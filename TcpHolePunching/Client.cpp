#include "Client.h"
#include <Ws2tcpip.h>
#include <winsock2.h>
#include <string>
#include <wil/resource.h>
#include "Address.h"
#include <array>
#include <memory>
#include <iostream>

using namespace std;

#define PORT "8080"
#define DEFAULT_BUFLEN 512

// Split string of private and public addresses, separated by semicolon
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

// Connect to connectToAddress, using a local port
void Client::Connect(USHORT port, const Address& connectToAddress) {
	struct addrinfo* connectToAddressInfo = NULL,
		* localAddrInfo = NULL,
		hints;

	ZeroMemory(&hints, sizeof(hints));
	hints.ai_family = AF_INET;
	hints.ai_socktype = SOCK_STREAM;
	hints.ai_protocol = IPPROTO_TCP;

	// Resolve the remote address to connect to
	int iResult = getaddrinfo(connectToAddress.ipAddress.c_str(), to_string(connectToAddress.port).c_str(), &hints, &connectToAddressInfo);
	if (iResult != 0) {
		printf("Connect: getaddrinfo for connectToAddress failed: %d\n", iResult);
		return;
	}

	// Create socket
	wil::unique_socket connectSocket(socket(connectToAddressInfo->ai_family, connectToAddressInfo->ai_socktype, connectToAddressInfo->ai_protocol));
	bool connected = false;
	if (connectSocket.get() == INVALID_SOCKET) {
		printf("Connect: Error at socket(): %ld\n", WSAGetLastError());
		return;
	}

	// Enable reuse address
	const char enable = 1;
	if (setsockopt(connectSocket.get(), SOL_SOCKET, SO_REUSEADDR, &enable, sizeof(enable)) == SOCKET_ERROR) {
		printf("Connect: Error at setsockopt() SO_REUSEADDR: %ld\n", WSAGetLastError());
		return;
	}

	// Resolve the local address and port to be used by the client
	iResult = getaddrinfo(NULL, to_string(port).c_str(), &hints, &localAddrInfo);
	if (iResult != 0) {
		printf("Connect: getaddrinfo for local address failed: %d\n", iResult);
		return;
	}

	// Bind socket to address
	iResult = bind(connectSocket.get(), localAddrInfo->ai_addr, (int)localAddrInfo->ai_addrlen);
	if (iResult == SOCKET_ERROR) {
		printf("Connect: bind failed with error: %d\n", WSAGetLastError());
		return;
	}

	// Set the socket in non-blocking
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
		// Connect to other client (peer)
		cout << "Performing attempt #" << to_string(i) << " to connect to peer" << endl;
		iResult = connect(connectSocket.get(), connectToAddressInfo->ai_addr, (int)connectToAddressInfo->ai_addrlen);
		if (iResult == SOCKET_ERROR) {
			printf("Connect: Unable to connect to other client!\n");
			return;
		}

		fd_set Write, Err;
		FD_ZERO(&Write);
		FD_ZERO(&Err);
		FD_SET(connectSocket.get(), &Write);
		FD_SET(connectSocket.get(), &Err);

		// Check if the socket is ready (Write=ready to send to)
		select(0, NULL, &Write, &Err, &Timeout);
		if (FD_ISSET(connectSocket.get(), &Write))
		{
			cout << "Successfully connected to peer!" << endl;
			lock_guard<mutex> lock(m_updatePeerInfoMutex);
			m_successfulPeerSocket.swap(connectSocket);
			return;
		}
		else {
			cout << "Timeout after " << to_string(c_timeoutSec) << "sec: failed attempt #" << to_string(i) << " to connect to peer" << endl;
		}
	}
}

void Client::Accept(USHORT port) {
	struct addrinfo* localAddrInfo = NULL, hints;

	ZeroMemory(&hints, sizeof(hints));
	hints.ai_family = AF_INET;
	hints.ai_socktype = SOCK_STREAM;
	hints.ai_protocol = IPPROTO_TCP;
	hints.ai_flags = AI_PASSIVE;

	// Resolve the local address and port to be used by the client, acting a a server
	auto iResult = getaddrinfo(NULL, to_string(port).c_str(), &hints, &localAddrInfo);
	if (iResult != 0) {
		printf("Accept: getaddrinfo failed: %d\n", iResult);
		return;
	}

	// Create socket
	wil::unique_socket listenSocket(socket(localAddrInfo->ai_family, localAddrInfo->ai_socktype, localAddrInfo->ai_protocol));
	if (listenSocket.get() == INVALID_SOCKET) {
		printf("Accept: Error at socket(): %ld\n", WSAGetLastError());
		return;
	}

	// Enable reuse address
	const char enable = 1;
	if (setsockopt(listenSocket.get(), SOL_SOCKET, SO_REUSEADDR, &enable, sizeof(enable)) == SOCKET_ERROR) {
		printf("Accept: Error at setsockopt() SO_REUSEADDR: %ld\n", WSAGetLastError());
		return;
	}

	// Bind socket to address
	iResult = bind(listenSocket.get(), localAddrInfo->ai_addr, (int)localAddrInfo->ai_addrlen);
	if (iResult == SOCKET_ERROR) {
		printf("Accept: bind failed with error: %d\n", WSAGetLastError());
		return;
	}
	freeaddrinfo(localAddrInfo);
	localAddrInfo = NULL;

	// Set the socket in non-blocking
	unsigned long iMode = 1;
	iResult = ioctlsocket(listenSocket.get(), FIONBIO, &iMode);
	if (iResult == SOCKET_ERROR)
	{
		printf("Accept: ioctlsocket failed with error: %ld\n", iResult);
		return;
	}

	// Listen to connection from other client
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
		// Accept a connection from other client (peer)
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

		// Check if the socket is ready (Read=ready to receive)
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
	struct addrinfo* mediatorAddrInfo = NULL, hints;

	ZeroMemory(&hints, sizeof(hints));
	hints.ai_family = AF_UNSPEC;
	hints.ai_socktype = SOCK_STREAM;
	hints.ai_protocol = IPPROTO_TCP;

	auto cleanup = wil::scope_exit([&]
		{
			if (mediatorAddrInfo != NULL)
				freeaddrinfo(mediatorAddrInfo);
			WSACleanup();
		});

	// Resolve address and port of mediator
	int iResult = getaddrinfo("127.0.0.1", PORT, &hints, &mediatorAddrInfo);
	if (iResult != 0) {
		printf("getaddrinfo failed: %d\n", iResult);
		return 1;
	}

	// Create socket
	wil::unique_socket connectSocket(socket(mediatorAddrInfo->ai_family, mediatorAddrInfo->ai_socktype, mediatorAddrInfo->ai_protocol));
	if (connectSocket.get() == INVALID_SOCKET) {
		printf("Error at socket(): %ld\n", WSAGetLastError());
		return 1;
	}

	// Enable reuse address
	const char enable = 1;
	if (setsockopt(connectSocket.get(), SOL_SOCKET, SO_REUSEADDR, &enable, sizeof(enable)) == SOCKET_ERROR) {
		printf("Error at setsockopt() SO_REUSEADDR: %ld\n", WSAGetLastError());
		return 1;
	}

	// Connect to mediator
	iResult = connect(connectSocket.get(), mediatorAddrInfo->ai_addr, (int)mediatorAddrInfo->ai_addrlen);
	if (iResult == SOCKET_ERROR) {
		printf("Unable to connect to server!\n");
		return 1;
	}
	cout << "Successfully connected to mediator" << endl;
	// Get private address of this socket
	sockaddr_in socketName;
	int nameLen = sizeof(socketName);
	iResult = getsockname(connectSocket.get(), (sockaddr*)&socketName, &nameLen);
	if (iResult == SOCKET_ERROR) {
		printf("Error at getsockname(): %ld\n", WSAGetLastError());
		return 1;
	}
	char privatelpAddress[INET_ADDRSTRLEN];
	if (inet_ntop(AF_INET, &(socketName.sin_addr), privatelpAddress, INET_ADDRSTRLEN) == nullptr) {
		printf("Failed to convert IP address to string.");
		return 1;
	}
	Address privateAddress(privatelpAddress, socketName.sin_port);
	cout << "Private address of client is: " << static_cast<string>(privateAddress) << endl;

	// Send private address to mediator
	string sendbuf(privateAddress);
	iResult = send(connectSocket.get(), sendbuf.c_str(), (int)strlen(sendbuf.c_str()), 0);
	if (iResult == SOCKET_ERROR) {
		printf("send failed: %d\n", WSAGetLastError());
		return 1;
	}
	cout << "Sent private address to mediator" << endl;

	// Shutdown the connection for sending since no more data will be sent.
	// Can still use the connectSocket for receiving data
	iResult = shutdown(connectSocket.get(), SD_SEND);
	if (iResult == SOCKET_ERROR) {
		printf("shutdown failed: %d\n", WSAGetLastError());
		return 1;
	}
	cout << "Shutdown client's connection for sending" << endl;

	// Receive the other client's private+public addresses
	int recvbuflen = DEFAULT_BUFLEN;
	char recvbuf[DEFAULT_BUFLEN + 1];
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
	auto addresses = ParseAddressesFromMediator(recvbuf);
	cout << "Received peer's addresses from mediator: " << recvbuf << endl;

	thread connectToPrivate(&Client::Connect, this, privateAddress.port, *addresses[0]);
	thread connectToPublic(&Client::Connect, this, privateAddress.port, *addresses[1]);

	connectToPrivate.join();
	connectToPublic.join();

	return 0;
}

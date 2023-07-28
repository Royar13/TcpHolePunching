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
void Client::Connect(string& log, USHORT port, const Address& connectToAddress) {
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
		log += "Connect: getaddrinfo for connectToAddress failed: " + to_string(iResult) + "\n";
		return;
	}

	// Create socket
	wil::unique_socket connectSocket(socket(connectToAddressInfo->ai_family, connectToAddressInfo->ai_socktype, connectToAddressInfo->ai_protocol));
	bool connected = false;
	if (connectSocket.get() == INVALID_SOCKET) {
		log += "Connect: Error at socket(): " + to_string(WSAGetLastError()) + "\n";
		return;
	}

	// Enable reuse address
	const char enable = 1;
	if (setsockopt(connectSocket.get(), SOL_SOCKET, SO_REUSEADDR, &enable, sizeof(enable)) == SOCKET_ERROR) {
		log += "Connect: Error at setsockopt() SO_REUSEADDR: " + to_string(WSAGetLastError()) + "\n";
		return;
	}

	// Resolve the local address and port to be used by the client
	iResult = getaddrinfo(NULL, to_string(port).c_str(), &hints, &localAddrInfo);
	if (iResult != 0) {
		log += "Connect: getaddrinfo for local address failed: " + to_string(iResult) + "\n";
		return;
	}

	// Bind socket to address
	iResult = bind(connectSocket.get(), localAddrInfo->ai_addr, (int)localAddrInfo->ai_addrlen);
	if (iResult == SOCKET_ERROR) {
		log += "Connect: bind failed with error: " + to_string(WSAGetLastError()) + "\n";
		return;
	}

	// Set the socket in non-blocking
	unsigned long iMode = 1;
	iResult = ioctlsocket(connectSocket.get(), FIONBIO, &iMode);
	if (iResult == SOCKET_ERROR)
	{
		log += "Connect: ioctlsocket failed with error: " + to_string(iResult) + "\n";
		return;
	}

	TIMEVAL Timeout;
	Timeout.tv_sec = c_timeoutSec;
	Timeout.tv_usec = 0;

	for (int i = 0; i < c_maxAttempts; i++) {
		// Connect to other client (peer)
		log += "Performing attempt #" + to_string(i) + " to connect to peer" + "\n";
		iResult = connect(connectSocket.get(), connectToAddressInfo->ai_addr, (int)connectToAddressInfo->ai_addrlen);
		if (iResult == SOCKET_ERROR) {
			auto err = WSAGetLastError();
			if (err != WSAEWOULDBLOCK) {
				log += "Connect: Unable to connect to other client, error:" + to_string(err) + "\n";
				return;
			}
		}

		fd_set Write;
		FD_ZERO(&Write);
		FD_SET(connectSocket.get(), &Write);

		// Check if the socket is ready (Write=ready to send to)
		iResult = select(0, NULL, &Write, NULL, &Timeout);
		if (iResult == SOCKET_ERROR) {
			// Error occurred
			log += "Connect: select failed, error:" + to_string(WSAGetLastError()) + "\n";
			return;
		}
		else if (iResult == 0) {
			// Timeout occurred
			log += "Connect: select timeout\n";
			continue;
		}

		lock_guard<mutex> lock(m_updatePeerInfoMutex);
		if (m_successfulPeerSocket) {
			// already connected in other thread
			return;
		}
		log += "Successfully connected to peer!\n";
		m_successfulPeerSocket.swap(connectSocket);
		return;
	}
}

void Client::Accept(string& log, USHORT port) {
	struct addrinfo* localAddrInfo = NULL, hints;

	ZeroMemory(&hints, sizeof(hints));
	hints.ai_family = AF_INET;
	hints.ai_socktype = SOCK_STREAM;
	hints.ai_protocol = IPPROTO_TCP;
	hints.ai_flags = AI_PASSIVE;

	// Resolve the local address and port to be used by the client, acting a a server
	auto iResult = getaddrinfo(NULL, to_string(port).c_str(), &hints, &localAddrInfo);
	if (iResult != 0) {
		log += "Accept: getaddrinfo failed: " + to_string(iResult) + "\n";
		return;
	}

	// Create socket
	wil::unique_socket listenSocket(socket(localAddrInfo->ai_family, localAddrInfo->ai_socktype, localAddrInfo->ai_protocol));
	if (listenSocket.get() == INVALID_SOCKET) {
		log += "Accept: Error at socket(): " + to_string(WSAGetLastError()) + "\n";
		return;
	}

	// Enable reuse address
	const char enable = 1;
	if (setsockopt(listenSocket.get(), SOL_SOCKET, SO_REUSEADDR, &enable, sizeof(enable)) == SOCKET_ERROR) {
		log += "Accept: Error at setsockopt() SO_REUSEADDR: " + to_string(WSAGetLastError()) + "\n";
		return;
	}

	// Bind socket to address
	iResult = bind(listenSocket.get(), localAddrInfo->ai_addr, (int)localAddrInfo->ai_addrlen);
	if (iResult == SOCKET_ERROR) {
		log += "Accept: bind failed with error: " + to_string(WSAGetLastError()) + "\n";
		return;
	}
	freeaddrinfo(localAddrInfo);
	localAddrInfo = NULL;

	// Set the socket in non-blocking
	unsigned long iMode = 1;
	iResult = ioctlsocket(listenSocket.get(), FIONBIO, &iMode);
	if (iResult == SOCKET_ERROR)
	{
		log += "Accept: ioctlsocket failed with error: " + to_string(iResult) + "\n";
		return;
	}

	// Listen to connection from other client
	if (listen(listenSocket.get(), SOMAXCONN) == SOCKET_ERROR) {
		log += "Accept: Listen failed with error: " + to_string(WSAGetLastError()) + "\n";
		return;
	}

	TIMEVAL Timeout;
	Timeout.tv_sec = c_timeoutSec;
	Timeout.tv_usec = 0;

	for (int i = 0; i < c_maxAttempts; i++) {
		log += "Performing attempt #" + to_string(i) + " to accept peer's connection\n";

		fd_set Read;
		FD_ZERO(&Read);
		FD_SET(listenSocket.get(), &Read);

		// Check if the socket is ready (Read=ready to receive)
		iResult = select(0, &Read, NULL, NULL, &Timeout);
		if (iResult == SOCKET_ERROR) {
			// Error occurred
			log += "Accept: select failed, error:" + to_string(WSAGetLastError()) + "\n";
			return;
		}
		else if (iResult == 0) {
			// Timeout occurred
			log += "Accept: select timeout\n";
			continue;
		}

		sockaddr_in peerName;
		int nameLen = sizeof(peerName);
		// Accept a connection from other client (peer)
		wil::unique_socket clientSocket(accept(listenSocket.get(), (sockaddr*)&peerName, &nameLen));
		if (clientSocket.get() == INVALID_SOCKET) {
			log += "Accept: accept failed: " + to_string(WSAGetLastError()) + "\n";
			return;
		}

		lock_guard<mutex> lock(m_updatePeerInfoMutex);
		if (m_successfulPeerSocket) {
			// already connected in other thread
			return;
		}

		log += "Successfully accepted peer's connection!\n";
		m_successfulPeerSocket.swap(clientSocket);
		return;
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
		cerr << "getaddrinfo failed: " << to_string(iResult) << endl;
		return 1;
	}

	// Create socket
	wil::unique_socket connectSocket(socket(mediatorAddrInfo->ai_family, mediatorAddrInfo->ai_socktype, mediatorAddrInfo->ai_protocol));
	if (connectSocket.get() == INVALID_SOCKET) {
		cerr << "Error at socket(): " << to_string(WSAGetLastError()) << endl;
		return 1;
	}

	// Enable reuse address
	const char enable = 1;
	if (setsockopt(connectSocket.get(), SOL_SOCKET, SO_REUSEADDR, &enable, sizeof(enable)) == SOCKET_ERROR) {
		cerr << "Error at setsockopt() SO_REUSEADDR: " << to_string(WSAGetLastError()) << endl;
		return 1;
	}

	// Connect to mediator
	iResult = connect(connectSocket.get(), mediatorAddrInfo->ai_addr, (int)mediatorAddrInfo->ai_addrlen);
	if (iResult == SOCKET_ERROR) {
		cerr << "Unable to connect to server, error: " << to_string(WSAGetLastError()) << endl;
		return 1;
	}
	cout << "Successfully connected to mediator" << endl;
	// Get private address of this socket
	sockaddr_in socketName;
	int nameLen = sizeof(socketName);
	iResult = getsockname(connectSocket.get(), (sockaddr*)&socketName, &nameLen);
	if (iResult == SOCKET_ERROR) {
		cerr << "Error at getsockname(): " << to_string(WSAGetLastError()) << endl;
		return 1;
	}
	char privatelpAddress[INET_ADDRSTRLEN];
	if (inet_ntop(AF_INET, &(socketName.sin_addr), privatelpAddress, INET_ADDRSTRLEN) == nullptr) {
		cerr << "Failed to convert IP address to string." << endl;
		return 1;
	}
	Address privateAddress(privatelpAddress, socketName.sin_port);
	cout << "Private address of client is: " << static_cast<string>(privateAddress) << endl;

	// Send private address to mediator
	string sendbuf(privateAddress);
	iResult = send(connectSocket.get(), sendbuf.c_str(), (int)strlen(sendbuf.c_str()), 0);
	if (iResult == SOCKET_ERROR) {
		cerr << "send failed: " << to_string(WSAGetLastError()) << endl;
		return 1;
	}
	cout << "Sent private address to mediator" << endl;

	// Shutdown the connection for sending since no more data will be sent.
	// Can still use the connectSocket for receiving data
	iResult = shutdown(connectSocket.get(), SD_SEND);
	if (iResult == SOCKET_ERROR) {
		cerr << "shutdown failed: " << to_string(WSAGetLastError()) << endl;
		return 1;
	}
	cout << "Shutdown client's connection for sending" << endl;

	// Receive the other client's private+public addresses
	int recvbuflen = DEFAULT_BUFLEN;
	char recvbuf[DEFAULT_BUFLEN + 1];
	iResult = recv(connectSocket.get(), recvbuf, recvbuflen, 0);
	if (iResult == 0) {
		cerr << "Connection closed" << endl;
		return 1;
	}
	else if (iResult < 0) {
		cerr << "recv failed: " << to_string(WSAGetLastError()) << endl;
		return 1;
	}
	recvbuf[iResult] = '\0';
	auto addresses = ParseAddressesFromMediator(recvbuf);
	cout << "Received peer's addresses from mediator: " << recvbuf << endl;

	thread connectToPrivateThread(&Client::Connect, this, ref(m_threadLogs[0]), privateAddress.port, *addresses[0]);
	thread connectToPublicThread(&Client::Connect, this, ref(m_threadLogs[1]), privateAddress.port, *addresses[1]);
	thread acceptThread(&Client::Accept, this, ref(m_threadLogs[2]), privateAddress.port);

	connectToPrivateThread.join();
	connectToPublicThread.join();
	acceptThread.join();

	for (int i = 0; i < 3; i++) {
		cout << "Logs of thread " + to_string(i) + ":\n" << m_threadLogs[i] << endl;
	}

	if (!m_successfulPeerSocket) {
		cerr << "Failed to establish connection with peer" << endl;
		return 1;
	}
	cout << "Choose role: 0=Send,1=Receive" << endl;
	char c;
	cin.get(c);
	if (c == 0) {
		// Shutdown connection for receiving
		iResult = shutdown(m_successfulPeerSocket.get(), SD_RECEIVE);
		if (iResult == SOCKET_ERROR) {
			cerr << "shutdown failed: " << to_string(WSAGetLastError()) << endl;
			return 1;
		}

		while (true) {
			iResult = recv(m_successfulPeerSocket.get(), recvbuf, recvbuflen, 0);
			if (iResult == 0) {
				cerr << "Connection closed" << endl;
				return 1;
			}
			else if (iResult < 0) {
				cerr << "recv failed: " << to_string(WSAGetLastError()) << endl;
				return 1;
			}
			recvbuf[iResult] = '\0';
			cout << "Received peer's message: " << recvbuf << endl;
		}
	}
	else {
		// Shutdown connection for sending
		iResult = shutdown(m_successfulPeerSocket.get(), SD_SEND);
		if (iResult == SOCKET_ERROR) {
			cerr << "shutdown failed: " << to_string(WSAGetLastError()) << endl;
			return 1;
		}

		while (true) {
			cout << "Enter message to send:" << endl;
			string sendbuf;
			getline(cin, sendbuf);
			iResult = send(m_successfulPeerSocket.get(), sendbuf.c_str(), (int)strlen(sendbuf.c_str()), 0);
			if (iResult == SOCKET_ERROR) {
				cerr << "send failed: " << to_string(WSAGetLastError()) << endl;
				return 1;
			}
			cout << "Sent message to peer: " << sendbuf << endl;
		}
	}

	return 0;
}

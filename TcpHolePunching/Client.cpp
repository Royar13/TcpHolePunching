#include "Client.h"
#include <Ws2tcpip.h>
#include <winsock2.h>
#include <string>

using namespace std;

#define PORT 8080

void Client::CreateSocket()
{
    int status, valread, client_fd;
    struct sockaddr_in serv_addr;
    string hello = "Hello from client";
    char buffer[1024] = { 0 };
    if ((client_fd = socket(AF_INET, SOCK_STREAM, 0)) < 0) {
        perror("Socket creation error");
        exit(EXIT_FAILURE);
    }

    serv_addr.sin_family = AF_INET;
    serv_addr.sin_port = htons(PORT);

    // Convert IPv4 and IPv6 addresses from text to binary
    // form
    if (InetPton(AF_INET, L"127.0.0.1", &serv_addr.sin_addr) <= 0) {
        perror("Invalid address/ Address not supported");
        exit(EXIT_FAILURE);
    }

    if ((status = connect(client_fd, (struct sockaddr*)&serv_addr, sizeof(serv_addr))) < 0) {
        perror("nConnection Failed");
        exit(EXIT_FAILURE);
    }
    send(client_fd, hello.c_str(), hello.length(), 0);
    printf("Hello message sent\n");
    valread = recv(client_fd, buffer, 1024, 0);
    printf("%s\n", buffer);

    // closing the connected socket
    closesocket(client_fd);
}

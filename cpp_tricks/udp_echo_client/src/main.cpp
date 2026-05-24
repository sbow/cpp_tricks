#include <arpa/inet.h>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

int main(int argc, char* argv[]) {
    const char* host = "127.0.0.1";
    uint16_t port = 9000;
    const char* message = "hello";

    if (argc >= 2) {
        host = argv[1];
    }
    if (argc >= 3) {
        port = static_cast<uint16_t>(std::stoi(argv[2]));
    }
    if (argc >= 4) {
        message = argv[3];
    }

    int sock = socket(AF_INET, SOCK_DGRAM, 0);
    if (sock < 0) {
        perror("socket");
        return 1;
    }

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    if (inet_pton(AF_INET, host, &addr.sin_addr) <= 0) {
        std::cerr << "invalid address: " << host << '\n';
        close(sock);
        return 1;
    }

    size_t msg_len = std::strlen(message);
    ssize_t sent = sendto(sock, message, msg_len, 0,
                          reinterpret_cast<sockaddr*>(&addr), sizeof(addr));
    if (sent < 0) {
        perror("sendto");
        close(sock);
        return 1;
    }

    char buf[1024];
    sockaddr_in from{};
    socklen_t from_len = sizeof(from);

    ssize_t n = recvfrom(sock, buf, sizeof(buf) - 1, 0,
                         reinterpret_cast<sockaddr*>(&from), &from_len);
    if (n < 0) {
        perror("recvfrom");
        close(sock);
        return 1;
    }

    buf[n] = '\0';
    std::cout << "echo: " << buf << '\n';
    close(sock);
}

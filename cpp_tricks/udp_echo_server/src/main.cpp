#include <arpa/inet.h>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

int main(int argc, char* argv[]) {
    uint16_t port = 9000;
    if (argc >= 2) {
        port = static_cast<uint16_t>(std::stoi(argv[1]));
    }

    int sock = socket(AF_INET, SOCK_DGRAM, 0);
    if (sock < 0) {
        perror("socket");
        return 1;
    }

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port = htons(port);

    if (bind(sock, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) < 0) {
        perror("bind");
        close(sock);
        return 1;
    }

    std::cerr << "UDP echo server listening on port " << port << '\n';

    char buf[1024];
    while (true) {
        sockaddr_in from{};
        socklen_t from_len = sizeof(from);

        ssize_t n = recvfrom(sock, buf, sizeof(buf), 0,
                             reinterpret_cast<sockaddr*>(&from), &from_len);
        if (n < 0) {
            perror("recvfrom");
            continue;
        }

        char host[INET_ADDRSTRLEN];
        inet_ntop(AF_INET, &from.sin_addr, host, sizeof(host));
        std::cerr << "received " << n << " bytes from " << host << ':'
                  << ntohs(from.sin_port) << '\n';

        ssize_t sent = sendto(sock, buf, static_cast<size_t>(n), 0,
                              reinterpret_cast<sockaddr*>(&from), from_len);
        if (sent < 0) {
            perror("sendto");
        }
    }
}

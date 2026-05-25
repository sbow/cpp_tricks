#include "ipc.h"

#include <cstdint>
#include <cstring>
#include <iostream>
#include <string>

namespace {

constexpr uint16_t kDefaultUdpPort = 19000;
constexpr const char* kDefaultUdsPath = "/tmp/cpp_tricks_echo_server.sock";

void usage(const char* prog) {
    std::cerr << "usage: " << prog << " udp [port]\n"
              << "       " << prog << " uds [socket_path]\n";
}

template<typename Transport>
void run_echo_server(const typename Transport::BindParams& bind_params) {
    EchoServer<Transport> server(bind_params);

    char storage[1024];
    Buffer buf = Buffer::writable(storage, sizeof(storage));
    typename Transport::RecvResult recv{};

    while (true) {
        server.recv_from(buf, recv);
        server.echo(recv, buf);
    }
}

}  // namespace

int main(int argc, char* argv[]) {
    const char* transport = (argc >= 2) ? argv[1] : "udp";

    try {
        if (std::strcmp(transport, "udp") == 0) {
            const uint16_t port = (argc >= 3)
                ? static_cast<uint16_t>(std::stoi(argv[2]))
                : kDefaultUdpPort;
            std::cerr << "UDP echo server on port " << port << '\n';
            run_echo_server<Udp>(Udp::BindParams{.port = port});
        } else if (std::strcmp(transport, "uds") == 0) {
            const std::string path = (argc >= 3) ? argv[2] : kDefaultUdsPath;
            std::cerr << "UDS echo server on " << path << '\n';
            run_echo_server<Uds>(Uds::BindParams{.path = path});
        } else {
            usage(argv[0]);
            return 1;
        }
    } catch (const std::exception& e) {
        std::cerr << "error: " << e.what() << '\n';
        return 1;
    }

    return 0;
}

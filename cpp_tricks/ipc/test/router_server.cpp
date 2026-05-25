#include "router_app.h"
#include "router_client_config.h"
#include "router_protocol.h"

#include <chrono>
#include <cstring>
#include <iostream>
#include <unistd.h>

namespace {

using Clock = std::chrono::steady_clock;
const Clock::time_point kStartTime = Clock::now();

uint64_t ns_since_start() {
    return static_cast<uint64_t>(
        std::chrono::duration_cast<std::chrono::nanoseconds>(
            Clock::now() - kStartTime).count());
}

void usage(const char* prog) {
    std::cerr << "usage: " << prog << " uds [router_path]\n"
              << "       " << prog << " udp [port]\n";
}

template<typename Transport>
void run_router(const typename Transport::BindParams& bind_params) {
    const EndpointRegistry& reg = demo_registry();
    Server<Transport> router;
    router.bind(bind_params);

    constexpr int kTestRecvTimeoutMs = 200;
    constexpr int kTestIdleExitMs = 1500;
    if (router_test_mode()) {
        router.set_recv_timeout_ms(kTestRecvTimeoutMs);
    }

    std::cerr << "router listening ("
              << router_endpoint_name(reg, kEndpointServer) << ")\n";

    RouterFrame frame;
    auto last_activity = Clock::now();

    while (!router_stop_requested()) {
        try {
            const ForwardResult fwd = router_forward(
                router,
                reg,
                kDemoRouteRules,
                sizeof(kDemoRouteRules) / sizeof(kDemoRouteRules[0]),
                frame,
                ns_since_start());
            if (!fwd) {
                continue;
            }

            last_activity = Clock::now();
            for (uint8_t dest : fwd.targets) {
                router_log(router_route_line(reg, fwd.source, dest, frame));
            }
        } catch (const std::runtime_error&) {
            if (router_test_mode()
                && router_idle_expired(last_activity,
                    std::chrono::milliseconds(kTestIdleExitMs))) {
                return;
            }
        }
    }
}

}  // namespace

int main(int argc, char* argv[]) {
    install_router_stop_handlers();

    if (argc < 2) {
        usage(argv[0]);
        return 1;
    }

    try {
        if (std::strcmp(argv[1], "uds") == 0) {
            const std::string path = (argc >= 3) ? argv[2] : kRouterUdsPath;
            ::unlink(path.c_str());
            std::cerr << "UDS router on " << path << '\n';
            run_router<Uds>(Uds::BindParams{.path = path});
        } else if (std::strcmp(argv[1], "udp") == 0) {
            const uint16_t port = (argc >= 3)
                ? static_cast<uint16_t>(std::stoi(argv[2]))
                : kRouterUdpPort;
            std::cerr << "UDP router on port " << port << '\n';
            run_router<Udp>(Udp::BindParams{.port = port});
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

#pragma once

#include <csignal>
#include <cstdlib>

inline volatile std::sig_atomic_t* router_stop_flag() {
    static volatile std::sig_atomic_t flag = 0;
    return &flag;
}

inline void router_on_stop(int) {
    *router_stop_flag() = 1;
}

inline void install_router_stop_handlers() {
    struct sigaction sa{};
    sa.sa_handler = router_on_stop;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = 0;
    sigaction(SIGTERM, &sa, nullptr);
    sigaction(SIGINT, &sa, nullptr);
}

inline bool router_stop_requested() {
    return *router_stop_flag() != 0;
}

inline bool router_test_mode() {
    return std::getenv("ROUTER_TEST") != nullptr;
}

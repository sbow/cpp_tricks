#pragma once

#include "ipc/app_shutdown.hpp"

#include <chrono>
#include <cstdlib>
#include <string_view>
#include <unistd.h>

inline volatile std::sig_atomic_t* router_stop_flag() {
    return app_stop_flag();
}

inline void install_router_stop_handlers() {
    install_app_stop_handlers();
}

inline bool router_stop_requested() {
    return app_stop_requested();
}

inline bool router_test_mode() {
    return std::getenv("ROUTER_TEST") != nullptr;
}

inline void router_log(std::string_view line) {
    std::string out(line);
    out += '\n';
    (void)!::write(STDERR_FILENO, out.data(), out.size());
}

inline bool router_idle_expired(
    std::chrono::steady_clock::time_point last,
    std::chrono::milliseconds limit) {
    return std::chrono::steady_clock::now() - last > limit;
}

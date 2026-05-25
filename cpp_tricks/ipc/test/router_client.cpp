#include "router_app.h"
#include "router_client_config.h"
#include "router_protocol.h"

#include <chrono>
#include <cstring>
#include <fstream>
#include <iostream>
#include <string>
#include <thread>

// Sample application roles: sensor, controller, recorder.

namespace {

constexpr int kRecvTimeoutMs = 200;
constexpr int kRecorderTestIdleExitMs = 500;
constexpr int kSensorMessages = 5;
constexpr int kControllerMessages = 3;
constexpr int kControllerListenMs = 2500;

void usage(const char* prog) {
    std::cerr << "usage: " << prog << " sensor uds [router_path] [client_path]\n"
              << "       " << prog << " controller uds [router_path] [client_path] [log_path]\n"
              << "       " << prog << " recorder uds [client_path] [log_path]\n"
              << "       " << prog << " sensor udp [router_port] [client_port]\n"
              << "       " << prog << " controller udp [router_port] [client_port] [log_path]\n"
              << "       " << prog << " recorder udp [client_port] [log_path]\n"
              << "       (router/client path and port args are optional — defaults in router_client_config.h)\n";
}

void append_record(const std::string& log_path, const RouterFrame& frame) {
    std::ofstream out(log_path, std::ios::app);
    if (!out) {
        throw std::runtime_error("failed to open log: " + log_path);
    }
    out << static_cast<int>(frame.source()) << ','
        << frame.timestamp_ns() << ','
        << frame.payload() << '\n';
}

void log_received(const char* role, const std::string& log_path, const RouterFrame& frame) {
    const EndpointRegistry& reg = demo_registry();
    append_record(log_path, frame);
    router_log(std::string(role) + " recv: "
        + frame.describe(router_endpoint_name(reg, frame.source())));
}

void log_sent(const char* role, uint8_t source_id, const std::string& payload) {
    const EndpointRegistry& reg = demo_registry();
    router_log(std::string(role) + " send: source="
        + router_endpoint_name(reg, source_id) + " payload=" + payload);
}

std::string log_path_for_role(const char* role, int argc, char* argv[]) {
    if (std::strcmp(role, "controller") == 0) {
        return (argc >= 6) ? argv[5] : demo_log_path(role);
    }
    if (std::strcmp(role, "recorder") == 0) {
        return (argc >= 5) ? argv[4] : demo_log_path(role);
    }
    return {};
}

template<typename Transport>
void run_sensor() {
    const EndpointRegistry& reg = demo_registry();
    RouterClient<Transport> client(reg, kEndpointSensor);
    router_log("sensor: sending " + std::to_string(kSensorMessages) + " messages");

    for (int i = 0; i < kSensorMessages; ++i) {
        const std::string payload = "sensor-" + std::to_string(i);
        log_sent("sensor", kEndpointSensor, payload);
        client.send_message(kEndpointSensor, payload);
        std::this_thread::sleep_for(std::chrono::milliseconds(80));
    }
    router_log("sensor: done");
}

template<typename Transport>
void run_controller(const std::string& log_path) {
    const EndpointRegistry& reg = demo_registry();
    RouterClient<Transport> client(reg, kEndpointController);
    router_log("controller: listening (log " + log_path + ")");

    for (int i = 0; i < kControllerMessages; ++i) {
        router_log("controller: waiting for sensor packet...");
        RouterFrame frame;
        client.recv_message_blocking_until(kEndpointSensor, frame);
        client.set_recv_timeout_ms(kRecvTimeoutMs);
        log_received("controller", log_path, frame);

        const std::string payload = "control-" + std::to_string(i);
        log_sent("controller", kEndpointController, payload);
        client.send_message(kEndpointController, payload);
    }

    client.set_recv_timeout_ms(kRecvTimeoutMs);
    const auto deadline = std::chrono::steady_clock::now()
        + std::chrono::milliseconds(kControllerListenMs);
    RouterFrame frame;
    while (std::chrono::steady_clock::now() < deadline) {
        if (client.recv_message(frame)) {
            log_received("controller", log_path, frame);
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }
    router_log("controller: done");
}

template<typename Transport>
void run_recorder(const std::string& log_path) {
    const EndpointRegistry& reg = demo_registry();
    RouterClient<Transport> client(reg, kEndpointRecorder);
    client.set_recv_timeout_ms(kRecvTimeoutMs);
    router_log("recorder: listening (log " + log_path + ")");

    RouterFrame frame;
    auto last_activity = std::chrono::steady_clock::now();
    while (!router_stop_requested()) {
        if (client.recv_message(frame)) {
            log_received("recorder", log_path, frame);
            last_activity = std::chrono::steady_clock::now();
        } else if (router_test_mode()
            && router_idle_expired(last_activity,
                std::chrono::milliseconds(kRecorderTestIdleExitMs))) {
            break;
        }
    }
}

template<typename Transport>
int dispatch_role(const char* role, int argc, char* argv[]) {
    const EndpointRegistry& reg = demo_registry();
    const EndpointInfo* info = endpoint_by_name(reg, role);
    if (!info) {
        return 1;
    }

    switch (info->id) {
        case kEndpointSensor:
            run_sensor<Transport>();
            return 0;
        case kEndpointController:
            run_controller<Transport>(log_path_for_role(role, argc, argv));
            return 0;
        case kEndpointRecorder:
            run_recorder<Transport>(log_path_for_role(role, argc, argv));
            return 0;
        default:
            return 1;
    }
}

struct RoleDispatcher {
    const char* role;
    int argc;
    char** argv;

    template<typename Transport>
    int operator()() const {
        return dispatch_role<Transport>(role, argc, argv);
    }
};

}  // namespace

int main(int argc, char* argv[]) {
    install_router_stop_handlers();

    if (argc < 3) {
        usage(argv[0]);
        return 1;
    }

    const char* role = argv[1];
    const char* transport = argv[2];

    try {
        const int rc = dispatch_transport(
            transport, RoleDispatcher{role, argc, argv});
        if (rc != 0) {
            usage(argv[0]);
            return 1;
        }
    } catch (const std::exception& e) {
        std::cerr << "error: " << e.what() << '\n';
        return 1;
    }

    return 0;
}

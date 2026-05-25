#pragma once

#include "router_protocol.h"

#include <cstring>

// Shared topology for the sensor / controller / recorder sample app.
// Included by router_client.cpp; router_server and router_test use the same demo.

constexpr uint8_t kEndpointSensor = 1;
constexpr uint8_t kEndpointController = 2;
constexpr uint8_t kEndpointRecorder = 3;

constexpr const char* kRouterUdsPath = "/tmp/cpp_tricks_router.sock";
constexpr const char* kSensorUdsPath = "/tmp/cpp_tricks_router_a.sock";
constexpr const char* kControllerUdsPath = "/tmp/cpp_tricks_router_b.sock";
constexpr const char* kRecorderUdsPath = "/tmp/cpp_tricks_router_c.sock";

constexpr uint16_t kRouterUdpPort = 19100;
constexpr uint16_t kSensorUdpPort = 19101;
constexpr uint16_t kControllerUdpPort = 19102;
constexpr uint16_t kRecorderUdpPort = 19103;
constexpr const char* kRouterUdpHost = "127.0.0.1";

constexpr const char* kControllerLogPath = "/tmp/cpp_tricks_router_b.log";
constexpr const char* kRecorderLogPath = "/tmp/cpp_tricks_router_c.log";

constexpr EndpointInfo kDemoEndpoints[] = {
    {kEndpointSensor, "sensor", kSensorUdsPath, kSensorUdpPort},
    {kEndpointController, "controller", kControllerUdsPath, kControllerUdpPort},
    {kEndpointRecorder, "recorder", kRecorderUdsPath, kRecorderUdpPort},
};

constexpr RouteRule kDemoRouteRules[] = {
    {kEndpointSensor, kEndpointController, kEndpointRecorder},
    {kEndpointController, kEndpointRecorder, 0},
};

inline const EndpointRegistry& demo_registry() {
    static const EndpointRegistry reg{
        kDemoEndpoints,
        sizeof(kDemoEndpoints) / sizeof(kDemoEndpoints[0]),
        kRouterUdsPath,
        kRouterUdpHost,
        kRouterUdpPort,
    };
    return reg;
}

inline const char* demo_log_path(const char* role) {
    if (std::strcmp(role, "controller") == 0) {
        return kControllerLogPath;
    }
    if (std::strcmp(role, "recorder") == 0) {
        return kRecorderLogPath;
    }
    return nullptr;
}

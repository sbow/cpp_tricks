#pragma once

#include "ipc.h"

#include <array>
#include <chrono>
#include <cstdint>
#include <cstring>
#include <cstring>
#include <stdexcept>
#include <string>
#include <string_view>
#include <unistd.h>

// Fixed 32-byte router frame:
//   [0]       endpoint id (source when forwarded by router)
//   [1..9]    nanoseconds since router started (9-byte big-endian unsigned)
//   [10..31]  payload (22 bytes)

constexpr size_t kRouterFrameSize = 32;
constexpr size_t kRouterTimestampOffset = 1;
constexpr size_t kRouterTimestampSize = 9;
constexpr size_t kRouterPayloadOffset = 10;
constexpr size_t kRouterPayloadSize = 22;

constexpr uint8_t kEndpointInvalid = 0;
constexpr uint8_t kEndpointServer = 255;

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

struct EndpointInfo {
    uint8_t id;
    const char* name;
    const char* uds_path;
    uint16_t udp_port;
};

struct EndpointRegistry {
    const EndpointInfo* endpoints;
    size_t count;
    const char* router_uds_path;
    const char* router_udp_host;
    uint16_t router_udp_port;
};

struct RouterFrame {
    uint8_t bytes[kRouterFrameSize]{};

    void init(uint8_t source_id) {
        std::memset(bytes, 0, kRouterFrameSize);
        set_source(source_id);
    }

    uint8_t source() const { return bytes[0]; }

    void set_source(uint8_t id) { bytes[0] = id; }

    void set_timestamp_ns(uint64_t ns) {
        for (int i = 8; i >= 0; --i) {
            bytes[kRouterTimestampOffset + static_cast<size_t>(i)] =
                static_cast<uint8_t>(ns & 0xff);
            ns >>= 8;
        }
    }

    uint64_t timestamp_ns() const {
        uint64_t ns = 0;
        for (size_t i = 0; i < kRouterTimestampSize; ++i) {
            ns = (ns << 8) | bytes[kRouterTimestampOffset + i];
        }
        return ns;
    }

    void set_payload(const void* data, size_t len) {
        if (len > kRouterPayloadSize) {
            throw std::runtime_error("router payload too large");
        }
        std::memset(bytes + kRouterPayloadOffset, 0, kRouterPayloadSize);
        if (len > 0) {
            std::memcpy(bytes + kRouterPayloadOffset, data, len);
        }
    }

    void set_payload(std::string_view payload) {
        set_payload(payload.data(), payload.size());
    }

    std::string payload() const {
        const char* begin = reinterpret_cast<const char*>(bytes + kRouterPayloadOffset);
        size_t len = kRouterPayloadSize;
        while (len > 0 && begin[len - 1] == '\0') {
            --len;
        }
        return std::string(begin, len);
    }

    std::string describe(const char* source_name) const {
        return "source=" + std::string(source_name)
            + " ts=" + std::to_string(timestamp_ns())
            + " payload=" + payload();
    }

    Buffer writable() { return Buffer::writable(bytes, kRouterFrameSize); }

    Buffer read_only() const { return Buffer::read_only(bytes, kRouterFrameSize); }
};

template<typename Predicate>
inline const EndpointInfo* endpoint_find(const EndpointRegistry& reg, Predicate pred) {
    for (size_t i = 0; i < reg.count; ++i) {
        if (pred(reg.endpoints[i])) {
            return &reg.endpoints[i];
        }
    }
    return nullptr;
}

inline const EndpointInfo* endpoint_by_id(const EndpointRegistry& reg, uint8_t id) {
    return endpoint_find(reg, [id](const EndpointInfo& e) { return e.id == id; });
}

inline const EndpointInfo* endpoint_by_name(const EndpointRegistry& reg, const char* name) {
    return endpoint_find(reg, [name](const EndpointInfo& e) {
        return std::strcmp(name, e.name) == 0;
    });
}

inline const EndpointInfo* endpoint_by_uds_path(const EndpointRegistry& reg, const char* path) {
    return endpoint_find(reg, [path](const EndpointInfo& e) {
        return std::strcmp(path, e.uds_path) == 0;
    });
}

inline const EndpointInfo* endpoint_by_udp_port(const EndpointRegistry& reg, uint16_t port) {
    return endpoint_find(reg, [port](const EndpointInfo& e) { return e.udp_port == port; });
}

inline const char* router_endpoint_name(const EndpointRegistry& reg, uint8_t id) {
    if (id == kEndpointServer) {
        return "server";
    }
    const EndpointInfo* info = endpoint_by_id(reg, id);
    return info ? info->name : "invalid";
}

struct RouteTargets {
    std::array<uint8_t, 8> ids{};
    size_t count = 0;

    const uint8_t* begin() const { return ids.data(); }
    const uint8_t* end() const { return ids.data() + count; }
};

struct RouteRule {
    uint8_t source;
    uint8_t dest0;
    uint8_t dest1;
};

inline RouteTargets route_targets_for(
    const RouteRule* rules,
    size_t rule_count,
    uint8_t source) {
    for (size_t i = 0; i < rule_count; ++i) {
        if (rules[i].source != source) {
            continue;
        }
        if (rules[i].dest1 != 0) {
            return RouteTargets{{{rules[i].dest0, rules[i].dest1}}, 2};
        }
        return RouteTargets{{{rules[i].dest0}}, 1};
    }
    return {};
}

struct ForwardResult {
    uint8_t source = kEndpointInvalid;
    RouteTargets targets{};

    explicit operator bool() const { return source != kEndpointInvalid; }
};

inline std::string router_route_line(
    const EndpointRegistry& reg,
    uint8_t source,
    uint8_t dest,
    const RouterFrame& frame) {
    return "router: " + std::string(router_endpoint_name(reg, source))
        + " -> " + router_endpoint_name(reg, dest)
        + " payload=" + frame.payload();
}

template<typename Transport>
struct RouterPeers;

template<>
struct RouterPeers<Uds> {
    static uint8_t endpoint_from(const EndpointRegistry& reg, const Uds::RecvResult& recv) {
        const EndpointInfo* info = endpoint_by_uds_path(reg, recv.from.sun_path);
        return info ? info->id : kEndpointInvalid;
    }

    static void bind_local(const EndpointRegistry& reg, int fd, uint8_t endpoint_id) {
        const EndpointInfo* info = endpoint_by_id(reg, endpoint_id);
        if (!info) {
            throw std::runtime_error("unknown uds endpoint");
        }
        ::unlink(info->uds_path);
        Uds::bind(fd, Uds::BindParams{.path = info->uds_path});
    }

    static void send_to_router(const EndpointRegistry& reg, int fd, const Buffer& payload) {
        Uds::send_to(fd, reg.router_uds_path, payload);
    }

    static void send(
        const EndpointRegistry& reg,
        int fd,
        uint8_t dest,
        const Buffer& payload) {
        const EndpointInfo* info = endpoint_by_id(reg, dest);
        if (info) {
            Uds::send_to(fd, info->uds_path, payload);
        }
    }
};

template<>
struct RouterPeers<Udp> {
    static uint8_t endpoint_from(const EndpointRegistry& reg, const Udp::RecvResult& recv) {
        const EndpointInfo* info = endpoint_by_udp_port(reg, ntohs(recv.from.sin_port));
        return info ? info->id : kEndpointInvalid;
    }

    static void bind_local(const EndpointRegistry& reg, int fd, uint8_t endpoint_id) {
        const EndpointInfo* info = endpoint_by_id(reg, endpoint_id);
        if (!info) {
            throw std::runtime_error("unknown udp endpoint");
        }
        Udp::bind(fd, Udp::BindParams{.port = info->udp_port});
    }

    static void send_to_router(const EndpointRegistry& reg, int fd, const Buffer& payload) {
        Udp::send_to(fd, reg.router_udp_host, reg.router_udp_port, payload);
    }

    static void send(
        const EndpointRegistry& reg,
        int fd,
        uint8_t dest,
        const Buffer& payload) {
        const EndpointInfo* info = endpoint_by_id(reg, dest);
        if (info) {
            Udp::send_to(fd, reg.router_udp_host, info->udp_port, payload);
        }
    }
};

template<typename Transport>
ForwardResult router_forward(
    Server<Transport>& server,
    const EndpointRegistry& reg,
    const RouteRule* rules,
    size_t rule_count,
    RouterFrame& frame,
    uint64_t timestamp_ns) {
    typename Transport::RecvResult recv{};
    Buffer buf = frame.writable();
    server.recv_from(buf, recv);

    if (buf.size < kRouterFrameSize) {
        return {};
    }

    const uint8_t source = RouterPeers<Transport>::endpoint_from(reg, recv);
    if (source == kEndpointInvalid) {
        return {};
    }

    frame.set_source(source);
    frame.set_timestamp_ns(timestamp_ns);

    ForwardResult result;
    result.source = source;
    result.targets = route_targets_for(rules, rule_count, source);
    for (uint8_t dest : result.targets) {
        RouterPeers<Transport>::send(reg, server.fd(), dest, buf);
    }
    return result;
}

template<typename Transport>
class RouterClient {
public:
    RouterClient(const EndpointRegistry& reg, uint8_t endpoint_id)
        : reg_(reg), endpoint_id_(endpoint_id) {
        RouterPeers<Transport>::bind_local(reg_, client_.fd(), endpoint_id);
    }

    int fd() const { return client_.fd(); }

    void set_recv_timeout_ms(int ms) { client_.set_recv_timeout_ms(ms); }

    void set_recv_blocking() { client_.set_recv_blocking(); }

    void send_message(uint8_t source_id, const std::string& payload) {
        RouterFrame frame;
        frame.init(source_id);
        frame.set_payload(payload);
        RouterPeers<Transport>::send_to_router(reg_, client_.fd(), frame.read_only());
    }

    bool recv_message(RouterFrame& frame) {
        Buffer buf = frame.writable();
        try {
            typename Transport::RecvResult recv{};
            client_.recv_from(buf, recv);
            return buf.size >= kRouterFrameSize;
        } catch (const std::runtime_error&) {
            return false;
        }
    }

    void recv_message_blocking_until(uint8_t wanted_source, RouterFrame& frame) {
        set_recv_blocking();
        while (true) {
            if (!recv_message(frame)) {
                continue;
            }
            if (frame.source() == wanted_source) {
                return;
            }
        }
    }

private:
    const EndpointRegistry& reg_;
    uint8_t endpoint_id_;
    Client<Transport> client_;
};

template<typename Fn>
int dispatch_transport(const char* transport, Fn&& fn) {
    if (std::strcmp(transport, "uds") == 0) {
        return fn.template operator()<Uds>();
    }
    if (std::strcmp(transport, "udp") == 0) {
        return fn.template operator()<Udp>();
    }
    return -1;
}

#pragma once

#include <arpa/inet.h>
#include <cstdint>
#include <cstring>
#include <netinet/in.h>
#include <unistd.h>
#include <stdexcept>
#include <string>
#include <sys/socket.h>
#include <sys/un.h>

// Non-owning view over caller storage (stack vector, etc.). Does not allocate.
struct Buffer {
    void* data;
    size_t size;
    size_t capacity;

    static Buffer writable(void* data, size_t capacity) {
        return Buffer{data, 0, capacity};
    }

    static Buffer read_only(const void* data, size_t size) {
        return Buffer{const_cast<void*>(data), size, size};
    }

private:
    Buffer(void* data_, size_t size_, size_t capacity_)
        : data(data_), size(size_), capacity(capacity_) {}
};

struct SocketConfig {
    const int domain;
    const int data_model_type;
    const int protocol;

    constexpr SocketConfig(int d, int type, int p) : domain(d), data_model_type(type), protocol(p) {}
};

struct Udp {
    struct BindParams {
        uint16_t port;
    };

    static void bind(int fd, const BindParams& params) {
        sockaddr_in addr{};
        addr.sin_family = AF_INET;
        addr.sin_port = htons(params.port);
        addr.sin_addr.s_addr = htonl(INADDR_ANY);

        if (::bind(fd, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) < 0) {
            throw std::runtime_error("failed to bind socket");
        }
    }

    struct SendParams {
        const char* host = "127.0.0.1";
        uint16_t port;
        Buffer payload;
    };

    static void connect_or_send(int fd, const SendParams& params) {
        sockaddr_in addr{};
        addr.sin_family = AF_INET;
        addr.sin_port = htons(params.port);
        if (inet_pton(AF_INET, params.host, &addr.sin_addr) <= 0) {
            throw std::runtime_error("invalid host");
        }

        send_to(fd, addr, params.payload);
    }

    static void send_to(int fd, const sockaddr_in& dest, const Buffer& payload) {
        ssize_t n = ::sendto(fd, payload.data, payload.size, 0,
            reinterpret_cast<const sockaddr*>(&dest), sizeof(dest));
        if (n < 0) {
            throw std::runtime_error("udp sendto failed");
        }
    }

    static void send_to(int fd, const char* host, uint16_t port, const Buffer& payload) {
        sockaddr_in addr{};
        addr.sin_family = AF_INET;
        addr.sin_port = htons(port);
        if (inet_pton(AF_INET, host, &addr.sin_addr) <= 0) {
            throw std::runtime_error("invalid host");
        }
        send_to(fd, addr, payload);
    }

    struct RecvResult {
        sockaddr_in from{};
    };

    static void recv_from(int fd, Buffer& buf, RecvResult& out) {
        socklen_t from_len = sizeof(out.from);

        const ssize_t n = ::recvfrom(fd, buf.data, buf.capacity, 0,
            reinterpret_cast<sockaddr*>(&out.from), &from_len);

        if (n < 0) {
            throw std::runtime_error("udp recvfrom failed");
        }
        buf.size = static_cast<size_t>(n);
    }

    static void echo(int fd, const RecvResult& r, const Buffer& payload) {
        send_to(fd, r.from, payload);
    }

    static constexpr SocketConfig kCfg{AF_INET, SOCK_DGRAM, IPPROTO_UDP};
};

struct Uds {
    struct BindParams {
        std::string path;
    };

    static void bind(int fd, const BindParams& params) {
        ::unlink(params.path.c_str());

        sockaddr_un addr{};
        addr.sun_family = AF_UNIX;
        std::strncpy(addr.sun_path, params.path.c_str(), sizeof(addr.sun_path) - 1);

        if (::bind(fd, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) < 0) {
            throw std::runtime_error("uds bind failed");
        }
    }

    struct SendParams {
        std::string server_path;
        std::string client_path;
        Buffer payload;
    };

    static void connect_or_send(int fd, const SendParams& params) {
        if (!params.client_path.empty()) {
            ::unlink(params.client_path.c_str());
            sockaddr_un local{};
            local.sun_family = AF_UNIX;
            std::strncpy(local.sun_path, params.client_path.c_str(), sizeof(local.sun_path) - 1);
            if (::bind(fd, reinterpret_cast<sockaddr*>(&local), sizeof(local)) < 0) {
                throw std::runtime_error("uds client bind failed");
            }
        }

        sockaddr_un server{};
        server.sun_family = AF_UNIX;
        std::strncpy(server.sun_path, params.server_path.c_str(), sizeof(server.sun_path) - 1);

        send_to(fd, server.sun_path, params.payload);
    }

    static void send_to(int fd, const std::string& path, const Buffer& payload) {
        send_to(fd, path.c_str(), payload);
    }

    static void send_to(int fd, const char* path, const Buffer& payload) {
        sockaddr_un dest{};
        dest.sun_family = AF_UNIX;
        std::strncpy(dest.sun_path, path, sizeof(dest.sun_path) - 1);

        ssize_t n = ::sendto(fd, payload.data, payload.size, 0,
            reinterpret_cast<sockaddr*>(&dest), sizeof(dest));
        if (n < 0) {
            throw std::runtime_error("uds sendto failed");
        }
    }

    struct RecvResult {
        sockaddr_un from{};
    };

    static void recv_from(int fd, Buffer& buf, RecvResult& out) {
        socklen_t from_len = sizeof(out.from);

        const ssize_t n = ::recvfrom(fd, buf.data, buf.capacity, 0,
            reinterpret_cast<sockaddr*>(&out.from), &from_len);

        if (n < 0) {
            throw std::runtime_error("uds recvfrom failed");
        }
        buf.size = static_cast<size_t>(n);
    }

    static void echo(int fd, const RecvResult& r, const Buffer& payload) {
        send_to(fd, r.from.sun_path, payload);
    }

    static constexpr SocketConfig kCfg{AF_UNIX, SOCK_DGRAM, 0};
};

class Socket {
public:
    explicit Socket(SocketConfig cfg)
        : fd_(::socket(cfg.domain, cfg.data_model_type, cfg.protocol)) {
        if (fd_ < 0) {
            throw std::runtime_error("failed to create socket");
        }
    }

    ~Socket() {
        if (fd_ >= 0) {
            ::close(fd_);
        }
    }

    Socket(const Socket&) = delete;
    Socket& operator=(const Socket&) = delete;
    Socket(Socket&&) = delete;
    Socket& operator=(Socket&&) = delete;

    int fd() const { return fd_; }

private:
    int fd_;
};

template<typename Transport>
class Client {
public:
    Client() : socket_(Transport::kCfg) {}

    int fd() const { return socket_.fd(); }

    void connect_or_send(const typename Transport::SendParams& params) {
        Transport::connect_or_send(socket_.fd(), params);
    }

    void recv_from(Buffer& buf, typename Transport::RecvResult& out) {
        Transport::recv_from(socket_.fd(), buf, out);
    }

    void set_recv_timeout_ms(int ms) {
        timeval tv{};
        tv.tv_sec = ms / 1000;
        tv.tv_usec = (ms % 1000) * 1000;
        if (setsockopt(socket_.fd(), SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv)) < 0) {
            throw std::runtime_error("setsockopt SO_RCVTIMEO failed");
        }
    }

    void set_recv_blocking() {
        timeval tv{};
        if (setsockopt(socket_.fd(), SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv)) < 0) {
            throw std::runtime_error("setsockopt SO_RCVTIMEO failed");
        }
    }

protected:
    Socket socket_;
};

template<typename Transport>
class Server {
public:
    Server() : socket_(Transport::kCfg) {}

    int fd() const { return socket_.fd(); }

    void bind(const typename Transport::BindParams& params) {
        Transport::bind(socket_.fd(), params);
    }

    void recv_from(Buffer& buf, typename Transport::RecvResult& out) {
        Transport::recv_from(socket_.fd(), buf, out);
    }

    void set_recv_timeout_ms(int ms) {
        timeval tv{};
        tv.tv_sec = ms / 1000;
        tv.tv_usec = (ms % 1000) * 1000;
        if (setsockopt(socket_.fd(), SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv)) < 0) {
            throw std::runtime_error("setsockopt SO_RCVTIMEO failed");
        }
    }

protected:
    Socket socket_;
};

template<typename Transport>
class EchoServer : public Server<Transport> {
public:
    explicit EchoServer(const typename Transport::BindParams& bind_params) {
        this->bind(bind_params);
    }

    void echo(const typename Transport::RecvResult& recv, const Buffer& payload) {
        Transport::echo(this->fd(), recv, payload);
    }

    void run() {
        char storage[1024];
        Buffer buf = Buffer::writable(storage, sizeof(storage));
        typename Transport::RecvResult recv{};

        while (true) {
            this->recv_from(buf, recv);
            echo(recv, buf);
        }
    }
};

template<typename Transport>
class EchoClient : public Client<Transport> {
public:
    Buffer exchange(const typename Transport::SendParams& send_params, Buffer recv_buf) {
        this->connect_or_send(send_params);
        typename Transport::RecvResult recv{};
        this->recv_from(recv_buf, recv);
        return recv_buf;
    }
};

using UdpClient = Client<Udp>;
using UdsClient = Client<Uds>;
using UdpServer = Server<Udp>;
using UdsServer = Server<Uds>;
using UdpEchoClient = EchoClient<Udp>;
using UdsEchoClient = EchoClient<Uds>;
using UdpEchoServer = EchoServer<Udp>;
using UdsEchoServer = EchoServer<Uds>;

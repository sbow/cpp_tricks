#include <arpa/inet.h>
#include <cstdint>
#include <cstring>
#include <netinet/in.h>
#include <unistd.h>
#include <stdexcept>
#include <string>
#include <sys/socket.h>
#include <sys/un.h>
#include <type_traits>

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

enum Role {
    kServer,
    kClient
};

struct SocketConfig {
    const int domain;
    const int data_model_type;
    const int protocol;

    constexpr SocketConfig(int d, int type, int p) : domain(d), data_model_type(type), protocol(p) {}
};

struct Udp : public SocketConfig {
    constexpr Udp() : SocketConfig(AF_INET, SOCK_DGRAM, IPPROTO_UDP) {}

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

        ssize_t n = ::sendto(fd, params.payload.data, params.payload.size, 0,
            reinterpret_cast<sockaddr*>(&addr), sizeof(addr));
        if (n < 0) {
            throw std::runtime_error("udp sendto failed");
        }
    }

    struct RecvResult {
        ssize_t n;
        sockaddr_in from{};
    };

    static void recv_from(int fd, Buffer& buf, RecvResult& out) {
        socklen_t from_len = sizeof(out.from);

        out.n = ::recvfrom(fd, buf.data, buf.capacity, 0,
            reinterpret_cast<sockaddr*>(&out.from), &from_len);

        if (out.n < 0) {
            throw std::runtime_error("udp recvfrom failed");
        }
        buf.size = static_cast<size_t>(out.n);
    }

    static void echo(int fd, const RecvResult& r, const Buffer& payload) {
        ssize_t sent = ::sendto(fd, payload.data, payload.size, 0,
            reinterpret_cast<const sockaddr*>(&r.from), sizeof(r.from));

        if (sent < 0) {
            throw std::runtime_error("udp echo failed");
        }
    }

    static constexpr SocketConfig kCfg{AF_INET, SOCK_DGRAM, IPPROTO_UDP};
};

struct Uds : public SocketConfig {
    constexpr Uds() : SocketConfig(AF_UNIX, SOCK_DGRAM, 0) {}

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

        ssize_t n = ::sendto(fd, params.payload.data, params.payload.size, 0,
            reinterpret_cast<sockaddr*>(&server), sizeof(server));
        if (n < 0) {
            throw std::runtime_error("uds sendto failed");
        }
    }

    struct RecvResult {
        ssize_t n;
        sockaddr_un from{};
    };

    static void recv_from(int fd, Buffer& buf, RecvResult& out) {
        socklen_t from_len = sizeof(out.from);

        out.n = ::recvfrom(fd, buf.data, buf.capacity, 0,
            reinterpret_cast<sockaddr*>(&out.from), &from_len);

        if (out.n < 0) {
            throw std::runtime_error("uds recvfrom failed");
        }
        buf.size = static_cast<size_t>(out.n);
    }

    static void echo(int fd, const RecvResult& r, const Buffer& payload) {
        ssize_t sent = ::sendto(fd, payload.data, payload.size, 0,
            reinterpret_cast<const sockaddr*>(&r.from), sizeof(r.from));
        if (sent < 0) {
            throw std::runtime_error("uds echo sendto failed");
        }
    }

    static constexpr SocketConfig kCfg{AF_UNIX, SOCK_DGRAM, 0};
};

class Socket {
public:
    // RAII: Resource Acquisition Is Initialization
    explicit Socket(SocketConfig cfg):
    domain_(cfg.domain),
    data_model_type_(cfg.data_model_type),
    protocol_(cfg.protocol),
    fd_(::socket(cfg.domain, cfg.data_model_type, cfg.protocol))
    {
        if (fd_ < 0) {
            throw std::runtime_error("failed to create socket");
        }
    }

    ~Socket() {
        if (fd_ >= 0) {
            ::close(fd_);
        }
    }

    // Rule of 5: delete move & copy constructors and assignment operators
    Socket(const Socket&) = delete;
    Socket& operator=(const Socket&) = delete;
    Socket(Socket&&) = delete;
    Socket& operator=(Socket&&) = delete;

    // Provide access to the socket file descriptor (read only)
    int fd() const { return fd_;}


private:
    int domain_;
    int data_model_type_;
    int protocol_;
    int fd_;
};



template<typename Derived>
class Endpoint {
public:
    explicit Endpoint(SocketConfig cfg) : socket(cfg) {
        static_assert(std::is_convertible_v<decltype(Derived::role), Role>,
                      "Derived must have a static constexpr Role member");
    }

    int fd() const { return socket.fd(); }

protected:
    Socket socket;
};

template<typename Transport>
class Client : public Endpoint<Client<Transport>> {
public:
    static constexpr Role role = kClient;

    Client() : Endpoint<Client<Transport>>(Transport::kCfg) {}

    void connect_or_send(const typename Transport::SendParams& params) {
        Transport::connect_or_send(this->socket.fd(), params);
    }

    void recv_from(Buffer& buf, typename Transport::RecvResult& out) {
        Transport::recv_from(this->socket.fd(), buf, out);
    }
};

template<typename Transport>
class Server : public Endpoint<Server<Transport>> {
public:
    static constexpr Role role = kServer;

    Server() : Endpoint<Server<Transport>>(Transport::kCfg) {}

    void bind(const typename Transport::BindParams& params) {
        Transport::bind(this->socket.fd(), params);
    }

    void recv_from(Buffer& buf, typename Transport::RecvResult& out) {
        Transport::recv_from(this->socket.fd(), buf, out);
    }
};


template<typename Transport>
class EchoServer : public Server<Transport> {
public:
    explicit EchoServer(const typename Transport::BindParams& bind_params) {
        this->bind(bind_params);
    }

    int run() {
        char storage[1024];
        Buffer buf = Buffer::writable(storage, sizeof(storage));
        typename Transport::RecvResult recv{};

        while (true) {
            // Hot path: reuse one RecvResult (out-parameter) across iterations.
            // recvfrom overwrites n and from each trip — no return-by-value, no RVO
            // reliance. Still dwarfed by syscall cost; avoids re-allocating/constructing
            // a fresh RecvResult object every loop.
            this->recv_from(buf, recv);
            Transport::echo(this->socket.fd(), recv, buf);
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

using UdpEchoServer = EchoServer<Udp>;
using UdsEchoServer = EchoServer<Uds>;
using UdpEchoClient = EchoClient<Udp>;
using UdsEchoClient = EchoClient<Uds>;
using UdpClient = Client<Udp>;

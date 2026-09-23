#include "engine/core/socket_control_server.hpp"

#include "engine/core/control.hpp"
#include "engine/core/diagnostics.hpp"

#include <algorithm>
#include <array>
#include <cerrno>
#include <cstring>
#include <string>
#include <utility>
#include <vector>

#if defined(__linux__) && !defined(__ANDROID__)
#include <arpa/inet.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>
#endif

namespace pac::core {

struct SocketControlServer::Impl {
#if defined(__linux__) && !defined(__ANDROID__)
    struct Client {
        int fd = -1;
        std::string input;
        std::string output;
        std::size_t output_offset = 0;
    };

    static void close_client(Client& client) {
        if (client.fd >= 0) ::close(client.fd);
        client.fd = -1;
    }

    int listener = -1;
    std::uint16_t bound_port = 0;
    std::vector<Client> clients;
#endif
};

namespace {

#if defined(__linux__) && !defined(__ANDROID__)
constexpr std::size_t kMaxClients = 8;
constexpr std::size_t kMaxMessageBytes = 1024 * 1024;
constexpr std::size_t kMaxMessagesPerPoll = 32;
constexpr std::size_t kReadChunkBytes = 16 * 1024;

bool would_block() { return errno == EAGAIN || errno == EWOULDBLOCK; }

bool make_nonblocking(int fd) {
    const int flags = ::fcntl(fd, F_GETFL, 0);
    return flags >= 0 && ::fcntl(fd, F_SETFL, flags | O_NONBLOCK) == 0;
}

#endif

} // namespace

SocketControlServer::SocketControlServer() : impl_(std::make_unique<Impl>()) {}

SocketControlServer::~SocketControlServer() { stop(); }

bool SocketControlServer::start(std::uint16_t requested_port, Diagnostics& log) {
    stop();
#if defined(__linux__) && !defined(__ANDROID__)
    impl_->listener = ::socket(AF_INET, SOCK_STREAM, 0);
    if (impl_->listener < 0) {
        log.error(std::string("control server: socket failed: ") + std::strerror(errno));
        return false;
    }
    int reuse = 1;
    (void) ::setsockopt(impl_->listener, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse));
    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    address.sin_port = htons(requested_port);
    if (::bind(impl_->listener, reinterpret_cast<sockaddr*>(&address), sizeof(address)) != 0 ||
        ::listen(impl_->listener, static_cast<int>(kMaxClients)) != 0 ||
        !make_nonblocking(impl_->listener)) {
        log.error(std::string("control server: could not listen on 127.0.0.1:") +
                  std::to_string(requested_port) + ": " + std::strerror(errno));
        stop();
        return false;
    }
    socklen_t length = sizeof(address);
    if (::getsockname(impl_->listener, reinterpret_cast<sockaddr*>(&address), &length) != 0) {
        log.error(std::string("control server: getsockname failed: ") + std::strerror(errno));
        stop();
        return false;
    }
    impl_->bound_port = ntohs(address.sin_port);
    log.info("control server listening on 127.0.0.1:" + std::to_string(impl_->bound_port));
    return true;
#else
    (void) requested_port;
    log.error("control server is currently supported only on Linux desktop");
    return false;
#endif
}

void SocketControlServer::stop() {
#if defined(__linux__) && !defined(__ANDROID__)
    for (auto& client : impl_->clients) Impl::close_client(client);
    impl_->clients.clear();
    if (impl_->listener >= 0) ::close(impl_->listener);
    impl_->listener = -1;
    impl_->bound_port = 0;
#endif
}

void SocketControlServer::poll(ControlRouter& router, Diagnostics& log) {
#if defined(__linux__) && !defined(__ANDROID__)
    if (impl_->listener < 0) return;

    while (impl_->clients.size() < kMaxClients) {
        const int fd = ::accept(impl_->listener, nullptr, nullptr);
        if (fd < 0) {
            if (!would_block()) log.warn(std::string("control server: accept failed: ") +
                                         std::strerror(errno));
            break;
        }
        if (!make_nonblocking(fd)) {
            ::close(fd);
            continue;
        }
        impl_->clients.push_back({fd, {}, {}, 0});
    }

    std::size_t messages = 0;
    std::array<char, kReadChunkBytes> buffer{};
    for (auto& client : impl_->clients) {
        if (client.fd < 0) continue;
        while (true) {
            const ssize_t received = ::recv(client.fd, buffer.data(), buffer.size(), 0);
            if (received > 0) {
                client.input.append(buffer.data(), static_cast<std::size_t>(received));
                if (client.input.size() > kMaxMessageBytes) {
                    log.warn("control server: client exceeded the 1 MiB message limit");
                    Impl::close_client(client);
                    break;
                }
                continue;
            }
            if (received == 0) Impl::close_client(client);
            else if (!would_block()) Impl::close_client(client);
            break;
        }
        while (client.fd >= 0 && messages < kMaxMessagesPerPoll) {
            const std::size_t newline = client.input.find('\n');
            if (newline == std::string::npos) break;
            std::string line = client.input.substr(0, newline);
            client.input.erase(0, newline + 1);
            if (!line.empty() && line.back() == '\r') line.pop_back();
            if (line.empty()) continue;
            if (const auto response = dispatch_control_json_rpc(router, line)) {
                client.output += *response;
                client.output.push_back('\n');
                if (client.output.size() - client.output_offset > kMaxMessageBytes) {
                    log.warn("control server: client exceeded the 1 MiB response backlog limit");
                    Impl::close_client(client);
                    break;
                }
            }
            ++messages;
        }
        while (client.fd >= 0 && client.output_offset < client.output.size()) {
            const char* data = client.output.data() + client.output_offset;
            const std::size_t remaining = client.output.size() - client.output_offset;
            const ssize_t sent = ::send(client.fd, data, remaining, MSG_NOSIGNAL);
            if (sent > 0) {
                client.output_offset += static_cast<std::size_t>(sent);
                continue;
            }
            if (sent < 0 && would_block()) break;
            Impl::close_client(client);
            break;
        }
        if (client.output_offset == client.output.size()) {
            client.output.clear();
            client.output_offset = 0;
        }
    }
    impl_->clients.erase(std::remove_if(impl_->clients.begin(),
                                        impl_->clients.end(),
                                        [](const auto& client) { return client.fd < 0; }),
                         impl_->clients.end());
#else
    (void) router;
    (void) log;
#endif
}

bool SocketControlServer::running() const {
#if defined(__linux__) && !defined(__ANDROID__)
    return impl_->listener >= 0;
#else
    return false;
#endif
}

std::uint16_t SocketControlServer::port() const {
#if defined(__linux__) && !defined(__ANDROID__)
    return impl_->bound_port;
#else
    return 0;
#endif
}

} // namespace pac::core

#pragma once

#include <cstdint>
#include <memory>

namespace pac::core {

class ControlRouter;
class Diagnostics;

/// Linux-desktop development transport for ControlRouter. It accepts
/// newline-delimited JSON-RPC on 127.0.0.1 and is polled by the engine thread;
/// no handler ever runs on a networking thread.
class SocketControlServer {
public:
    SocketControlServer();
    ~SocketControlServer();
    SocketControlServer(const SocketControlServer&) = delete;
    SocketControlServer& operator=(const SocketControlServer&) = delete;

    /// Start on a requested port (0 asks the OS for an ephemeral port).
    /// Returns false on unsupported platforms or when the listener cannot start.
    bool start(std::uint16_t port, Diagnostics& log);
    void stop();
    void poll(ControlRouter& router, Diagnostics& log);

    [[nodiscard]] bool running() const;
    [[nodiscard]] std::uint16_t port() const;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace pac::core

#include "engine/core/control.hpp"
#include "engine/core/diagnostics.hpp"
#include "engine/core/scripting.hpp"
#include "engine/core/socket_control_server.hpp"
#include "core/control_services.hpp"

#include <doctest/doctest.h>

#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#if defined(__linux__) && !defined(__ANDROID__)
#include <arpa/inet.h>
#include <sys/socket.h>
#include <unistd.h>
#endif

namespace {

class EchoService final : public pac::core::ControlService {
public:
    std::vector<pac::core::ControlMethod> methods() const override {
        return {{"echo", "Return the supplied params."}, {"fail", "Return a service error."}};
    }

    pac::core::ControlResult invoke(std::string_view method,
                                    const pac::core::ControlValue& params) override {
        if (method == "echo") return params;
        if (method == "fail") return pac::core::ControlError{-32001, "expected failure", {}};
        return pac::core::ControlError{-32601, "unknown echo method", {}};
    }
};

} // namespace

TEST_CASE("control JSON round-trips nested values and Unicode") {
    const std::string json = R"({"a":[true,null,-2,1.25,"caf\u00e9"],"z":{"x":"line\n"}})";
    const auto parsed = pac::core::parse_control_json(json);
    REQUIRE(std::holds_alternative<pac::core::ControlValue>(parsed));
    const std::string encoded = pac::core::write_control_json(std::get<pac::core::ControlValue>(parsed));
    const auto reparsed = pac::core::parse_control_json(encoded);
    CHECK(std::holds_alternative<pac::core::ControlValue>(reparsed));
    CHECK(encoded.find("caf") != std::string::npos);
}

TEST_CASE("control JSON rejects malformed input") {
    const auto parsed = pac::core::parse_control_json(R"({"a": [1,})");
    REQUIRE(std::holds_alternative<pac::core::ControlError>(parsed));
    CHECK(std::get<pac::core::ControlError>(parsed).code == -32700);
}

TEST_CASE("control registration is scoped and duplicate names fail") {
    pac::core::ControlRouter router;
    EchoService first;
    EchoService second;
    auto registration = router.register_service("test", first);
    CHECK_THROWS_AS(router.register_service("test", second), std::invalid_argument);
    CHECK(std::holds_alternative<pac::core::ControlValue>(
        router.dispatch("test.echo", pac::core::ControlValue::Object{})));
    registration.reset();
    const auto missing = router.dispatch("test.echo", pac::core::ControlValue::Object{});
    REQUIRE(std::holds_alternative<pac::core::ControlError>(missing));
    CHECK(std::get<pac::core::ControlError>(missing).code == -32601);
}

TEST_CASE("control JSON-RPC dispatches requests and suppresses notifications") {
    pac::core::ControlRouter router;
    EchoService service;
    auto registration = router.register_service("test", service);
    const auto response = pac::core::dispatch_control_json_rpc(
        router,
        R"({"jsonrpc":"2.0","id":7,"method":"test.echo","params":{"value":42}})");
    REQUIRE(response);
    CHECK(response->find("\"id\":7") != std::string::npos);
    CHECK(response->find("\"value\":42") != std::string::npos);
    CHECK_FALSE(pac::core::dispatch_control_json_rpc(
        router,
        R"({"jsonrpc":"2.0","method":"test.echo","params":{}})"));
}

TEST_CASE("control router describes currently registered services") {
    pac::core::ControlRouter router;
    EchoService service;
    auto registration = router.register_service("test", service);
    const std::string description = pac::core::write_control_json(router.describe());
    CHECK(description.find("\"test\"") != std::string::npos);
    CHECK(description.find("\"echo\"") != std::string::npos);
}

TEST_CASE("Lua control service converts JSON-compatible arguments and results") {
    pac::core::Diagnostics log(pac::core::LogLevel::ERROR);
    pac::core::Scripting scripting(log);
    REQUIRE(scripting.run_string("function control_add(a, b) return a + b end"));
    pac::core::ControlRouter router;
    pac::core::LuaControlService service(scripting);
    auto registration = router.register_service("lua", service);

    const auto response = pac::core::dispatch_control_json_rpc(
        router,
        R"({"jsonrpc":"2.0","id":1,"method":"lua.call","params":{"function":"control_add","args":[19,23]}})");
    REQUIRE(response);
    CHECK(response->find("\"result\":42") != std::string::npos);

    const auto evaluated = pac::core::dispatch_control_json_rpc(
        router,
        R"({"jsonrpc":"2.0","id":2,"method":"lua.eval","params":{"code":"return {ok=true, count=3}"}})");
    REQUIRE(evaluated);
    CHECK(evaluated->find("\"ok\":true") != std::string::npos);
}

#if defined(__linux__) && !defined(__ANDROID__)
TEST_CASE("Linux socket control server routes one newline-delimited request") {
    pac::core::Diagnostics log(pac::core::LogLevel::ERROR);
    pac::core::ControlRouter router;
    EchoService service;
    auto registration = router.register_service("test", service);
    pac::core::SocketControlServer server;
    REQUIRE(server.start(0, log));
    REQUIRE(server.port() != 0);

    const int client = ::socket(AF_INET, SOCK_STREAM, 0);
    REQUIRE(client >= 0);
    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    address.sin_port = htons(server.port());
    REQUIRE(::connect(client, reinterpret_cast<sockaddr*>(&address), sizeof(address)) == 0);
    const std::string request =
        "{\"jsonrpc\":\"2.0\",\"id\":9,\"method\":\"test.echo\",\"params\":{\"ok\":true}}\n";
    REQUIRE(::send(client, request.data(), request.size(), 0) ==
            static_cast<ssize_t>(request.size()));
    server.poll(router, log);

    char response[512]{};
    const ssize_t received = ::recv(client, response, sizeof(response), 0);
    CHECK(received > 0);
    CHECK(std::string(response, static_cast<std::size_t>(received)).find("\"ok\":true") !=
          std::string::npos);
    ::close(client);
}
#endif

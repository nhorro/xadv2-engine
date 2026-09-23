#pragma once

#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

namespace pac::core {

/// Transport-neutral value used by the development control endpoint.
struct ControlValue {
    using Array = std::vector<ControlValue>;
    using Object = std::map<std::string, ControlValue>;
    using Storage =
        std::variant<std::nullptr_t, bool, std::int64_t, double, std::string, Array, Object>;

    Storage value = nullptr;

    ControlValue() = default;
    ControlValue(std::nullptr_t) : value(nullptr) {}
    ControlValue(bool item) : value(item) {}
    ControlValue(int item) : value(static_cast<std::int64_t>(item)) {}
    ControlValue(std::int64_t item) : value(item) {}
    ControlValue(double item) : value(item) {}
    ControlValue(const char* item) : value(std::string(item)) {}
    ControlValue(std::string item) : value(std::move(item)) {}
    ControlValue(Array item) : value(std::move(item)) {}
    ControlValue(Object item) : value(std::move(item)) {}

    [[nodiscard]] bool is_object() const { return std::holds_alternative<Object>(value); }
    [[nodiscard]] bool is_array() const { return std::holds_alternative<Array>(value); }
    [[nodiscard]] const Object* object() const { return std::get_if<Object>(&value); }
    [[nodiscard]] Object* object() { return std::get_if<Object>(&value); }
    [[nodiscard]] const Array* array() const { return std::get_if<Array>(&value); }
    [[nodiscard]] Array* array() { return std::get_if<Array>(&value); }
};

struct ControlError {
    int code = -32603;
    std::string message;
    std::optional<ControlValue> data;
};

using ControlResult = std::variant<ControlValue, ControlError>;

struct ControlMethod {
    std::string name;
    std::string summary;
};

/// One dynamically registered namespace (for example `lua` or `room`).
class ControlService {
public:
    virtual ~ControlService() = default;
    [[nodiscard]] virtual std::vector<ControlMethod> methods() const = 0;
    virtual ControlResult invoke(std::string_view method, const ControlValue& params) = 0;
};

class ControlRouter;

/// Move-only registration token. Destruction unregisters only the exact service
/// generation it represents, so a late scene destructor cannot remove a newer
/// replacement registered under the same namespace.
class ControlRegistration {
public:
    ControlRegistration() = default;
    ~ControlRegistration();
    ControlRegistration(const ControlRegistration&) = delete;
    ControlRegistration& operator=(const ControlRegistration&) = delete;
    ControlRegistration(ControlRegistration&& other) noexcept;
    ControlRegistration& operator=(ControlRegistration&& other) noexcept;

    void reset();
    [[nodiscard]] explicit operator bool() const { return router_ != nullptr; }

private:
    friend class ControlRouter;
    ControlRegistration(ControlRouter& router, std::string service, std::uint64_t generation);

    ControlRouter* router_ = nullptr;
    std::string service_;
    std::uint64_t generation_ = 0;
};

/// Synchronous, transport-independent service router. All dispatch is expected
/// on the engine thread; transports queue or poll work into that thread.
class ControlRouter {
public:
    ControlRegistration register_service(std::string name, ControlService& service);
    [[nodiscard]] ControlResult dispatch(std::string_view method, const ControlValue& params);
    [[nodiscard]] ControlValue describe() const;

private:
    friend class ControlRegistration;
    void unregister_service(const std::string& name, std::uint64_t generation);

    struct Entry {
        ControlService* service = nullptr;
        std::uint64_t generation = 0;
    };
    std::map<std::string, Entry> services_;
    std::uint64_t next_generation_ = 1;
};

/// JSON codec and one-message JSON-RPC 2.0 dispatcher used by socket adapters
/// and headless protocol tests. A nullopt response denotes a notification.
ControlResult parse_control_json(std::string_view json);
std::string write_control_json(const ControlValue& value);
std::optional<std::string> dispatch_control_json_rpc(ControlRouter& router,
                                                     std::string_view request);

} // namespace pac::core

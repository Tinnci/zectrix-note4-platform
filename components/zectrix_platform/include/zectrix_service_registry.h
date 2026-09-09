#pragma once

#include <array>
#include <cstddef>
#include <type_traits>

#include "esp_err.h"

namespace zectrix {

// The composition owner serializes lifecycle calls. Services do not own tasks
// merely by implementing this interface. Stop must handle partial Init/Start,
// quiesce consumers before returning, and never perform a power transition.
class Service {
public:
    virtual ~Service() = default;
    virtual esp_err_t Init() = 0;
    virtual esp_err_t Start() = 0;
    virtual esp_err_t Stop() = 0;
};

// An adapter can expose an abstract interface or an existing service facade.
// GetInterface is side-effect-free. Its pointer stays valid and unchanged from
// successful Start until Stop.
template <typename Interface>
class ServiceProvider : public Service {
public:
    virtual Interface* GetInterface() = 0;
};

// Fixed storage and borrowed providers; no RTTI, allocator, task or global
// registry. Register dependencies first. The owner stops and clears the
// registry before destroying providers. All registry calls use that owner;
// concurrent access is not supported. Callbacks must not throw.
class ServiceRegistry {
public:
    static constexpr std::size_t kCapacity = 16;

    ServiceRegistry() = default;
    ServiceRegistry(const ServiceRegistry&) = delete;
    ServiceRegistry& operator=(const ServiceRegistry&) = delete;

    template <typename Interface>
    esp_err_t Register(ServiceProvider<Interface>& provider) {
        static_assert(std::is_class_v<Interface> &&
                      std::is_same_v<Interface, std::remove_cv_t<Interface>>);
        return Add(&type_key_<Interface>, provider, [](Service& service) -> void* {
            return static_cast<ServiceProvider<Interface>&>(service).GetInterface();
        });
    }

    // Missing, not-yet-started and stopped interfaces return nullptr. The
    // registry is read-only to consumers; the borrowed service can be mutable.
    template <typename Interface>
    Interface* Get() const {
        return static_cast<Interface*>(Find(&type_key_<std::remove_cv_t<Interface>>));
    }

    // Run Init then Start for each provider before moving to its dependents.
    // A failure stops the attempted provider and earlier ones in reverse order.
    esp_err_t StartAll();
    esp_err_t StopAll();
    esp_err_t Clear();
    std::size_t size() const { return count_; }

private:
    enum class Phase { Configuring, Starting, Running, Stopping, Stopped };
    struct Entry {
        const void* key = nullptr;
        Service* service = nullptr;
        void* (*get)(Service&) = nullptr;
        bool entered = false;
        bool running = false;
    };

    // A type has one address across translation units. Keys are process-local
    // identity only; they are never serialized or used as persistent IDs.
    template <typename Interface> inline static char type_key_ = 0;
    esp_err_t Add(const void* key, Service& service, void* (*get)(Service&));
    void* Find(const void* key) const;
    esp_err_t StopEntered();

    std::array<Entry, kCapacity> entries_{};
    std::size_t count_ = 0;
    Phase phase_ = Phase::Configuring;
};

}  // namespace zectrix

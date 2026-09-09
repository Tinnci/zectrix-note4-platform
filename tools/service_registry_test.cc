#include "zectrix_service_registry.h"

#include <cassert>
#include <cstdio>
#include <cstdlib>
#include <new>
#include <tuple>
#include <utility>

namespace {
using zectrix::ServiceRegistry;

unsigned allocations = 0;
struct Trace {
    std::array<unsigned, 128> events{};
    std::size_t size = 0;
    void Add(unsigned event) { assert(size < events.size()); events[size++] = event; }
};

template <unsigned Id>
struct Value {
    virtual ~Value() = default;
    virtual unsigned Read() const = 0;
};

struct State {
    esp_err_t init_result = ESP_OK, start_result = ESP_OK, stop_result = ESP_OK;
    unsigned init_calls = 0, start_calls = 0, stop_calls = 0;
    bool resource = false, active = false, expose = true, reenter = false;
};

template <unsigned Id>
class Provider final : public zectrix::ServiceProvider<Value<Id>>, public Value<Id> {
public:
    Provider(ServiceRegistry& registry, Trace& trace) : registry_(registry), trace_(trace) {}
    State state;
    bool needs_first = Id != 0;

    esp_err_t Init() override {
        CheckCallback();
        trace_.Add(Id * 10 + 1);
        ++state.init_calls;
        state.resource = true;
        if (needs_first && !registry_.Get<Value<0>>()) return ESP_ERR_NOT_FOUND;
        return state.init_result;
    }
    esp_err_t Start() override {
        CheckCallback();
        assert(state.resource);
        trace_.Add(Id * 10 + 2);
        ++state.start_calls;
        state.active = true;
        return state.start_result;
    }
    esp_err_t Stop() override {
        CheckCallback();
        trace_.Add(Id * 10 + 3);
        ++state.stop_calls;
        if (needs_first && state.active) assert(registry_.Get<Value<0>>());
        state.active = state.resource = false;
        return state.stop_result;
    }
    Value<Id>* GetInterface() override { return state.expose ? this : nullptr; }
    unsigned Read() const override { return Id + 100; }

private:
    void CheckCallback() {
        assert(registry_.Get<Value<Id>>() == nullptr);
        if (!state.reenter) return;
        assert(registry_.StartAll() == ESP_ERR_INVALID_STATE);
        assert(registry_.StopAll() == ESP_ERR_INVALID_STATE);
        assert(registry_.Clear() == ESP_ERR_INVALID_STATE);
        assert(registry_.Register(*this) == ESP_ERR_INVALID_STATE);
    }
    ServiceRegistry& registry_;
    Trace& trace_;
};

void TestLifecycle() {
    ServiceRegistry registry;
    Trace trace;
    Provider<0> clock(registry, trace), duplicate(registry, trace);
    Provider<1> reader(registry, trace);
    clock.state.reenter = reader.state.reenter = true;
    assert(!registry.Get<Value<0>>());
    assert(registry.Register(clock) == ESP_OK);
    assert(registry.Register(clock) == ESP_ERR_INVALID_STATE);
    assert(registry.Register(duplicate) == ESP_ERR_INVALID_STATE);
    assert(registry.Register(reader) == ESP_OK && registry.size() == 2);
    assert(!registry.Get<Value<0>>() && !registry.Get<Value<1>>());
    assert(registry.StartAll() == ESP_OK);
    const auto* clock_interface = static_cast<Value<0>*>(&clock);
    assert(registry.Get<const Value<0>>() == clock_interface);
    assert(registry.Get<Value<1>>()->Read() == 101);
    assert(!registry.Get<Value<2>>());
    assert(registry.Register(duplicate) == ESP_ERR_INVALID_STATE);
    assert(registry.Clear() == ESP_ERR_INVALID_STATE);
    assert(registry.StartAll() == ESP_OK && trace.size == 4);
    assert(registry.StopAll() == ESP_OK);
    assert(!registry.Get<Value<0>>() && !registry.Get<Value<1>>());
    assert(registry.StopAll() == ESP_OK && trace.size == 6);
    const unsigned expected[] = {1, 2, 11, 12, 13, 3};
    for (unsigned i = 0; i < std::size(expected); ++i) assert(trace.events[i] == expected[i]);
    assert(registry.StartAll() == ESP_ERR_INVALID_STATE);
    assert(registry.Clear() == ESP_OK && registry.size() == 0);
    assert(!duplicate.state.init_calls && !duplicate.state.stop_calls);
}

void TestRollback() {
    for (unsigned failed = 0; failed < 3; ++failed) {
        for (unsigned phase = 0; phase < 3; ++phase) {
            ServiceRegistry registry;
            Trace trace;
            Provider<0> first(registry, trace);
            Provider<1> second(registry, trace);
            Provider<2> third(registry, trace);
            State* states[] = {&first.state, &second.state, &third.state};
            if (phase == 0) states[failed]->init_result = ESP_FAIL;
            if (phase == 1) states[failed]->start_result = ESP_FAIL;
            if (phase == 2) states[failed]->expose = false;
            assert(registry.Register(first) == ESP_OK);
            assert(registry.Register(second) == ESP_OK);
            assert(registry.Register(third) == ESP_OK);
            assert(registry.StartAll() == (phase == 2 ? ESP_ERR_INVALID_STATE : ESP_FAIL));
            for (unsigned i = 0; i < 3; ++i) {
                assert(states[i]->init_calls == (i <= failed ? 1U : 0U));
                assert(states[i]->start_calls == (i < failed || (i == failed && phase != 0) ? 1U : 0U));
                assert(states[i]->stop_calls == (i <= failed ? 1U : 0U));
                assert(!states[i]->resource && !states[i]->active);
            }
            for (unsigned i = 0; i <= failed; ++i)
                assert(trace.events[trace.size - 1 - i] == i * 10 + 3);
            assert(!registry.Get<Value<0>>() && !registry.Get<Value<1>>() && !registry.Get<Value<2>>());
            assert(registry.StopAll() == ESP_OK);
            assert(registry.StartAll() == ESP_ERR_INVALID_STATE);
            assert(registry.Clear() == ESP_OK);
        }
    }
}

void TestStopErrorsAndOptionalServices() {
    ServiceRegistry registry;
    Trace trace;
    Provider<0> first(registry, trace);
    Provider<1> second(registry, trace);
    first.state.stop_result = ESP_FAIL;
    second.state.stop_result = ESP_ERR_INVALID_RESPONSE;
    assert(registry.Register(first) == ESP_OK && registry.Register(second) == ESP_OK);
    assert(registry.StartAll() == ESP_OK);
    assert(registry.StopAll() == ESP_ERR_INVALID_RESPONSE);
    assert(first.state.stop_calls == 1 && second.state.stop_calls == 1);
    assert(!first.state.resource && !second.state.resource);
    assert(!registry.Get<Value<0>>() && !registry.Get<Value<1>>());
    assert(registry.StopAll() == ESP_OK && registry.Clear() == ESP_OK);

    Provider<2> optional(registry, trace);
    assert(registry.Register(optional) == ESP_OK);
    assert(registry.StartAll() == ESP_ERR_NOT_FOUND);
    assert(optional.state.stop_calls == 1 && !optional.state.resource);
    assert(registry.Clear() == ESP_OK);
    optional.needs_first = false;
    assert(registry.Register(optional) == ESP_OK);
    assert(registry.StartAll() == ESP_OK);
    assert(!registry.Get<Value<0>>() && registry.Get<Value<2>>()->Read() == 102);
    assert(registry.StopAll() == ESP_OK && registry.Clear() == ESP_OK);
    assert(registry.StartAll() == ESP_OK && registry.StopAll() == ESP_OK);
    assert(registry.Clear() == ESP_OK);
}

template <std::size_t... Id>
void TestCapacity(std::index_sequence<Id...>) {
    ServiceRegistry registry;
    Trace trace;
    std::tuple<Provider<Id>...> providers{Provider<Id>{registry, trace}...};
    assert(((registry.Register(std::get<Id>(providers)) == ESP_OK) && ...));
    Provider<ServiceRegistry::kCapacity> extra(registry, trace);
    assert(registry.size() == ServiceRegistry::kCapacity);
    assert(registry.Register(extra) == ESP_ERR_NO_MEM);
    assert(registry.StartAll() == ESP_OK);
    assert(((registry.Get<Value<Id>>() == std::get<Id>(providers).GetInterface()) && ...));
    assert(!registry.Get<Value<ServiceRegistry::kCapacity>>());
    assert(registry.StopAll() == ESP_OK && registry.Clear() == ESP_OK);
    assert(!extra.state.init_calls && !extra.state.stop_calls);
}
}  // namespace

void* operator new(std::size_t size) {
    ++allocations;
    if (void* pointer = std::malloc(size)) return pointer;
    std::abort();
}
void* operator new[](std::size_t size) { return ::operator new(size); }
void operator delete(void* pointer) noexcept { std::free(pointer); }
void operator delete[](void* pointer) noexcept { ::operator delete(pointer); }
void operator delete(void* pointer, std::size_t) noexcept { ::operator delete(pointer); }
void operator delete[](void* pointer, std::size_t) noexcept { ::operator delete(pointer); }

int main() {
    const unsigned before = allocations;
    TestLifecycle();
    TestRollback();
    TestStopErrorsAndOptionalServices();
    TestCapacity(std::make_index_sequence<ServiceRegistry::kCapacity>{});
    assert(allocations == before);
    std::printf("MEASURE: ServiceRegistry=%zu bytes, capacity=%zu, lifecycle heap allocations=0.\n",
                sizeof(ServiceRegistry), ServiceRegistry::kCapacity);
    std::puts("PASS: typed service lookup, bounded registration, lifecycle rollback and cleanup.");
}

#include "zectrix_companion_identity.h"
#include "zectrix_companion_protocol.h"
#include "zectrix_clock_sync.h"
#include "zectrix_enrollment_ndef.h"
#include "zectrix_pairing_bootstrap.h"
#include "zectrix_sync_session.h"
#include "zectrix_weather_sync.h"

#include <cassert>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>
#include <unistd.h>

// A process boundary for the production codecs, bootstrap and durable engine.
// The test transport deliberately has no Bluetooth or RF implementation.
namespace {
using namespace zectrix::companion;
std::string Hex(const uint8_t* bytes, std::size_t size) {
    constexpr char digits[] = "0123456789abcdef";
    std::string result;
    for (std::size_t i = 0; i < size; ++i) { result += digits[bytes[i] >> 4]; result += digits[bytes[i] & 15]; }
    return result;
}
std::vector<uint8_t> Unhex(const std::string& value) {
    assert(value.size() % 2 == 0 && value.size() <= kMaximumFrameSize * 2);
    std::vector<uint8_t> result;
    for (std::size_t i = 0; i < value.size(); i += 2) result.push_back(std::stoul(value.substr(i, 2), nullptr, 16));
    return result;
}
class FileStore final : public SyncStore {
public:
    explicit FileStore(std::string path) : path_(std::move(path)) {}
    StoreReadStatus Load(uint8_t* output, std::size_t capacity, std::size_t* size) override {
        if (!std::filesystem::exists(path_)) return StoreReadStatus::kNotFound;
        const auto length = std::filesystem::file_size(path_);
        if (length > capacity) return StoreReadStatus::kError;
        std::ifstream input(path_, std::ios::binary);
        input.read(reinterpret_cast<char*>(output), length);
        *size = length;
        return input ? StoreReadStatus::kOk : StoreReadStatus::kError;
    }
    bool Save(const uint8_t* input, std::size_t size) override {
        if (fail_next) { fail_next = false; return false; }
        const auto temporary = path_ + ".tmp";
        auto* file = std::fopen(temporary.c_str(), "wb");
        if (!file) return false;
        const bool saved = std::fwrite(input, 1, size, file) == size && std::fflush(file) == 0 && fsync(fileno(file)) == 0;
        const bool closed = std::fclose(file) == 0;
        if (!saved || !closed || std::rename(temporary.c_str(), path_.c_str()) != 0) return false;
        ++writes;
        return true;
    }
    unsigned writes = 0;
    bool fail_next = false;
private:
    std::string path_;
};
class Clock final : public PairingBootstrapClock {
public:
    uint32_t now = 0;
    uint32_t MonotonicMilliseconds() const override { return now; }
};
class Random final : public PairingBootstrapRandom {
public:
    bool Fill(BootstrapToken* token) override {
        // Reproducible fixture bytes are never linked into firmware.
        for (unsigned i = 0; i < token->size(); ++i) (*token)[i] = 0x31 + i + generation;
        ++generation;
        return true;
    }
    unsigned generation = 0;
};
class Sender final : public SyncFrameSender {
public:
    LinkResult SendSyncFrame(const uint8_t* frame, std::size_t size) override {
        bytes.assign(frame, frame + size); return LinkResult::kOk;
    }
    std::vector<uint8_t> bytes;
};
}  // namespace

int main(int argc, char** argv) {
    assert(argc == 2);
    std::filesystem::create_directories(argv[1]);
    FileStore store(std::string(argv[1]) + "/sync.bin");
    FileStore identity(std::string(argv[1]) + "/identity.bin");
    SyncEngine engine;
    assert(engine.Initialize(store) == SyncStatus::kOk);
    SyncSession session(engine);
    FragmentReassembler fragments;
    Clock clock; Random random; PairingBootstrap bootstrap(clock, random);
    uint32_t sequence = 1;
    uint16_t frame_id = 1;
    std::string line;
    while (std::getline(std::cin, line)) {
        assert(line.size() < 10000);
        std::istringstream input(line);
        std::string command, value;
        input >> command;
        if (command == "cursors") {
            std::array<uint8_t, kSyncCursorValueSize> bytes{};
            std::size_t size = 0;
            assert(EncodeSyncCursors(engine.Cursors(), bytes.data(), bytes.size(), &size) == ProtocolStatus::kOk);
            std::cout << Hex(bytes.data(), size);
        } else if (command == "start") {
            input >> value;
            const auto bytes = Unhex(value); SyncCursors cursors;
            assert(DecodeSyncCursors(bytes.data(), bytes.size(), &cursors) == ProtocolStatus::kOk);
            std::cout << static_cast<unsigned>(session.Start(cursors, clock.now));
            sequence = 1; fragments.Reset();
        } else if (command == "put") {
            unsigned key, revision; input >> key >> revision >> value;
            const auto bytes = Unhex(value);
            std::cout << static_cast<unsigned>(engine.PutDurableState(key, revision, bytes.data(), bytes.size()));
        } else if (command == "packet") {
            input >> value; const auto bytes = Unhex(value);
            const auto status = fragments.Accept(bytes.data(), bytes.size());
            if (status == ProtocolStatus::kFrameComplete) {
                FrameView frame;
                assert(DecodeFrame(fragments.Data(), fragments.Size(), 1, 0, &frame) == ProtocolStatus::kOk);
                std::cout << (session.Receive(frame) ? "ok" : "rejected");
            } else {
                assert(status == ProtocolStatus::kFragmentAccepted);
                std::cout << "partial";
            }
        } else if (command == "poll") {
            unsigned packet_size; input >> clock.now >> packet_size;
            Sender sender;
            session.Poll(sender, sequence, clock.now);
            if (sender.bytes.empty()) std::cout << "none";
            else {
                const auto count = FragmentCount(sender.bytes.size(), packet_size);
                for (std::size_t i = 0; i < count; ++i) {
                    std::array<uint8_t, 512> packet{}; std::size_t size = 0;
                    assert(EncodeFragment(sender.bytes.data(), sender.bytes.size(), frame_id, i, packet_size,
                        packet.data(), packet.size(), &size) == ProtocolStatus::kOk);
                    if (i) std::cout << ',';
                    std::cout << Hex(packet.data(), size);
                }
                ++frame_id;
            }
        } else if (command == "read") {
            unsigned key; input >> key; DurableStateView state;
            if (engine.ReadIncomingState(key, &state) != SyncStatus::kOk) std::cout << "missing";
            else std::cout << state.revision << ' ' << Hex(state.value, state.value_size);
        } else if (command == "converged") std::cout << (session.Converged() ? "yes" : "no");
        else if (command == "writes") std::cout << store.writes;
        else if (command == "fail-save") { store.fail_next = true; std::cout << "ok"; }
        else if (command == "ndef") {
            assert(bootstrap.Prepare() == BootstrapStatus::kOk);
            BootstrapMaterial material;
            assert(bootstrap.Material(&material) == BootstrapStatus::kOk);
            EnrollmentNdefPayload payload;
            payload.flags = kEnrollmentNdefFlagBleAddressValid;
            payload.ble_address_type = 0;
            payload.ble_address = {0x12, 0x34, 0x56, 0x78, 0x9a, 0xbc};
            payload.device_id.fill(0x42);
            payload.generation = material.generation; payload.token = material.token;
            std::vector<uint8_t> bytes(EnrollmentNdefMessageSize()); std::size_t size = 0;
            assert(EncodeEnrollmentNdefMessage(payload, bytes.data(), bytes.size(), &size) == EnrollmentNdefStatus::kOk);
            std::cout << Hex(bytes.data(), size);
        } else if (command == "field") std::cout << static_cast<unsigned>(bootstrap.OpenPairingWindow());
        else if (command == "time") { input >> clock.now; std::cout << "ok"; }
        else if (command == "proof") {
            unsigned fail = 0; input >> value >> fail;
            const auto bytes = Unhex(value);
            uint32_t generation = 0; uint8_t token[16]{}, peer[16]{};
            assert(DecodeEnrollmentProofValue(bytes.data(), bytes.size(), &generation, token, peer) == ProtocolStatus::kOk);
            std::cout << static_cast<unsigned>(bootstrap.ValidateAndPersistEnrollmentProof(42, generation, token, sizeof(token), [&] {
                CompanionIdentityRecord record{0x3150435aU, 1, 0, {}, generation};
                std::memcpy(record.companion_id, peer, sizeof(peer));
                std::array<uint8_t, kCompanionIdentityRecordSize> stored{};
                EncodeCompanionIdentityRecord(record, stored.data());
                identity.fail_next = fail != 0;
                return identity.Save(stored.data(), stored.size());
            }));
        } else if (command == "identity") {
            std::array<uint8_t, kCompanionIdentityRecordSize> stored{}; std::size_t size = 0;
            CompanionIdentityRecord record;
            if (identity.Load(stored.data(), stored.size(), &size) == StoreReadStatus::kOk &&
                DecodeCompanionIdentityRecord(stored.data(), size, &record)) std::cout << Hex(record.companion_id, 16);
            else std::cout << "missing";
        } else if (command == "clock") {
            unsigned authorized, age; input >> value >> authorized >> age;
            const auto bytes = Unhex(value); ClockSample sample;
            assert(DecodeClockSampleValue(bytes.data(), bytes.size(), &sample) == ProtocolStatus::kOk);
            ClockMailbox mailbox; mailbox.Offer(sample, 42, 0);
            if (mailbox.Take(authorized ? 42 : 0, age, &sample))
                std::cout << sample.unix_milliseconds << ' ' << sample.utc_offset_seconds;
            else std::cout << "rejected";
        } else if (command == "weather") {
            int64_t now; input >> value >> now;
            const auto bytes = Unhex(value); WeatherSnapshot weather;
            if (DecodeWeatherSnapshot(bytes.data(), bytes.size(), &weather) && weather.Fresh(now))
                std::cout << weather.deci_celsius << ' ' << unsigned(weather.code) << ' ' << weather.place.data();
            else std::cout << "rejected";
        } else assert(false);
        std::cout << std::endl;
    }
}

#include "digit_codec.h"
#include "large_digits.h"
#include <cassert>
#include <chrono>
#include <cstdio>
#include <fstream>
#include <vector>

struct Record { unsigned width, height, codec; std::vector<uint8_t> data, raw; };
unsigned Read16(std::ifstream& input) {
    const auto low = input.get(), high = input.get();
    assert(low >= 0 && high >= 0);
    return static_cast<unsigned>(low | (high << 8));
}

int main(int argc, char** argv) {
    assert(argc == 2);
    std::ifstream input(argv[1], std::ios::binary);
    assert(input);
    std::vector<Record> records;
    while (input.peek() != EOF) {
        Record record;
        record.width = Read16(input); record.height = Read16(input);
        const int codec = input.get(); assert(codec >= 0);
        record.codec = static_cast<unsigned>(codec);
        const auto size = Read16(input);
        record.data.resize(size);
        record.raw.resize((record.width * record.height + 7) / 8);
        assert(input.read(reinterpret_cast<char*>(record.data.data()), size));
        assert(input.read(reinterpret_cast<char*>(record.raw.data()), record.raw.size()));
        unsigned emitted = 0;
        assert(Note4DecodeDigit(record.data.data(), size, record.width, record.height, record.codec,
            [&](unsigned x, unsigned y, bool black) {
                const auto bit = y * record.width + x;
                assert(black == ((record.raw[bit / 8] & (128 >> (bit % 8))) != 0));
                ++emitted;
            }));
        assert(emitted == record.width * record.height);
        records.push_back(record);
    }
    assert(!records.empty());
    auto noop = [](unsigned, unsigned, bool) {};
    const uint8_t one[] = {0}, extra[] = {0, 0}, overrun[] = {127};
    assert(!Note4DecodeDigit(nullptr, 1, 1, 1, 0, noop));
    assert(!Note4DecodeDigit(one, 1, 0, 1, 0, noop));
    assert(!Note4DecodeDigit(one, 1, 43, 1, 0, noop));
    assert(!Note4DecodeDigit(one, 1, 1, 129, 0, noop));
    assert(!Note4DecodeDigit(one, 1, 1, 1, 4, noop));
    assert(!Note4DecodeDigit(one, 0, 1, 1, 0, noop));
    assert(!Note4DecodeDigit(one, 0, 1, 1, 1, noop));
    assert(!Note4DecodeDigit(extra, 2, 1, 1, 1, noop));
    assert(!Note4DecodeDigit(overrun, 1, 1, 1, 2, noop));
    assert(!Note4DecodeDigit<false>(one, 1, 1, 1, 2, noop));
    assert((!Note4DecodeDigit<true, false>(one, 1, 1, 1, 3, noop)));
    for (const auto& glyph : kNote4DigitGlyphs) {
        assert(glyph.offset + glyph.size <= sizeof(kNote4DigitData));
        assert(Note4DecodeDigit(kNote4DigitData + glyph.offset, glyph.size, glyph.width, 48, glyph.codec, noop));
        assert((Note4DecodeDigit<kNote4DigitUsesXor, kNote4DigitUsesColumn>(
            kNote4DigitData + glyph.offset, glyph.size, glyph.width, 48, glyph.codec, noop)));
    }
    volatile uint64_t checksum = 0;
    for (unsigned codec = 0; codec < 4; ++codec) {
        uint64_t pixels = 0;
        const auto started = std::chrono::steady_clock::now();
        for (unsigned repeat = 0; repeat < 128; ++repeat) for (const auto& record : records) {
            if (record.codec != codec) continue;
            pixels += record.width * record.height;
            assert(Note4DecodeDigit(record.data.data(), record.data.size(), record.width, record.height, codec,
                [&](unsigned x, unsigned y, bool black) { checksum += black + x + y; }));
        }
        const auto elapsed = std::chrono::duration<double, std::nano>(std::chrono::steady_clock::now() - started).count();
        std::printf("MEASURE: host C++ codec=%u ns/pixel=%.3f pixels=%llu; NOT MCU energy/latency.\n",
                    codec, elapsed / pixels, static_cast<unsigned long long>(pixels));
    }
    std::printf("PASS: Python/C++ bit-exact corpus=%zu, native glyphs=50; checksum=%llu.\n",
                records.size(), static_cast<unsigned long long>(checksum));
}

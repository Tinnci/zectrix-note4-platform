#include "ssd2683_waveform_catalog.h"
#include <cstdio>
#include <cstring>
#include <type_traits>

namespace {
const char* selected = nullptr;
bool found = false;
template <typename T, std::size_t N>
void Visit(const char* name, const std::array<T, N>& items, bool production = false) {
    if constexpr (std::is_same_v<T, uint8_t> && N == ssd2683_waveform_catalog::kWaveformSize) {
        if (!selected)
            std::printf("%s bytes=%zu default_sequence=%u\n", name, N, production);
        else if (!std::strcmp(name, selected)) {
            found = true;
            std::printf("{\"name\":\"%s\",\"bytes\":%zu,\"default_sequence\":%s,\"payload\":[",
                        name, N, production ? "true" : "false");
            for (std::size_t i = 0; i < N; ++i)
                std::printf("%s%u", i ? "," : "", items[i]);
            std::puts("]}");
        }
    } else {
        for (std::size_t i = 0; i < N; ++i) {
            char child[128];
            std::snprintf(child, sizeof(child), "%s.%zu", name, i);
            Visit(child, items[i], production);
        }
    }
}
} // namespace

int main(int argc, char** argv) {
    if (argc == 3 && !std::strcmp(argv[1], "--dump"))
        selected = argv[2];
    else if (argc != 2 || std::strcmp(argv[1], "--list")) {
        std::fprintf(stderr,
                     "Usage: %s --list | --dump <catalog-name>\nOffline only; never sends "
                     "controller commands.\n",
                     argv[0]);
        return 2;
    }
    using namespace ssd2683_waveform_catalog;
    Visit("fast-bw", kFastBwWaveform);
    Visit("gray4", kGray4Waveform);
    Visit("all-black", kAllBlackWaveform);
    Visit("white-stripe", kFullWhiteStripeWaveform);
    Visit("gray16-unit", kGray16UnitWeightWaveform);
    Visit("gray16-reset-double", kGray16ResetDoubleWeightWaveform);
    Visit("gray16-reset-unit", kGray16ResetUnitWeightWaveform);
    Visit("gray16-reset-unit-120hz", kGray16ResetUnitWeight120HzWaveform);
    Visit("gray16-unit-120hz", kGray16UnitWeight120HzWaveform);
    Visit("weak-white-fields", kWeakWhiteFieldDiagnosticWaveforms);
    Visit("weak-white-rates", kWeakWhiteRateDiagnosticWaveforms);
    Visit("vendor-gray4", kVendorGray4Waveform);
    Visit("vendor-gray4-no-reset", kVendorGray4NoResetWaveform);
    Visit("vendor-gray4-incremental", kVendorGray4IncrementalWaveform);
    Visit("vendor-screening", kVendorScreeningWaveforms);
    Visit("vendor-gray16", kVendorGray16RenderWaveforms, true);
    Visit("vendor-comparison", kVendorGray16SetWaveforms);
    if (selected && !found) {
        std::fprintf(stderr, "Unknown catalog entry: %s\n", selected);
        return 2;
    }
    return 0;
}

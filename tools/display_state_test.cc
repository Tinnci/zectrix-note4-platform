#include "zectrix_display_telemetry.h"

#include <cassert>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <initializer_list>

using namespace zectrix::display;

void TestLifecycleAndDirtyBounds() {
    StateModel model;
    assert(!model.CanUsePartial());
    model.OnPartial1BppSuccess({1, 1, 2, 2}, 4);
    assert(model.state().partial_refresh_count == 0 && model.state().partial_changed_pixels == 0);
    model.OnFull1BppSuccess();
    assert(model.CanUsePartial());
    model.OnPartial1BppSuccess({10, 20, 5, 6}, 30);
    model.OnPartial1BppSuccess({0, 0, 4, 8}, 32);
    assert(model.state().partial_refresh_count == 2 && model.state().partial_changed_pixels == 62);
    const auto& dirty = model.state().dirty_region;
    assert(dirty.x == 0 && dirty.y == 0 && dirty.width == 15 && dirty.height == 26);
    model.OnPartial1BppSuccess({}, 5);
    model.OnPartial1BppSuccess({0, 0, 400, 300}, 0);
    assert(model.state().partial_refresh_count == 2 && model.state().partial_changed_pixels == 62);
    // Diagnostic counters no longer saturate at the former scheduling limits.
    for (unsigned i = 0; i < 20; ++i) model.OnPartial1BppSuccess({0, 0, 1, 1}, 1);
    assert(model.state().partial_refresh_count == 22);
    model.OnPartial1BppSuccess({0, 0, 400, 300}, UINT32_MAX);
    model.OnPartial1BppSuccess({0, 0, 400, 300}, UINT32_MAX);
    assert(model.state().partial_changed_pixels == UINT32_MAX);
    for (bool gray : {false, true}) {
        model.OnFull1BppSuccess();
        model.OnPartial1BppSuccess({0, 0, 400, 30}, 12000);
        if (gray) model.OnFull4BppSuccess(); else model.OnRefreshError();
        assert(!model.CanUsePartial() && !model.state().has_dirty_region);
        assert(model.state().partial_refresh_count == 0 && model.state().partial_changed_pixels == 0);
    }
}

FrameActivity Page(unsigned pixels_per_tile) {
    FrameActivity activity;
    activity.window = {0, 0, 400, 300};
    activity.valid = true;
    for (auto& tile : activity.tiles) tile.white_to_black = pixels_per_tile;
    return activity;
}

void TestModelEquations() {
    PhysicsModel model;
    const PhysicsEnvironment warm{2500, 3900, 0, 0};
    auto activity = Page(300);
    const auto first = model.Predict(activity, warm, 0);
    assert(!first.RequiresFull() && first.gain_q8 == 256 && model.state().Peak() == 0);
    // Independent real-valued calculation of the documented equation.
    const double f = 300.0 / 6000.0;
    const double reference = (0.125 + f + f * f + 0.25 * f * f) * kDebtOne;
    assert(std::abs(first.next.Peak() - reference) < 4);
    model.Commit(first, 0);
    const auto same = model.Predict(activity, warm, 0);
    for (auto& tile : activity.tiles) tile = {300, 0};
    const auto reversed = model.Predict(activity, warm, 0);
    assert(reversed.next.memory_q16[0] == 0);
    assert(reversed.next.Peak() > first.next.Peak());
    assert(same.next.Peak() > reversed.next.Peak());
    assert(model.state().Peak() == first.next.Peak());

    // Default ghosting debt cannot disappear just because the device was idle.
    const auto idle = model.Predict(activity, warm, 3600000000ULL);
    assert(idle.next.Peak() >= model.state().Peak());
    auto parameters = model.parameters();
    parameters.debt_tau_ms = 1000;
    parameters.revision = 7;
    assert(model.SetParameters(parameters));
    assert(model.state().Peak() == first.next.Peak());
    const auto decayed = model.Predict(activity, warm, 3600000000ULL);
    assert(decayed.next.Peak() < idle.next.Peak());
    auto invalid = parameters;
    invalid.local_limit_q16 = 0;
    assert(!model.SetParameters(invalid) && model.parameters().revision == 7);
    model.Clean(0);
    assert(model.state().Peak() == 0 && model.parameters().revision == 7);
    activity.valid = false;
    assert(model.Predict(activity, warm, 0).reason == RefreshReason::Recovery);
    activity = Page(1500);
    assert(model.Predict(activity, warm, 0).reason == RefreshReason::HighContrast);
}

void TestEnvironmentAndNumericalBounds() {
    PhysicsModel model;
    assert(model.EnvironmentGain({2500, 3900, 0, 0}) == 256);
    assert(model.EnvironmentGain({0, 3900, 0, 0}) == 512);
    assert(model.EnvironmentGain({-1000, 3900, 0, 0}) == 640);
    assert(model.EnvironmentGain({1750, 3900, 0, 0}) == 320);
    assert(model.EnvironmentGain({2500, 3300, 0, 0}) == 320);
    assert(model.EnvironmentGain({2500, 0, 0, 0}) == 256);
    assert(model.EnvironmentGain({2500, 3300, 0, 60001}) == 256);
    assert(model.EnvironmentGain({2500, 3900, 60001, 0}) == 384);
    assert(model.EnvironmentGain({9000, 3900, 0, 0}) == 384);
    const auto activity = Page(300);
    auto parameters = model.parameters();
    parameters.flip_weight_q8 = UINT16_MAX;
    parameters.temperature_gain_q8.fill(UINT16_MAX);
    parameters.low_battery_gain_q8 = UINT16_MAX;
    assert(model.SetParameters(parameters));
    for (unsigned step = 0; step < 100; ++step) {
        const auto prediction = model.Predict(activity, {2500, 1, 0, 0}, UINT64_MAX - step);
        assert(prediction.next.Peak() <= 64 * kDebtOne);
        for (const auto memory : prediction.next.memory_q16)
            assert(memory >= -static_cast<int32_t>(64 * kDebtOne) && memory <= 64 * static_cast<int32_t>(kDebtOne));
        model.Commit(prediction, step);
    }
    assert(model.state().Peak() == 64 * kDebtOne);
    uint32_t energy = 1;
    assert(!model.EstimateEnergy(RefreshKind::kPartial1Bpp, 100000, 1000, true, 100, 200, &energy) && energy == 0);
    parameters.energy[2] = {10, 20000, 5, 10, 20, true};
    assert(model.SetParameters(parameters));
    assert(model.EstimateEnergy(RefreshKind::kPartial1Bpp, 100000, 1000, true, 100, 200, &energy));
    assert(energy == 2020);
    assert(!model.EstimateEnergy(RefreshKind::kPartial1Bpp, 100000, 1000, false, 0, 0, &energy));
    assert(!model.EstimateEnergy(RefreshKind::kNone, 0, 0, true, 0, 0, &energy));
    parameters.energy[2] = {UINT32_MAX, UINT32_MAX, UINT16_MAX, UINT16_MAX, UINT16_MAX, true};
    assert(model.SetParameters(parameters));
    assert(model.EstimateEnergy(RefreshKind::kPartial1Bpp, UINT32_MAX, UINT32_MAX, true, UINT32_MAX, UINT32_MAX, &energy));
    assert(energy == UINT32_MAX);
}

void TestRing() {
    TelemetryRecorder ring;
    assert(ring.Read().count == 0);
    for (unsigned i = 1; i <= 20; ++i) {
        FrameTelemetry frame;
        frame.started_us = i * 100;
        ring.Record(frame);
    }
    const auto first = ring.Read();
    assert(first.count == 4 && first.lost == 4 && first.next == 8 && first.latest == 20);
    assert(first.frames[0].sequence == 5 && first.frames[0].started_us == 500);
    auto batch = ring.Read(first.next);
    assert(batch.count == 4 && batch.lost == 0 && batch.frames[0].sequence == 9);
    for (unsigned i = 0; i < 20; ++i) ring.Record({});
    assert(first.frames[0].sequence == 5 && first.frames[0].started_us == 500);
    batch = ring.Read(12);
    assert(batch.lost == 12 && batch.frames[0].sequence == 25);
    batch = ring.Read(UINT64_MAX);
    assert(batch.frames[0].sequence == 25 && batch.count == 4);
    batch = ring.Read(40);
    assert(batch.count == 0 && batch.next == 40);
}

unsigned Simulate(const char* name, FrameActivity activity, PhysicsEnvironment environment,
                  uint64_t interval_us, FILE* csv) {
    PhysicsModel model;
    unsigned full = 0;
    for (unsigned step = 1; step <= 4096; ++step) {
        for (auto& tile : activity.tiles) std::swap(tile.black_to_white, tile.white_to_black);
        const auto now = step * interval_us;
        const auto prediction = model.Predict(activity, environment, now);
        if (prediction.RequiresFull()) { ++full; model.Clean(now); }
        else model.Commit(prediction, now);
        assert(model.state().Mean() < model.parameters().global_limit_q16);
        assert(model.state().Peak() < model.parameters().local_limit_q16);
        if (csv) std::fprintf(csv, "%s,%u,%llu,%d,%u,%u,%u,%u,%u,%u,%u\n", name, step,
            static_cast<unsigned long long>(now), environment.temperature_centi_c, prediction.gain_q8,
            activity.ChangedPixels(), static_cast<unsigned>(prediction.reason), prediction.next.Mean(),
            prediction.next.Peak(), model.state().Mean(), model.state().Peak());
    }
    std::printf("SIMULATE: %s updates=4096 partial=%u full=%u\n", name, 4096 - full, full);
    assert(full > 0 && full < 4096);
    return full;
}

int main(int argc, char** argv) {
    assert(argc <= 2);
    TestLifecycleAndDirtyBounds();
    TestModelEquations();
    TestEnvironmentAndNumericalBounds();
    TestRing();
    FILE* csv = argc == 2 ? std::fopen(argv[1], "w") : nullptr;
    assert(argc != 2 || csv);
    if (csv) std::fputs("scenario,frame,monotonic_us,temperature_centi_c,gain_q8,flips,reason,projected_mean_q16,projected_peak_q16,committed_mean_q16,committed_peak_q16\n", csv);
    const auto started = std::chrono::steady_clock::now();
    const PhysicsEnvironment warm{2500, 3900, 0, 0}, cold{0, 3900, 0, 0};
    const auto sparse = Simulate("reading_sparse_25C", Page(300), warm, 20000000, csv);
    const auto dense = Simulate("reading_dense_25C", Page(1000), warm, 20000000, csv);
    const auto freezing = Simulate("reading_sparse_0C", Page(300), cold, 20000000, csv);
    assert(dense > sparse && freezing > sparse);
    auto hotspot = Page(0);
    hotspot.tiles[0].white_to_black = 6000;
    const auto clustered = Simulate("clustered_fast", hotspot, warm, 100000, csv);
    const auto spread = Simulate("distributed_fast", Page(300), warm, 100000, csv);
    assert(clustered > spread);
    hotspot.window = {0, 0, 40, 10};
    hotspot.tiles[0].white_to_black = 100;
    const auto small = Simulate("status_strip", hotspot, warm, 60000000, csv);
    assert(small < 4096 / 8);
    if (csv) std::fclose(csv);
    const auto duration = std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now() - started).count();
    std::printf("MEASURE: model scenarios=%u evaluations=%u elapsed_us=%lld state=%zu parameters=%zu recorder=%zu frame=%zu bytes (host).\n",
        6u, 6u * 4096, static_cast<long long>(duration), sizeof(PhysicsModel), sizeof(PhysicsParameters),
        sizeof(TelemetryRecorder), sizeof(FrameTelemetry));
}

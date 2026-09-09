#include "zectrix_display_service.h"
#include "zectrix_demo_ui.h"
#include "zectrix_book_transfer_controller.h"
#include "zectrix_sleep_cover.h"
#include "zectrix_reader_controller.h"
#include "zectrix_epd.h"

#include <algorithm>
#include <array>
#include <cassert>
#include <climits>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>
#include <memory>
#include <new>
#include <string>
#include <vector>

#include "esp_heap_caps.h"
#include "freertos/semphr.h"
#include "freertos/task.h"

struct FakeSpiDevice {};
struct FakeSemaphore { bool locked = false; };

namespace {

using namespace zectrix::display;
using Frame = std::array<uint8_t, DisplayService::kFrameBytes1Bpp>;
struct Packet { uint8_t command; std::vector<uint8_t> data; };
std::vector<Packet> packets;
std::array<int, 49> pins{};
std::array<int, 49> modes{}, pullups{};
std::map<void*, std::size_t> allocations;
bool bus_active = false;
unsigned devices = 0, mutexes = 0, gpio_writes = 0;
unsigned nothrow_allocations = 0, fail_allocation_at = 0;
unsigned heap_allocations = 0, fail_heap_at = 0;
int fail_command = -1, fail_data = -1;
bool fail_power_off = false, timeout_refresh = false, fail_lock = false;
int64_t now_us = 0;

void Reset() {
    assert(!bus_active && devices == 0 && mutexes == 0 && allocations.empty());
    packets.clear();
    pins = {};
    modes = {};
    pullups = {};
    gpio_writes = nothrow_allocations = heap_allocations = 0;
    fail_allocation_at = fail_heap_at = 0;
    fail_command = fail_data = -1;
    fail_power_off = timeout_refresh = fail_lock = false;
    now_us = 0;
}

void ClearTraffic() { packets.clear(); gpio_writes = 0; }

const Packet& PacketFor(uint8_t command) {
    const Packet* result = nullptr;
    for (const auto& packet : packets) {
        if (packet.command == command) {
            assert(result == nullptr);
            result = &packet;
        }
    }
    assert(result != nullptr);
    return *result;
}

bool HasCommand(uint8_t command) {
    return std::any_of(packets.begin(), packets.end(),
        [command](const auto& packet) { return packet.command == command; });
}

bool Bit(const uint8_t* bytes, int stride, int x, int y) {
    return (bytes[y * stride + x / 8] & (0x80u >> (x % 8))) != 0;
}

void PutBit(uint8_t* bytes, int stride, int x, int y, bool white) {
    auto& byte = bytes[y * stride + x / 8];
    const auto mask = static_cast<uint8_t>(0x80u >> (x % 8));
    byte = white ? byte | mask : byte & static_cast<uint8_t>(~mask);
}

void ToggleFirstPixels(Frame& frame, uint32_t count) {
    assert(count <= 400 * 300);
    for (uint32_t pixel = 0; pixel < count; ++pixel) {
        const int x = pixel % 400, y = pixel / 400;
        PutBit(frame.data(), 50, x, y, !Bit(frame.data(), 50, x, y));
    }
}

std::vector<uint8_t> Crop(const Frame& frame, const zectrix_epd_rect_t& rect) {
    const int stride = (rect.width + 7) / 8;
    std::vector<uint8_t> pixels(stride * rect.height, 0xa5);
    for (int y = 0; y < rect.height; ++y) {
        for (int x = 0; x < rect.width; ++x) {
            PutBit(pixels.data(), stride, x, y, Bit(frame.data(), 50, rect.x + x, rect.y + y));
        }
    }
    return pixels;
}

zectrix_epd_rect_t ReferenceDirty(const Frame& before, const zectrix_epd_rect_t& source,
                                 const uint8_t* pixels) {
    int left = 400, top = 300, right = -1, bottom = -1;
    // This pixel-by-pixel reference is independent of the driver's byte scan.
    for (int y = 0; y < source.height; ++y) {
        for (int x = 0; x < source.width; ++x) {
            if (Bit(before.data(), 50, source.x + x, source.y + y) !=
                Bit(pixels, (source.width + 7) / 8, x, y)) {
                left = std::min(left, source.x + x);
                right = std::max(right, source.x + x);
                top = std::min(top, source.y + y);
                bottom = std::max(bottom, source.y + y);
            }
        }
    }
    return right < left ? zectrix_epd_rect_t{} :
        zectrix_epd_rect_t{left, top, right - left + 1, bottom - top + 1};
}

template <typename Left, typename Right>
void SameRect(const Left& left, const Right& right) {
    assert(left.x == right.x && left.y == right.y &&
           left.width == right.width && left.height == right.height);
}

std::size_t CheckPartial(const Frame& before, const zectrix_epd_rect_t& source,
                         const uint8_t* pixels) {
    const auto dirty = ReferenceDirty(before, source, pixels);
    assert(dirty.width > 0);
    const int x0 = dirty.x & ~7;
    const int x1 = ((dirty.x + dirty.width + 7) & ~7) - 1;
    const int y1 = dirty.y + dirty.height - 1;
    const std::vector<uint8_t> window{
        static_cast<uint8_t>(x0 >> 8), static_cast<uint8_t>(x0),
        static_cast<uint8_t>(x1 >> 8), static_cast<uint8_t>(x1),
        static_cast<uint8_t>(dirty.y >> 8), static_cast<uint8_t>(dirty.y),
        static_cast<uint8_t>(y1 >> 8), static_cast<uint8_t>(y1), 1};
    assert(PacketFor(0x83).data == window);
    const auto& data = PacketFor(0x10).data;
    assert(data.size() == static_cast<std::size_t>((x1 - x0 + 1) * dirty.height / 4));
    std::size_t index = 0;
    for (int y = dirty.y; y <= y1; ++y) {
        for (int x = x0; x <= x1; ++x, ++index) {
            const bool old_pixel = Bit(before.data(), 50, x, y);
            const bool new_pixel = x < source.x || x >= source.x + source.width
                ? old_pixel : Bit(pixels, (source.width + 7) / 8, x - source.x, y - source.y);
            const unsigned transition = (data[index / 4] >> (6 - (index % 4) * 2)) & 3;
            assert(transition == (static_cast<unsigned>(old_pixel) * 2 + new_pixel));
        }
    }
    assert(HasCommand(0x12));
    return data.size();
}

void CheckFull(const Frame& frame) {
    assert(!HasCommand(0x83) && HasCommand(0x12));
    const auto& data = PacketFor(0x10).data;
    assert(data.size() == 30000);
    for (int pixel = 0; pixel < 400 * 300; ++pixel) {
        const unsigned code = (data[pixel / 4] >> (6 - (pixel % 4) * 2)) & 3;
        assert(code == Bit(frame.data(), 50, pixel % 400, pixel / 400));
    }
}

std::unique_ptr<DisplayService> CreateService() {
    DisplayService* service = nullptr;
    assert(DisplayService::Create(&service) == ESP_OK);
    return std::unique_ptr<DisplayService>(service);
}

DisplayInspection Inspect(const DisplayService& service) {
    DisplayInspection result;
    assert(service.ReadInspection(&result) == ESP_OK);
    return result;
}

void Present(DisplayService& service, const Frame& frame, DisplayIntent intent = DisplayIntent::Auto) {
    assert(service.Present1Bpp(intent, frame.data(), frame.size()) == ESP_OK);
}

void TestCreationAndInputErrors() {
    for (unsigned failure : {1u, 2u}) {
        Reset();
        fail_allocation_at = failure;
        DisplayService* service = nullptr;
        assert(DisplayService::Create(&service) == ESP_ERR_NO_MEM && service == nullptr);
    }
    for (unsigned failure : {1u, 2u}) {
        Reset();
        fail_heap_at = failure;
        DisplayService* service = nullptr;
        assert(DisplayService::Create(&service) == ESP_ERR_NO_MEM && service == nullptr);
    }
    Reset();
    assert(DisplayService::Create(nullptr) == ESP_ERR_INVALID_ARG);
    auto service = CreateService();
    Frame frame{};
    assert(!Inspect(*service).framebuffer_valid);
    assert(service->ReadInspection(nullptr) == ESP_ERR_INVALID_ARG);
    ClearTraffic();
    assert(service->Present1Bpp(DisplayIntent::Auto, nullptr, frame.size()) == ESP_ERR_INVALID_ARG);
    assert(service->Present1Bpp(DisplayIntent::Auto, frame.data(), 1) == ESP_ERR_INVALID_ARG);
    assert(service->Present1Bpp(static_cast<DisplayIntent>(255), frame.data(), frame.size()) == ESP_ERR_INVALID_ARG);
    for (const Rect invalid : {Rect{-1, 0, 1, 1}, Rect{0, -1, 1, 1}, Rect{400, 0, 1, 1},
                              Rect{0, 300, 1, 1}, Rect{1, 0, INT_MAX, 1}, Rect{0, 1, 1, INT_MAX},
                              Rect{0, 0, 0, 1}, Rect{0, 0, 1, -1}}) {
        assert(service->Present1Bpp(DisplayIntent::Auto, frame.data(), frame.size(),
                                    invalid, frame.data(), 1) == ESP_ERR_INVALID_ARG);
    }
    assert(service->Present1Bpp(DisplayIntent::Auto, frame.data(), frame.size(),
                                {0, 0, 9, 1}, frame.data(), 1) == ESP_ERR_INVALID_SIZE);
    assert(service->Present1Bpp(DisplayIntent::Fast, frame.data(), frame.size(),
                                {0, 0, 1, 1}, nullptr, 1) == ESP_ERR_INVALID_ARG);
    assert(packets.empty() && gpio_writes == 0 && Inspect(*service).refresh_count == 0);
}

void TestAutomaticRefreshAndBudget() {
    Reset();
    auto service = CreateService();
    Frame frame;
    frame.fill(0xff);
    ClearTraffic();
    Present(*service, frame);
    CheckFull(frame);
    auto inspection = Inspect(*service);
    assert(inspection.framebuffer_valid && inspection.bits_per_pixel == 1 && !inspection.powered);
    assert(inspection.refresh_count == 1 && inspection.last_duration_us > 0);
    ClearTraffic();
    Present(*service, frame, DisplayIntent::Fast);
    assert(packets.empty() && gpio_writes == 0 && Inspect(*service).refresh_count == 1);
    const auto before = frame;
    PutBit(frame.data(), 50, 19, 31, false);
    PutBit(frame.data(), 50, 28, 38, false);
    Present(*service, frame);
    assert(CheckPartial(before, {0, 0, 400, 300}, frame.data()) == 32);
    SameRect(service->state().dirty_region, (Rect{19, 31, 10, 8}));
    assert(service->state().partial_refresh_count == 1);
    assert(service->state().partial_changed_pixels == 2);
    assert(Inspect(*service).last_refresh == RefreshKind::kPartial1Bpp);
    for (unsigned count = 1; count < 8; ++count) {
        PutBit(frame.data(), 50, 19, 31, count % 2 != 0);
        Present(*service, frame);
    }
    assert(service->state().partial_refresh_count == 8);
    assert(service->state().partial_changed_pixels == 9);
    ClearTraffic();
    const auto attempts = Inspect(*service).refresh_count;
    Present(*service, frame);
    assert(packets.empty() && gpio_writes == 0 && Inspect(*service).refresh_count == attempts);
    PutBit(frame.data(), 50, 0, 0, false);
    Present(*service, frame);
    CheckFull(frame);
    assert(service->state().partial_refresh_count == 0 && !service->state().has_dirty_region);
    assert(service->state().partial_changed_pixels == 0);
    assert(Inspect(*service).preview[0] == 0x7f);
    frame[0] = 0xff;
    assert(Inspect(*service).preview[0] == 0x7f);
    for (auto intent : {DisplayIntent::Quality, DisplayIntent::FullClean}) {
        ClearTraffic();
        Present(*service, frame, intent);
        CheckFull(frame);
    }
    frame.fill(0);
    ClearTraffic();
    Present(*service, frame);
    CheckFull(frame);
}

void TestHighContrastAndSparseChanges() {
    for (auto intent : {DisplayIntent::Auto, DisplayIntent::Fast}) {
        Reset();
        auto service = CreateService();
        Frame white;
        white.fill(0xff);
        Present(*service, white);
        Frame frame = white;
        ToggleFirstPixels(frame, 29999);
        ClearTraffic();
        Present(*service, frame, intent);
        CheckPartial(white, {0, 0, 400, 300}, frame.data());
        assert(service->state().partial_refresh_count == 1);
        assert(service->state().partial_changed_pixels == 29999);

        Present(*service, white, DisplayIntent::FullClean);
        frame = white;
        ToggleFirstPixels(frame, 30000);
        ClearTraffic();
        Present(*service, frame, intent);
        CheckFull(frame);
        assert(service->state().partial_refresh_count == 0 && service->state().partial_changed_pixels == 0);
        const auto attempts = Inspect(*service).refresh_count;
        ClearTraffic();
        Present(*service, frame, intent);
        assert(packets.empty() && gpio_writes == 0 && Inspect(*service).refresh_count == attempts);
        // The inverse transition must trigger cleanup too.
        Present(*service, white, intent);
        CheckFull(white);

        frame = white;
        PutBit(frame.data(), 50, 0, 0, false);
        PutBit(frame.data(), 50, 399, 299, false);
        ClearTraffic();
        Present(*service, frame, intent);
        // A panel-sized bounding box is not a panel-sized contrast change.
        assert(CheckPartial(white, {0, 0, 400, 300}, frame.data()) == 30000);
        assert(service->state().partial_changed_pixels == 2);
    }
}

void TestAccumulatedChanges() {
    Reset();
    auto service = CreateService();
    Frame frame;
    frame.fill(0xff);
    Present(*service, frame);
    assert(service->BeginBatch() == ESP_OK);
    for (unsigned cycle = 0; cycle < 2; ++cycle) {
        for (unsigned step = 1; step <= 5; ++step) {
            const auto before = frame;
            ToggleFirstPixels(frame, 12000);
            ClearTraffic();
            Present(*service, frame);
            if (step < 5) {
                CheckPartial(before, {0, 0, 400, 300}, frame.data());
                assert(service->state().partial_refresh_count == step);
                assert(service->state().partial_changed_pixels == step * 12000);
            } else {
                CheckFull(frame);
                assert(service->state().partial_refresh_count == 0);
                assert(service->state().partial_changed_pixels == 0 && !service->state().has_dirty_region);
            }
            assert(service->IsPowered() && Inspect(*service).batch_active);
            const auto attempts = Inspect(*service).refresh_count;
            ClearTraffic();
            Present(*service, frame);
            assert(packets.empty() && gpio_writes == 0 && Inspect(*service).refresh_count == attempts);
        }
    }
    assert(service->EndBatch() == ESP_OK && !service->IsPowered());
}

void TestPatchCompatibility() {
    Reset();
    auto service = CreateService();
    Frame fallback;
    fallback.fill(0xff);
    const uint8_t black = 0;
    // The legacy patch API still uses the supplied full image for recovery.
    assert(service->Present1Bpp(DisplayIntent::Fast, fallback.data(), fallback.size(),
                                {3, 2, 1, 1}, &black, 1) == ESP_OK);
    CheckFull(fallback);
    ClearTraffic();
    assert(service->Present1Bpp(DisplayIntent::Fast, fallback.data(), fallback.size(),
                                {3, 2, 1, 1}, &black, 1) == ESP_OK);
    CheckPartial(fallback, {3, 2, 1, 1}, &black);
    auto expected = fallback;
    PutBit(expected.data(), 50, 3, 2, false);
    ClearTraffic();
    Present(*service, expected);
    assert(packets.empty() && gpio_writes == 0);
    Present(*service, fallback, DisplayIntent::FullClean);
    ClearTraffic();
    std::array<uint8_t, 50 * 75> large_patch{};
    assert(service->Present1Bpp(DisplayIntent::Fast, fallback.data(), fallback.size(),
                                {0, 0, 400, 75}, large_patch.data(), large_patch.size()) == ESP_OK);
    CheckFull(fallback);
    assert(service->state().partial_refresh_count == 0 && service->state().partial_changed_pixels == 0);
}

void TestDriverDiffAndWindow() {
    Reset();
    zectrix_epd_config_t config;
    zectrix_epd_get_default_config(&config);
    zectrix_epd_handle_t handle = nullptr;
    assert(zectrix_epd_new(&config, &handle) == ESP_OK);
    Frame before;
    for (std::size_t i = 0; i < before.size(); ++i) before[i] = static_cast<uint8_t>(i * 79 + 23);
    const zectrix_epd_rect_t full{0, 0, 400, 300};
    zectrix_epd_rect_t dirty{1, 2, 3, 4};
    assert(zectrix_epd_find_dirty_1bpp(handle, &full, before.data(), before.size(), &dirty) == ESP_ERR_INVALID_STATE);
    SameRect(dirty, zectrix_epd_rect_t{});
    zectrix_epd_diff_t difference{{1, 2, 3, 4}, 17};
    assert(zectrix_epd_analyze_1bpp(handle, &full, before.data(), before.size(), &difference) == ESP_ERR_INVALID_STATE);
    SameRect(difference.dirty, zectrix_epd_rect_t{});
    assert(difference.changed_pixels == 0);
    assert(zectrix_epd_power_on(handle) == ESP_OK);
    assert(zectrix_epd_refresh_full_1bpp(handle, before.data(), before.size()) == ESP_OK);
    assert(zectrix_epd_power_off(handle) == ESP_OK);
    ClearTraffic();
    const auto allocation_count = heap_allocations;
    for (int x = 0; x < 400; ++x) {
        for (int width = 1; width <= std::min(17, 400 - x); ++width) {
            for (int y : {0, 157, 299}) {
                const zectrix_epd_rect_t source{x, y, width, std::min(3, 300 - y)};
                auto pixels = Crop(before, source);
                assert(zectrix_epd_find_dirty_1bpp(handle, &source, pixels.data(), pixels.size(), &dirty) == ESP_OK);
                SameRect(dirty, zectrix_epd_rect_t{});
                assert(zectrix_epd_analyze_1bpp(handle, &source, pixels.data(), pixels.size(), &difference) == ESP_OK);
                assert(difference.changed_pixels == 0);
                const int dx = width - 1, dy = source.height - 1;
                PutBit(pixels.data(), (width + 7) / 8, dx, dy,
                       !Bit(before.data(), 50, x + dx, y + dy));
                assert(zectrix_epd_find_dirty_1bpp(handle, &source, pixels.data(), pixels.size(), &dirty) == ESP_OK);
                SameRect(dirty, ReferenceDirty(before, source, pixels.data()));
                assert(zectrix_epd_analyze_1bpp(handle, &source, pixels.data(), pixels.size(), &difference) == ESP_OK);
                SameRect(difference.dirty, dirty);
                assert(difference.changed_pixels == 1);
                for (auto& byte : pixels) byte = static_cast<uint8_t>(~byte);
                assert(zectrix_epd_analyze_1bpp(handle, &source, pixels.data(), pixels.size(), &difference) == ESP_OK);
                SameRect(difference.dirty, ReferenceDirty(before, source, pixels.data()));
                assert(difference.changed_pixels == static_cast<uint32_t>(width * source.height - 1));
            }
        }
    }
    assert(packets.empty() && gpio_writes == 0 && heap_allocations == allocation_count);
    const zectrix_epd_rect_t source{5, 10, 17, 4};
    auto pixels = Crop(before, source);
    PutBit(pixels.data(), 3, 2, 1, !Bit(before.data(), 50, 7, 11));
    PutBit(pixels.data(), 3, 14, 3, !Bit(before.data(), 50, 19, 13));
    dirty = source;
    assert(zectrix_epd_find_dirty_1bpp(handle, &dirty, pixels.data(), pixels.size(), &dirty) == ESP_OK);
    SameRect(dirty, (zectrix_epd_rect_t{7, 11, 13, 3}));
    difference.dirty = source;
    assert(zectrix_epd_analyze_1bpp(handle, &difference.dirty, pixels.data(), pixels.size(), &difference) == ESP_OK);
    SameRect(difference.dirty, dirty);
    assert(difference.changed_pixels == 2);
    assert(zectrix_epd_refresh_partial_1bpp(handle, &source, pixels.data(), pixels.size()) == ESP_ERR_INVALID_STATE);
    assert(zectrix_epd_power_on(handle) == ESP_OK);
    ClearTraffic();
    assert(zectrix_epd_refresh_partial_1bpp(handle, &source, pixels.data(), pixels.size()) == ESP_OK);
    assert(CheckPartial(before, source, pixels.data()) == 18);
    Frame expected = before, shadow;
    for (int y = 0; y < source.height; ++y) {
        for (int x = 0; x < source.width; ++x) {
            PutBit(expected.data(), 50, source.x + x, source.y + y, Bit(pixels.data(), 3, x, y));
        }
    }
    assert(zectrix_epd_copy_shadow(handle, 0, shadow.data(), shadow.size()) == ESP_OK && shadow == expected);
    ClearTraffic();
    assert(zectrix_epd_refresh_partial_1bpp(handle, &source, pixels.data(), pixels.size()) == ESP_OK);
    assert(packets.empty() && gpio_writes == 0);
    before = expected;
    PutBit(expected.data(), 50, 399, 299, !Bit(before.data(), 50, 399, 299));
    assert(zectrix_epd_refresh_partial_1bpp(handle, &full, expected.data(), expected.size()) == ESP_OK);
    assert(CheckPartial(before, full, expected.data()) == 2);
    assert(zectrix_epd_copy_shadow(handle, 0, shadow.data(), shadow.size()) == ESP_OK && shadow == expected);
    ClearTraffic();
    Frame inverted = expected;
    for (auto& byte : inverted) byte = static_cast<uint8_t>(~byte);
    assert(zectrix_epd_analyze_1bpp(handle, &full, inverted.data(), inverted.size(), &difference) == ESP_OK);
    SameRect(difference.dirty, full);
    assert(difference.changed_pixels == 120000);
    const zectrix_epd_rect_t invalid{1, 1, INT_MAX, INT_MAX};
    assert(zectrix_epd_refresh_partial_1bpp(handle, &invalid, pixels.data(), pixels.size()) == ESP_ERR_INVALID_ARG);
    assert(zectrix_epd_find_dirty_1bpp(handle, &full, expected.data(), SIZE_MAX, &dirty) == ESP_ERR_INVALID_SIZE);
    SameRect(dirty, zectrix_epd_rect_t{});
    assert(zectrix_epd_find_dirty_1bpp(handle, nullptr, expected.data(), expected.size(), &dirty) == ESP_ERR_INVALID_ARG);
    assert(zectrix_epd_find_dirty_1bpp(handle, &full, expected.data(), expected.size(), nullptr) == ESP_ERR_INVALID_ARG);
    assert(zectrix_epd_analyze_1bpp(handle, &full, expected.data(), SIZE_MAX, &difference) == ESP_ERR_INVALID_SIZE);
    SameRect(difference.dirty, zectrix_epd_rect_t{});
    assert(difference.changed_pixels == 0);
    assert(zectrix_epd_analyze_1bpp(handle, nullptr, expected.data(), expected.size(), &difference) == ESP_ERR_INVALID_ARG);
    assert(zectrix_epd_analyze_1bpp(handle, &full, expected.data(), expected.size(), nullptr) == ESP_ERR_INVALID_ARG);
    assert(packets.empty() && gpio_writes == 0);
    assert(zectrix_epd_del(handle) == ESP_OK);
}

void TestFailuresRecoverWithFullFrame() {
    for (bool full : {false, true}) {
        for (int failure = 0; failure < 5; ++failure) {
            Reset();
            auto service = CreateService();
            Frame frame;
            frame.fill(0xff);
            Present(*service, frame);
            ToggleFirstPixels(frame, 12000);
            Present(*service, frame);
            assert(service->state().partial_changed_pixels == 12000);
            if (full) ToggleFirstPixels(frame, 30000);
            else PutBit(frame.data(), 50, 399, 299, false);
            ClearTraffic();
            if (failure == 0) fail_command = 0xe9;
            if (failure == 1) fail_data = 0x10;
            if (failure == 2) timeout_refresh = true;
            if (failure == 3) fail_power_off = true;
            if (failure == 4) fail_lock = true;
            const auto expected = failure == 2 ? ESP_ERR_TIMEOUT : ESP_FAIL;
            assert(service->Present1Bpp(DisplayIntent::Auto, frame.data(), frame.size()) == expected);
            if (failure == 2) assert(!HasCommand(0x02));
            assert(!service->CanUsePartial());
            assert(service->state().partial_refresh_count == 0 && service->state().partial_changed_pixels == 0);
            const auto status = Inspect(*service);
            assert(!status.framebuffer_valid && status.failed_refresh_count == 1 && status.last_error == expected);
            timeout_refresh = false;
            ClearTraffic();
            // A failed power-off may leave a matching shadow. It still needs recovery.
            Present(*service, frame);
            CheckFull(frame);
            assert(service->CanUsePartial() && Inspect(*service).framebuffer_valid);
            assert(service->state().partial_changed_pixels == 0);
        }
    }
}

void TestBatchAndGray() {
    Reset();
    auto service = CreateService();
    Frame frame;
    frame.fill(0xff);
    Present(*service, frame);
    assert(service->BeginBatch() == ESP_OK);
    assert(service->BeginBatch() == ESP_ERR_INVALID_STATE);
    ClearTraffic();
    Present(*service, frame);
    assert(service->IsPowered() && packets.empty() && gpio_writes == 0);
    frame[0] = 0;
    Present(*service, frame);
    assert(service->IsPowered() && Inspect(*service).batch_active);
    assert(service->EndBatch() == ESP_OK && !service->IsPowered());
    assert(service->EndBatch() == ESP_ERR_INVALID_STATE);
    std::array<uint8_t, DisplayService::kFrameBytes4Bpp> gray{};
    gray[0] = 0x73;
    ClearTraffic();
    assert(service->Present4Bpp(DisplayIntent::Fast, gray.data(), gray.size()) == ESP_ERR_NOT_SUPPORTED);
    assert(service->Present4Bpp(DisplayIntent::Quality, gray.data(), 1) == ESP_ERR_INVALID_ARG);
    assert(packets.empty() && gpio_writes == 0);
    assert(service->Present4Bpp(DisplayIntent::Quality, gray.data(), gray.size()) == ESP_OK);
    const auto inspection = Inspect(*service);
    assert(!service->CanUsePartial() && inspection.framebuffer_valid && inspection.bits_per_pixel == 4);
    assert(inspection.state.partial_refresh_count == 0 && inspection.state.partial_changed_pixels == 0);
    assert(inspection.preview[0] == 0x73 && inspection.framebuffer_bytes == gray.size());
    ClearTraffic();
    Present(*service, frame);
    CheckFull(frame);
}

void TestShutdownReleasesSpi() {
    for (unsigned failure = 0; failure < 5; ++failure) {
        Reset();
        auto service = CreateService();
        ZectrixDemoUi ui(service.get());
        if (failure == 1) assert(service->BeginBatch() == ESP_OK);
        if (failure == 2) fail_data = 0x10;
        if (failure == 3) timeout_refresh = true;
        if (failure == 4) fail_power_off = true;
        const auto cleared = ui.ClearDisplay();
        assert((cleared == ESP_OK) == (failure < 2));
        // Shutdown must release DMA, the SPI device and bus even if clearing
        // failed or the application left an explicit power batch open.
        service.reset();
        assert(!bus_active && devices == 0 && mutexes == 0 && allocations.empty());
        assert(pins[GPIO_NUM_6] == 0);
        for (int pin : {GPIO_NUM_8, GPIO_NUM_9, GPIO_NUM_10, GPIO_NUM_11, GPIO_NUM_12, GPIO_NUM_13}) {
            assert(modes[pin] == GPIO_MODE_DISABLE && pullups[pin] == GPIO_PULLUP_DISABLE);
        }
    }
    Reset();
    zectrix_epd_config_t config;
    zectrix_epd_get_default_config(&config);
    config.initialize_spi_bus = false;
    bus_active = true;
    modes[config.pin_mosi] = modes[config.pin_sclk] = GPIO_MODE_OUTPUT;
    zectrix_epd_handle_t handle = nullptr;
    assert(zectrix_epd_new(&config, &handle) == ESP_OK);
    assert(zectrix_epd_del(handle) == ESP_OK);
    assert(bus_active && devices == 0);
    assert(modes[config.pin_mosi] == GPIO_MODE_OUTPUT && modes[config.pin_sclk] == GPIO_MODE_OUTPUT);
    assert(spi_bus_free(config.spi_host) == ESP_OK);
    Reset();
}

void TestUiTraffic() {
    Reset();
    auto service = CreateService();
    ZectrixDemoUi ui(service.get());
    zectrix::time::DateTime clock;
    clock.year = 2026;
    clock.month = 9;
    clock.day = 8;
    clock.hour = 12;
    clock.minute = 34;
    clock.second = 56;
    assert(ui.ShowClock(clock, true) == ESP_OK);
    Frame before;
    std::memcpy(before.data(), ui.canvas().data(), before.size());
    ClearTraffic();
    ++clock.second;
    assert(ui.ShowClock(clock, false) == ESP_OK);
    assert(packets.empty() && gpio_writes == 0);
    ++clock.minute;
    assert(ui.ShowClock(clock, false) == ESP_OK);
    const auto clock_bytes = CheckPartial(before, {0, 0, 400, 300}, ui.canvas().data());
    assert(clock_bytes < 23400);
    const auto count = Inspect(*service).refresh_count;
    ClearTraffic();
    assert(ui.ShowClock(clock, false) == ESP_OK);
    assert(packets.empty() && gpio_writes == 0 && Inspect(*service).refresh_count == count);
    const char* items[] = {"CLOCK", "SETTINGS", "ABOUT"};
    assert(ui.ShowMenu("MENU", items, 3, 0, "OK Select", true) == ESP_OK);
    std::memcpy(before.data(), ui.canvas().data(), before.size());
    ClearTraffic();
    assert(ui.ShowMenu("MENU", items, 3, 1, "OK Select", false) == ESP_OK);
    const auto menu_bytes = CheckPartial(before, {0, 0, 400, 300}, ui.canvas().data());
    assert(menu_bytes < 23400);
    std::memcpy(before.data(), ui.canvas().data(), before.size());
    ClearTraffic();
    assert(ui.ShowMenu("OTHER", items, 3, 1, "UP Back", false) == ESP_OK);
    const auto dirty = ReferenceDirty(before, {0, 0, 400, 300}, ui.canvas().data());
    assert(dirty.y < 44 && dirty.y + dirty.height > 270);
    CheckPartial(before, {0, 0, 400, 300}, ui.canvas().data());
    std::printf("MEASURE: native RAM payload clock=%zu menu=%zu bytes; previous fixed window=23400 bytes.\n",
                clock_bytes, menu_bytes);
}

void SavePreview(const ZectrixCanvas& canvas, const char* name) {
    const char* directory = std::getenv("ZECTRIX_UI_PREVIEW_DIR");
    if (!directory) return;
    char path[1024];
    std::snprintf(path, sizeof(path), "%s/%s.pbm", directory, name);
    FILE* output = std::fopen(path, "wb");
    assert(output);
    std::fprintf(output, "P4\n400 300\n");
    for (size_t i = 0; i < canvas.size(); ++i) std::fputc(canvas.data()[i] ^ 0xff, output);
    assert(std::fclose(output) == 0);
}

void TestViewPorts() {
    using zectrix::ui::ViewPortScheduler;
    ViewPortScheduler ports;
    ZectrixCanvas canvas;
    canvas.Clear();
    int content_draws = 0, status_draws = 0;
    const auto draw = [](void* context, ZectrixCanvas& target) {
        ++*static_cast<int*>(context);
        target.Clear(false);
        target.Text(0, 0, "OUTSIDE CLIP", 3);
    };
    assert(!ports.Configure(4, {{0, 0, 400, 24}, draw, &status_draws}));
    assert(!ports.Configure(0, {{399, 0, INT_MAX, 24}, draw, &status_draws}));
    assert(!ports.Invalidate(0));
    assert(ports.Configure(0, {{0, 24, 400, 276}, draw, &content_draws}));
    assert(ports.Configure(1, {{0, 0, 400, 24}, draw, &status_draws}));
    assert(ports.Enable(1, false));
    const auto saved_clip = ZectrixCanvas::Clip{10, 30, 80, 60};
    canvas.SetClip(saved_clip);
    auto update = ports.Compose(canvas);
    assert(update.pending && update.dirty.y == 24 && update.dirty.height == 276);
    assert(canvas.clip().x == saved_clip.x && canvas.clip().width == saved_clip.width);
    assert(content_draws == 1 && status_draws == 0);
    assert(Bit(canvas.data(), 50, 0, 0) && !Bit(canvas.data(), 50, 0, 24));
    ports.Complete(true);
    assert(!ports.Compose(canvas).pending);
    assert(ports.Enable(1, true));
    assert(ports.Invalidate(0, true));
    update = ports.Compose(canvas);
    assert(update.quality && update.dirty.y == 0 && update.dirty.height == 300);
    ports.Complete(false);
    assert(ports.Compose(canvas).quality);
    ports.Complete(true);
    assert(ports.Invalidate(1));
    const int before = content_draws;
    update = ports.Compose(canvas);
    assert(update.pending && !update.quality && update.dirty.height == 24);
    assert(content_draws == before);
    ports.Complete(true);
    assert(!ports.Compose(canvas).pending);
    canvas.SetClip({INT_MAX, INT_MAX, INT_MAX, INT_MAX});
    assert(canvas.clip().width == 0 && canvas.clip().height == 0);
    canvas.Clear(false);
}

void TestStatusAndImageComposition() {
    Reset();
    auto service = CreateService();
    ZectrixDemoUi ui(service.get());
    zectrix::ui::StatusBarState state;
    state.time_valid = state.battery_valid = state.charging = true;
    state.hour = 12;
    state.minute = 34;
    state.battery_percent = 65;
    state.ble = zectrix::ui::RadioIndicator::Connected;
    ui.UpdateStatus(state);
    const char* items[] = {"BOOK READER", "SEND BOOKS", "CLOCK", "SLEEP COVER", "SETTINGS", "CONNECTIVITY", "AUTO SHOWCASE",
        "DISPLAY GALLERY", "HARDWARE TESTS", "DEVICE INFO", "ABOUT & LICENSE"};
    assert(ui.ShowMenu("ZECTRIX | LAUNCHER", items, std::size(items), 0,
        "UP/DOWN Move  OK Select  Hold DOWN Off", true) == ESP_OK);
    assert(Inspect(*service).refresh_count == 1);
    SavePreview(ui.canvas(), "launcher");
    Frame before;
    std::memcpy(before.data(), ui.canvas().data(), before.size());
    ClearTraffic();
    ui.UpdateStatus(state);
    assert(ui.RefreshPending() == ESP_OK && packets.empty());
    ++state.minute;
    ui.UpdateStatus(state);
    assert(ui.RefreshPending() == ESP_OK);
    constexpr size_t content_offset = 24 * 50;
    assert(std::memcmp(before.data() + content_offset, ui.canvas().data() + content_offset,
                       before.size() - content_offset) == 0);
    const auto dirty = ReferenceDirty(before, {0, 0, 400, 300}, ui.canvas().data());
    assert(dirty.y + dirty.height <= 24);
    const auto bytes = CheckPartial(before, {0, 0, 400, 300}, ui.canvas().data());
    assert(bytes <= 2400);
    std::printf("MEASURE: status-only minute update RAM payload=%zu bytes.\n", bytes);

    std::memcpy(before.data(), ui.canvas().data(), before.size());
    ClearTraffic();
    assert(ui.ShowMenu("ZECTRIX | LAUNCHER", items, std::size(items), 10,
        "UP/DOWN Move  OK Select  Hold DOWN Off", false) == ESP_OK);
    assert(std::memcmp(before.data(), ui.canvas().data(), content_offset) == 0);
    SavePreview(ui.canvas(), "launcher-last");
    const auto count = Inspect(*service).refresh_count;
    ++state.minute;
    ui.UpdateStatus(state);
    assert(ui.ShowMenu("ZECTRIX | LAUNCHER", items, std::size(items), 9,
        "UP/DOWN Move  OK Select  Hold DOWN Off", false) == ESP_OK);
    assert(ui.RefreshPending() == ESP_OK && Inspect(*service).refresh_count == count + 1);

    ++state.minute;
    ui.UpdateStatus(state);
    fail_command = 0xe9;
    assert(ui.RefreshPending() == ESP_FAIL);
    assert(!service->CanUsePartial());
    ClearTraffic();
    assert(ui.RefreshPending() == ESP_OK && service->CanUsePartial());
    Frame recovered;
    std::memcpy(recovered.data(), ui.canvas().data(), recovered.size());
    CheckFull(recovered);

    const char* minimal_items[] = {"CLOCK", "SLEEP COVER", "SETTINGS", "AUTO SHOWCASE",
        "DISPLAY GALLERY", "HARDWARE TESTS", "DEVICE INFO", "ABOUT & LICENSE"};
    state.ble = zectrix::ui::RadioIndicator::Off;
    ui.UpdateStatus(state);
    assert(ui.ShowMenu("ZECTRIX | LAUNCHER", minimal_items, std::size(minimal_items), 0,
        "UP/DOWN Move  OK Select  Hold DOWN Off", true) == ESP_OK);
    SavePreview(ui.canvas(), "launcher-minimal");

    state.time_valid = state.battery_valid = state.charging = false;
    state.charge_fault = true;
    state.wifi = zectrix::ui::RadioIndicator::Fault;
    ui.UpdateStatus(state);
    assert(ui.ShowClock({0, 0, 0, 0, 25, 42, 0}, true, "UPTIME (HH:MM)", false) == ESP_OK);
    SavePreview(ui.canvas(), "clock-fallback");
    auto equivalent = state;
    equivalent.minute = 59;
    equivalent.battery_percent = 100;
    assert(state == equivalent);

    Frame image;
    image.fill(0x55);
    assert(ui.ShowImage1Bpp(image.data(), image.size()) == ESP_OK);
    std::memcpy(before.data(), ui.canvas().data(), before.size());
    assert(std::memcmp(before.data() + content_offset, image.data() + content_offset,
                       image.size() - content_offset) == 0);
    const uint8_t patch[] = {0x00, 0x00, 0x00, 0x00};
    assert(ui.ShowImagePatch({0, 22, 8, 4}, patch, sizeof(patch)) == ESP_OK);
    assert(std::memcmp(before.data(), ui.canvas().data(), content_offset) == 0);
    assert(!Bit(ui.canvas().data(), 50, 0, 24));
    assert(ui.ShowImagePatch({0, 0, INT_MAX, 1}, patch, sizeof(patch)) == ESP_ERR_INVALID_ARG);
    assert(ui.ShowImagePatch({399, 299, 1, 1}, patch, 2) == ESP_ERR_INVALID_SIZE);

    std::array<uint8_t, DisplayService::kFrameBytes4Bpp> gray;
    for (size_t i = 0; i < gray.size(); ++i) gray[i] = static_cast<uint8_t>(i);
    const auto original = gray;
    ClearTraffic();
    assert(ui.ShowImage4Bpp(gray.data(), gray.size()) == ESP_OK);
    assert(gray == original && Inspect(*service).bits_per_pixel == 4);
    const auto first_packets = packets;
    ClearTraffic();
    assert(ui.RefreshPending() == ESP_OK && packets.empty());
    state.battery_valid = true;
    state.battery_percent = 50;
    ui.UpdateStatus(state);
    assert(ui.RefreshPending() == ESP_OK && Inspect(*service).bits_per_pixel == 4);
    assert(packets.size() == first_packets.size());
    unsigned writes = 0;
    for (size_t i = 0; i < packets.size(); ++i) {
        assert(packets[i].command == first_packets[i].command);
        if (packets[i].command == 0x10) {
            ++writes;
            const auto& previous = first_packets[i].data;
            const auto& current = packets[i].data;
            assert(previous.size() == 30000 && current.size() == previous.size());
            assert(std::equal(previous.begin() + 24 * 100, previous.end(), current.begin() + 24 * 100));
        }
    }
    assert(writes > 2);
    assert(ui.ShowImagePatch({0, 24, 8, 4}, patch, sizeof(patch)) == ESP_ERR_INVALID_STATE);
    assert(ui.ShowAbout() == ESP_OK && Inspect(*service).bits_per_pixel == 1);
    SavePreview(ui.canvas(), "about");
    std::array<ZectrixTestState, static_cast<size_t>(ZectrixTestId::kCount)> tests{};
    assert(ui.ShowTestMenu(2, tests, true) == ESP_OK);
    SavePreview(ui.canvas(), "diagnostics");
    ClearTraffic();
    assert(ui.ClearDisplay() == ESP_OK);
    Frame white;
    white.fill(0xff);
    CheckFull(white);
    assert(ui.ShowAbout() == ESP_OK);
    assert(!Bit(ui.canvas().data(), 50, 0, 23));
}

void TestReaderComposition() {
    using namespace zectrix::reader;
    using namespace zectrix::app;
    using zectrix::sdk::Button;
    using zectrix::sdk::InputAction;
    Reset();
    auto service = CreateService();
    ZectrixDemoUi ui(service.get());
    zectrix::ui::StatusBarState status;
    status.time_valid = status.battery_valid = true;
    status.hour = 20; status.minute = 26; status.battery_percent = 82;
    ui.UpdateStatus(status);
    class PreviewLibrary final : public Library {
    public:
        std::string text;
        std::unique_ptr<MemorySource> source;
        PreviewLibrary() {
            for (unsigned i = 0; i < 30; ++i)
                text += "第一段：风从海上来，带着远方的消息。\nEnglish words stay together on a quiet page.\n日本語と한국어。轻按按键，继续阅读。\n";
            source = std::make_unique<MemorySource>(reinterpret_cast<const uint8_t*>(text.data()), text.size());
        }
        Result Refresh() override { return Result::Ok; }
        std::size_t count() const override { return 1; }
        BookInfo Get(std::size_t) const override {
            BookInfo book;
            std::strcpy(book.id.data(), "风从海上来.txt"); book.bytes = text.size(); return book;
        }
        bool truncated() const override { return false; }
        Result Open(std::size_t, Source** output) override { *output = source.get(); return Result::Ok; }
        void Close() override {}
    } library;
    class PreviewStore final : public BookmarkStore {
    public:
        Result Load(uint8_t*, std::size_t, std::size_t*) override { return Result::End; }
        Result Save(const uint8_t*, std::size_t) override { return Result::Ok; }
        Result Publish(uint32_t, const uint8_t*, std::size_t) override { return Result::Ok; }
        Result Receive(uint32_t*, uint8_t*, std::size_t, std::size_t*) override { return Result::End; }
    } store;
    Bookmarks bookmarks(store);
    ReaderController reader(library, bookmarks);
    assert(zectrix::sdk::IsOk(reader.Start()));
    assert(ui.ShowReader(reader, true) == ESP_OK);
    SavePreview(ui.canvas(), "reader-library");
    assert(reader.Handle({Button::Ok, InputAction::Click}) == ReaderDecision::RenderQuality);
    assert(ui.ShowReader(reader, true) == ESP_OK);
    reader.Presented(true);
    SavePreview(ui.canvas(), "reader-small");
    const auto small = reader.engine().page().count;
    const auto glyph = reader.engine().page().glyphs[0];
    assert(glyph.codepoint == U'第');
    const auto* bitmap = GlyphBitmap(glyph.codepoint);
    for (int row = 0; row < 16; ++row) {
        const uint16_t bits = static_cast<uint16_t>(bitmap[row * 2 + 1]) << 8 | bitmap[row * 2 + 2];
        for (int col = 0; col < 16; ++col)
            assert(Bit(ui.canvas().data(), 50, 8 + glyph.x + col, 48 + glyph.y + row) == !(bits & (0x8000 >> col)));
    }
    Frame page;
    std::memcpy(page.data(), ui.canvas().data(), page.size());
    ++status.minute;
    ui.UpdateStatus(status);
    ClearTraffic();
    assert(ui.RefreshPending() == ESP_OK);
    assert(std::memcmp(page.data() + 1200, ui.canvas().data() + 1200, page.size() - 1200) == 0);
    assert(ReferenceDirty(page, {0, 0, 400, 300}, ui.canvas().data()).height <= 24);

    assert(reader.Handle({Button::Ok, InputAction::Click}) == ReaderDecision::RenderQuality);
    assert(ui.ShowReader(reader, true) == ESP_OK);
    SavePreview(ui.canvas(), "reader-options");
    assert(reader.Handle({Button::Ok, InputAction::Click}) == ReaderDecision::RenderQuality);
    assert(reader.engine().page().font == FontSize::Large && reader.engine().page().count < small);
    assert(ui.ShowReader(reader, true) == ESP_OK);
    reader.Presented(true);
    SavePreview(ui.canvas(), "reader-large");
    const auto saved = *bookmarks.Find(library.Get(0).id.data(), library.Get(0).bytes);
    assert(reader.Handle({Button::Down, InputAction::Click}) == ReaderDecision::RenderFast);
    fail_command = 0xe9;
    const auto failed = ui.ShowReader(reader, false);
    assert(failed == ESP_FAIL);
    reader.Presented(false);
    assert(*bookmarks.Find(saved.book_id.data(), saved.source_bytes) == saved);
    assert(reader.Tick(0) == ReaderDecision::RenderQuality);
    assert(ui.ShowReader(reader, true) == ESP_OK);
    reader.Presented(true);
    assert(saved.position < bookmarks.Find(saved.book_id.data(), saved.source_bytes)->position);
    assert(reader.Tick(1) == ReaderDecision::None);
}

void TestBookTransferComposition() {
    using namespace zectrix::connectivity;
    using namespace zectrix::app;
    Reset();
    auto service = CreateService();
    ZectrixDemoUi ui(service.get());
    zectrix::ui::StatusBarState status;
    status.time_valid = status.battery_valid = true;
    status.hour = 20; status.minute = 26; status.battery_percent = 82;
    ui.UpdateStatus(status);
    BookTransferController controller;
    assert(zectrix::sdk::IsOk(controller.Start()));
    BookTransferSnapshot transfer;
    assert(ui.ShowBookTransfer(transfer, true, false, true) == ESP_OK);
    SavePreview(ui.canvas(), "books-mode");
    assert(ui.ShowBookTransfer(transfer, true, true, false) == ESP_OK);
    SavePreview(ui.canvas(), "books-mode-station");
    assert(controller.Handle({zectrix::sdk::Button::Ok, zectrix::sdk::InputAction::Click}) == BookTransferDecision::Hotspot);
    transfer.state = BookTransferState::Starting;
    std::strcpy(transfer.ssid.data(), "NOTE4-1234");
    std::strcpy(transfer.code.data(), "ABCDEFGH2345");
    assert(controller.Update(transfer, 0) == BookTransferDecision::RenderQuality);
    assert(ui.ShowBookTransfer(controller.snapshot(), false, false, true) == ESP_OK);
    SavePreview(ui.canvas(), "books-starting");
    transfer.state = BookTransferState::Sharing;
    transfer.expected = 20000; transfer.received = 11000; transfer.uploaded = 2;
    std::strcpy(transfer.address.data(), "192.168.4.1");
    status.wifi = zectrix::ui::RadioIndicator::Connected;
    ui.UpdateStatus(status);
    assert(controller.Update(transfer, 1000000) == BookTransferDecision::RenderQuality);
    assert(ui.ShowBookTransfer(controller.snapshot(), false, false, true) == ESP_OK);
    SavePreview(ui.canvas(), "books-sharing");
    Frame page;
    std::memcpy(page.data(), ui.canvas().data(), page.size());
    ++status.minute;
    ui.UpdateStatus(status);
    assert(ui.RefreshPending() == ESP_OK);
    assert(std::memcmp(page.data() + 1200, ui.canvas().data() + 1200, page.size() - 1200) == 0);

    transfer.received = 15000;
    assert(controller.Update(transfer, 2000000) == BookTransferDecision::RenderFast);
    fail_command = 0xe9;
    assert(ui.ShowBookTransfer(controller.snapshot(), false, false, false) == ESP_FAIL);
    controller.Presented(false);
    assert(controller.Update(transfer, 2000001) == BookTransferDecision::RenderQuality);
    assert(ui.ShowBookTransfer(controller.snapshot(), false, false, true) == ESP_OK);
    controller.Presented(true);
    assert(controller.Update(transfer, 2000002) == BookTransferDecision::None);

    transfer.mode = BookTransferMode::Station;
    std::strcpy(transfer.ssid.data(), "海风书房-WiFi");
    std::strcpy(transfer.address.data(), "192.168.100.123");
    assert(ui.ShowBookTransfer(transfer, false, true, true) == ESP_OK);
    SavePreview(ui.canvas(), "books-station");
    transfer.state = BookTransferState::Complete;
    status.wifi = zectrix::ui::RadioIndicator::Off;
    ui.UpdateStatus(status);
    assert(ui.ShowBookTransfer(transfer, false, false, true) == ESP_OK);
    SavePreview(ui.canvas(), "books-complete");
    transfer.state = BookTransferState::Failed;
    transfer.error = BookTransferError::Storage;
    assert(ui.ShowBookTransfer(transfer, false, false, true) == ESP_OK);
    SavePreview(ui.canvas(), "books-storage-error");
    transfer.error = BookTransferError::Power;
    assert(ui.ShowBookTransfer(transfer, false, false, true) == ESP_OK);
    SavePreview(ui.canvas(), "books-power-error");
}

void TestSleepCoverComposition() {
    using namespace zectrix::app;
    Reset();
    auto service = CreateService();
    ZectrixDemoUi ui(service.get());
    zectrix::ui::StatusBarState status;
    status.hour = 20; status.minute = 27; status.battery_percent = 82;
    status.time_valid = status.battery_valid = true;
    status.ble = status.wifi = zectrix::ui::RadioIndicator::Connected;
    ui.UpdateStatus(status);
    SleepCoverSnapshot snapshot;
    snapshot.clock = {{2026, 9, 9, 0, 20, 27, 0}, zectrix::time::ClockSource::Rtc};
    snapshot.power.battery_valid = true; snapshot.power.battery_percent = 82;
    snapshot.has_reading = true;
    std::strcpy(snapshot.reading.book_id.data(), "风从海上来——旅途中的阅读笔记.epub");
    snapshot.reading.progress_per_mille = 425;

    assert(ui.ShowSleepCoverMenu(SleepCoverStyle::Dashboard, SleepCoverStyle::Dashboard, nullptr, true) == ESP_OK);
    SavePreview(ui.canvas(), "sleep-menu");
    assert(ui.ShowSleepCover(snapshot, SleepCoverStyle::Dashboard, true) == ESP_OK);
    SavePreview(ui.canvas(), "sleep-preview");
    Frame preview;
    std::memcpy(preview.data(), ui.canvas().data(), preview.size());
    ++status.minute;
    ui.UpdateStatus(status);
    assert(ui.RefreshPending() == ESP_OK);
    assert(std::memcmp(preview.data() + 1200, ui.canvas().data() + 1200, preview.size() - 1200) == 0);

    std::vector<uint8_t> gray(60000, 0x73);
    assert(ui.ShowImage4Bpp(gray.data(), gray.size()) == ESP_OK);
    ClearTraffic();
    assert(ui.ShowSleepCover(snapshot, SleepCoverStyle::Dashboard) == ESP_OK);
    Frame cover;
    std::memcpy(cover.data(), ui.canvas().data(), cover.size());
    CheckFull(cover);
    assert(Inspect(*service).bits_per_pixel == 1 && !service->IsPowered());
    SavePreview(ui.canvas(), "sleep-dashboard");
    ClearTraffic();
    ++status.minute; status.wifi = zectrix::ui::RadioIndicator::Off;
    ui.UpdateStatus(status);
    assert(ui.RefreshPending() == ESP_OK && ui.RefreshFull() == ESP_OK);
    assert(packets.empty() && gpio_writes == 0);
    assert(std::memcmp(cover.data(), ui.canvas().data(), cover.size()) == 0);

    snapshot.clock.value = {2025, 3, 31, 0, 8, 4, 0};
    assert(ui.ShowSleepCover(snapshot, SleepCoverStyle::Dashboard) == ESP_OK);
    SavePreview(ui.canvas(), "sleep-six-week-month");
    snapshot.clock.source = zectrix::time::ClockSource::Uptime;
    snapshot.has_reading = snapshot.power.battery_valid = false;
    assert(ui.ShowSleepCover(snapshot, SleepCoverStyle::Dashboard) == ESP_OK);
    SavePreview(ui.canvas(), "sleep-empty");
    snapshot.clock = {{2026, 9, 9, 0, 20, 27, 0}, zectrix::time::ClockSource::System};
    snapshot.power.battery_valid = true;
    assert(ui.ShowSleepCover(snapshot, SleepCoverStyle::Quote) == ESP_OK);
    SavePreview(ui.canvas(), "sleep-landscape");
    std::memcpy(cover.data(), ui.canvas().data(), cover.size());
    snapshot.has_reading = true;
    std::strcpy(snapshot.reading.book_id.data(), "Private reading.txt");
    assert(ui.ShowSleepCover(snapshot, SleepCoverStyle::Quote) == ESP_OK);
    assert(std::memcmp(cover.data(), ui.canvas().data(), cover.size()) == 0);

    snapshot.reading.progress_per_mille = UINT16_MAX;
    assert(ui.ShowSleepCover(snapshot, SleepCoverStyle::Dashboard) == ESP_OK);
    assert(!Bit(ui.canvas().data(), 50, 382, 231));
    snapshot.reading.progress_per_mille = 0;
    assert(ui.ShowSleepCover(snapshot, SleepCoverStyle::Dashboard) == ESP_OK);
    assert(Bit(ui.canvas().data(), 50, 17, 231));
    assert(ui.ShowSleepCover(snapshot, SleepCoverStyle::Blank, true, false) == ESP_OK);
    SavePreview(ui.canvas(), "sleep-blank-preview");
    ClearTraffic();
    assert(ui.ShowSleepCover(snapshot, SleepCoverStyle::Blank) == ESP_OK);
    Frame white;
    white.fill(0xff);
    CheckFull(white);
    SavePreview(ui.canvas(), "sleep-blank");

    assert(ui.ShowAbout() == ESP_OK);
    fail_command = 0xe9;
    assert(ui.ShowSleepCover(snapshot, SleepCoverStyle::Dashboard) == ESP_FAIL);
    ClearTraffic();
    assert(ui.ClearDisplay() == ESP_OK);
    CheckFull(white);
    assert(!service->IsPowered());
    assert(ui.ShowSleepCover(snapshot, SleepCoverStyle::Dashboard) == ESP_OK);
    std::memcpy(cover.data(), ui.canvas().data(), cover.size());
    ClearTraffic();
    service.reset();
    assert(packets.empty() && !bus_active && devices == 0 && mutexes == 0 && allocations.empty());
    assert(std::memcmp(ui.canvas().data(), cover.data(), cover.size()) == 0);
}

}  // namespace

void* operator new(std::size_t size, const std::nothrow_t&) noexcept {
    if (++nothrow_allocations == fail_allocation_at) return nullptr;
    try { return ::operator new(size); } catch (...) { return nullptr; }
}
void operator delete(void* pointer, const std::nothrow_t&) noexcept { ::operator delete(pointer); }
void* heap_caps_malloc(std::size_t size, uint32_t) {
    if (++heap_allocations == fail_heap_at) return nullptr;
    void* pointer = std::malloc(size);
    if (pointer != nullptr) allocations[pointer] = size;
    return pointer;
}
void heap_caps_free(void* pointer) {
    assert(allocations.erase(pointer) == 1);
    std::free(pointer);
}
SemaphoreHandle_t xSemaphoreCreateMutex() { ++mutexes; return new FakeSemaphore; }
BaseType_t xSemaphoreTake(SemaphoreHandle_t mutex, TickType_t) {
    if (fail_lock) { fail_lock = false; return pdFALSE; }
    assert(!mutex->locked);
    mutex->locked = true;
    return pdTRUE;
}
BaseType_t xSemaphoreGive(SemaphoreHandle_t mutex) {
    assert(mutex->locked);
    mutex->locked = false;
    return pdTRUE;
}
void vSemaphoreDelete(SemaphoreHandle_t mutex) { assert(!mutex->locked); --mutexes; delete mutex; }
void vTaskDelay(TickType_t ticks) { now_us += static_cast<int64_t>(ticks) * 1000; }
int64_t esp_timer_get_time() { return now_us; }
int64_t zectrix::time::TimeService::MonotonicMicroseconds() const { return now_us; }
const char* ZectrixSelfTest::Name(ZectrixTestId) { return "test"; }
const char* esp_err_to_name(esp_err_t err) { return err == ESP_OK ? "ESP_OK" : "ESP_FAIL"; }
esp_err_t gpio_config(const gpio_config_t* config) {
    for (unsigned pin = 0; pin < pins.size(); ++pin) {
        if ((config->pin_bit_mask & (1ULL << pin)) == 0) continue;
        modes[pin] = config->mode;
        pullups[pin] = config->pull_up_en;
    }
    return ESP_OK;
}
esp_err_t gpio_set_level(gpio_num_t pin, int value) {
    ++gpio_writes;
    if (pin == GPIO_NUM_6 && value == 0 && fail_power_off) {
        fail_power_off = false;
        return ESP_FAIL;
    }
    pins[pin] = value;
    return ESP_OK;
}
int gpio_get_level(gpio_num_t pin) {
    assert(pin == GPIO_NUM_8);
    return timeout_refresh && !packets.empty() && packets.back().command == 0x12 ? 0 : 1;
}
esp_err_t gpio_hold_dis(gpio_num_t) { return ESP_OK; }
esp_err_t gpio_hold_en(gpio_num_t) { return ESP_OK; }
esp_err_t spi_bus_initialize(spi_host_device_t, const spi_bus_config_t*, int) {
    assert(!bus_active);
    bus_active = true;
    return ESP_OK;
}
esp_err_t spi_bus_free(spi_host_device_t) {
    assert(bus_active && devices == 0);
    bus_active = false;
    return ESP_OK;
}
esp_err_t spi_bus_add_device(spi_host_device_t, const spi_device_interface_config_t*, spi_device_handle_t* device) {
    assert(bus_active && devices == 0);
    *device = new FakeSpiDevice;
    ++devices;
    return ESP_OK;
}
esp_err_t spi_bus_remove_device(spi_device_handle_t device) { --devices; delete device; return ESP_OK; }
esp_err_t spi_device_polling_transmit(spi_device_handle_t, spi_transaction_t* transaction) {
    assert(pins[GPIO_NUM_11] == 0 && pins[GPIO_NUM_6] == 1);
    if (transaction->flags & SPI_TRANS_USE_RXDATA) {
        transaction->rx_data[0] = 25;
        return ESP_OK;
    }
    const auto* bytes = transaction->flags & SPI_TRANS_USE_TXDATA ? transaction->tx_data :
        static_cast<const uint8_t*>(transaction->tx_buffer);
    const auto size = transaction->length / 8;
    assert(size > 0 && size <= 1024 && transaction->length % 8 == 0);
    if (pins[GPIO_NUM_10] == 0) {
        assert(size == 1);
        if (bytes[0] == fail_command) { fail_command = -1; return ESP_FAIL; }
        packets.push_back({bytes[0], {}});
    } else {
        assert(!packets.empty());
        if (packets.back().command == fail_data) { fail_data = -1; return ESP_FAIL; }
        packets.back().data.insert(packets.back().data.end(), bytes, bytes + size);
    }
    return ESP_OK;
}

int main() {
    TestCreationAndInputErrors();
    TestAutomaticRefreshAndBudget();
    TestHighContrastAndSparseChanges();
    TestAccumulatedChanges();
    TestPatchCompatibility();
    TestDriverDiffAndWindow();
    TestFailuresRecoverWithFullFrame();
    TestBatchAndGray();
    TestShutdownReleasesSpi();
    TestUiTraffic();
    TestViewPorts();
    TestStatusAndImageComposition();
    TestReaderComposition();
    TestBookTransferComposition();
    TestSleepCoverComposition();
    Reset();
}

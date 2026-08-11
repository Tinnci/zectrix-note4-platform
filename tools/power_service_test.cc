#include "zectrix_power_service.h"
#include <cassert>

int main() {
    using namespace zectrix::power;
    const PowerSnapshot snapshot{true, 3900, 72, true, false};
    assert(snapshot.battery_valid && snapshot.battery_mv == 3900);
    assert(snapshot.external_power_present && !snapshot.charging);
    assert(WakeReason::PowerOn != WakeReason::Timer);
}

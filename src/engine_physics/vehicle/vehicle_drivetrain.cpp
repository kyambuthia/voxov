#include "engine_physics/vehicle/vehicle_drivetrain.hpp"

#include <algorithm>
#include <cmath>

VehicleDrivetrain::VehicleDrivetrain() {
    gears = {
        {0.0f, 0.0f, 0.0f},
        {3.20f, 5200.0f, 1200.0f},
        {2.10f, 5600.0f, 1700.0f},
        {1.45f, 5900.0f, 1900.0f},
        {1.05f, 6200.0f, 2100.0f},
        {0.84f, 6500.0f, 2400.0f}};
}

void VehicleDrivetrain::set_gears(const std::vector<GearState> &new_gears) {
    if (new_gears.size() < 2) {
        return;
    }
    gears = new_gears;
    reset();
}

void VehicleDrivetrain::reset() {
    gear_index = std::clamp(1, 1, static_cast<int>(gears.size()) - 1);
    current = DrivetrainTelemetry{};
    current.current_gear = gear_index;
}

float VehicleDrivetrain::update(float throttle, float wheel_speed_mps, float dt_seconds) {
    if (gears.size() < 2) {
        return std::clamp(throttle, -1.0f, 1.0f);
    }
    const float abs_wheel_speed = std::fabs(wheel_speed_mps);
    const GearState &gear = gears[gear_index];
    const float target_rpm = 900.0f + abs_wheel_speed * gear.ratio * 250.0f + std::max(0.0f, throttle) * 1400.0f;
    current.engine_rpm += (target_rpm - current.engine_rpm) * std::clamp(dt_seconds * 10.0f, 0.0f, 1.0f);
    current.engine_rpm = std::clamp(current.engine_rpm, 700.0f, 7600.0f);

    if (gear_index < static_cast<int>(gears.size()) - 1 && current.engine_rpm > gear.upshift_rpm) {
        ++gear_index;
    } else if (gear_index > 1 && current.engine_rpm < gear.downshift_rpm) {
        --gear_index;
    }

    current.current_gear = gear_index;
    current.wheel_speed_mps = wheel_speed_mps;
    const float ratio_scale = gears[gear_index].ratio / std::max(gears[1].ratio, 0.1f);
    return std::clamp(throttle, -1.0f, 1.0f) * std::clamp(ratio_scale, 0.2f, 1.4f);
}

const DrivetrainTelemetry &VehicleDrivetrain::telemetry() const {
    return current;
}

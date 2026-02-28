#pragma once

#include <vector>

struct GearState {
    float ratio = 1.0f;
    float upshift_rpm = 5200.0f;
    float downshift_rpm = 1500.0f;
};

struct DrivetrainTelemetry {
    int current_gear = 1;
    float engine_rpm = 900.0f;
    float wheel_speed_mps = 0.0f;
};

class VehicleDrivetrain {
public:
    VehicleDrivetrain();

    void set_gears(const std::vector<GearState> &new_gears);
    void reset();
    float update(float throttle, float wheel_speed_mps, float dt_seconds);

    const DrivetrainTelemetry &telemetry() const;

private:
    std::vector<GearState> gears;
    int gear_index = 1;
    DrivetrainTelemetry current{};
};

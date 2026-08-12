#pragma once

#include <cstdint>
#include <string_view>

enum class ExpeditionStage : uint8_t {
    CollectSample = 0,
    DeployBeacon = 1,
    PlotCourse = 2,
    ReachAster = 3,
    Complete = 4,
};

class ExpeditionMission {
public:
    void reset();

    void on_block_removed(int32_t body_index);
    void on_block_placed(int32_t body_index);
    void on_course_locked(int32_t body_index);
    void on_landed(int32_t body_index);

    ExpeditionStage stage() const;
    bool complete() const;
    float progress() const;
    std::string_view status() const;
    std::string_view hint() const;

private:
    void advance(ExpeditionStage expected, ExpeditionStage next);

    ExpeditionStage stage_ = ExpeditionStage::CollectSample;
};

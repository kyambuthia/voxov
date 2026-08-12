#include "engine_gameplay/objectives/expedition_mission.hpp"

namespace {
constexpr int32_t kVoxovBodyIndex = 1;
constexpr int32_t kAsterBodyIndex = 3;
}

void ExpeditionMission::reset() {
    stage_ = ExpeditionStage::CollectSample;
}

void ExpeditionMission::on_block_removed(int32_t body_index) {
    if (body_index == kVoxovBodyIndex) {
        advance(ExpeditionStage::CollectSample, ExpeditionStage::DeployBeacon);
    }
}

void ExpeditionMission::on_block_placed(int32_t body_index) {
    if (body_index == kVoxovBodyIndex) {
        advance(ExpeditionStage::DeployBeacon, ExpeditionStage::PlotCourse);
    }
}

void ExpeditionMission::on_course_locked(int32_t body_index) {
    if (body_index == kAsterBodyIndex) {
        advance(ExpeditionStage::PlotCourse, ExpeditionStage::ReachAster);
    }
}

void ExpeditionMission::on_landed(int32_t body_index) {
    if (body_index == kAsterBodyIndex) {
        advance(ExpeditionStage::ReachAster, ExpeditionStage::Complete);
    }
}

ExpeditionStage ExpeditionMission::stage() const {
    return stage_;
}

bool ExpeditionMission::complete() const {
    return stage_ == ExpeditionStage::Complete;
}

float ExpeditionMission::progress() const {
    return static_cast<float>(stage_) /
        static_cast<float>(ExpeditionStage::Complete);
}

std::string_view ExpeditionMission::status() const {
    switch (stage_) {
    case ExpeditionStage::CollectSample:
        return "EXPEDITION 1/4  COLLECT A TERRAIN SAMPLE";
    case ExpeditionStage::DeployBeacon:
        return "EXPEDITION 2/4  DEPLOY A STONE BEACON";
    case ExpeditionStage::PlotCourse:
        return "EXPEDITION 3/4  PLOT A COURSE TO ASTER";
    case ExpeditionStage::ReachAster:
        return "EXPEDITION 4/4  REACH ASTER";
    case ExpeditionStage::Complete:
        return "EXPEDITION COMPLETE  ASTER REACHED";
    }
    return {};
}

std::string_view ExpeditionMission::hint() const {
    switch (stage_) {
    case ExpeditionStage::CollectSample:
        return "Aim at terrain and break one block [LMB]";
    case ExpeditionStage::DeployBeacon:
        return "Place one stone block as a beacon [RMB]";
    case ExpeditionStage::PlotCourse:
        return "Open sky navigation [F6], select ASTER, then lock [ENTER]";
    case ExpeditionStage::ReachAster:
        return "Keep sky navigation open and hold [W] to engage flight assist";
    case ExpeditionStage::Complete:
        return "Landing confirmed. Explore, build, or return to Voxov.";
    }
    return {};
}

void ExpeditionMission::advance(ExpeditionStage expected,
                                ExpeditionStage next) {
    if (stage_ == expected) {
        stage_ = next;
    }
}

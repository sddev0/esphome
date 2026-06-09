#pragma once

#include "esphome/core/component.h"
#include "esphome/components/novoferm/novoferm.h"
#include "esphome/components/cover/cover.h"
namespace esphome {
namespace novoferm {

inline cover::CoverOperation gate_status_to_cover_operation(GateStatus s) {
  switch (s) {
    case OPENING:
      return cover::CoverOperation::COVER_OPERATION_OPENING;
    case CLOSING:
      return cover::CoverOperation::COVER_OPERATION_CLOSING;
    // fall through cases if gate is idle
    case OPENED:
    case CLOSED:
    case PAUSED:
    case VENTILATING:
    default:
      return cover::CoverOperation::COVER_OPERATION_IDLE;
  }
}

class NovofermCover : public cover::Cover, public PollingComponent {
 public:
  void loop() override;
  void update() override;
  void setup() override;
  void dump_config() override;

  void set_novoferm_parent(Novoferm *parent) { this->parent_ = parent; }
  void set_open_duration(uint32_t duration) { this->open_duration_ = duration; }
  void set_close_duration(uint32_t duration) { this->close_duration_ = duration; }
  void set_learn_cycle_times(bool enabled) { this->learn_cycle_times_ = enabled; }

  void publish_state(bool save = true, uint32_t ratelimit = 0);

  void ventilation_mode(bool active);

 protected:
  void control(const cover::CoverCall &call) override;

  cover::CoverTraits get_traits() override;
  Novoferm *parent_;
  GateStatus current_status_{PAUSED};

  void recalibrate_duration_(GateStatus s);
  void recompute_position_();
  void control_position_(float target);
  void stop_at_target_();

  uint32_t open_duration_{0};
  uint32_t close_duration_{0};
  bool learn_cycle_times_ = true;

  uint32_t last_publish_time_{0};
  uint32_t last_recompute_time_{0};
  uint32_t direction_start_time_{0};
  optional<float> target_position_{};
};
}  // namespace novoferm
}  // namespace esphome

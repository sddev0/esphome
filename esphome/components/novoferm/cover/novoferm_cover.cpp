#include "esphome/core/log.h"
#include "novoferm_cover.h"

namespace esphome {
namespace novoferm {

using namespace esphome::cover;

static const char *const TAG = "novoferm.cover";

void NovofermCover::loop() {
  this->recompute_position_();
  this->stop_at_target_();
}

void NovofermCover::update() { this->parent_->request_gate_status(); }

void NovofermCover::setup() {
  this->parent_->add_on_initialized_callback([this]() {
    auto restore = this->restore_state_();
    if (restore.has_value()) {
      restore->apply(this);
      return;
    }

    // Assume gate is closed without preexisting state.
    this->position = 0.0f;
  });

  this->parent_->set_cover_state_listener([this](const GateStatus &s) {
    if (this->current_status_ == s) {
      return;
    }

    // Learn new open/close cycle times if enabled
    if (this->learn_cycle_times_) {
      this->recalibrate_duration_(s);
    }

    ESP_LOGI(TAG, "Status changed from %s to %s", gate_status_to_str(this->current_status_), gate_status_to_str(s));

    switch (s) {
      case OPENED:
        // The Novoferm 423 doesn't respond to the first 'Close' command after
        // being opened completely. Sending a pause command after opening fixes
        // that.
        this->parent_->perform_gate_action(GateAction::STOP_OR_CLOSE);
        this->position = cover::COVER_OPEN;
        break;
      case CLOSED:
        this->position = cover::COVER_CLOSED;
        break;
      default:
        break;
    }

    this->current_status_ = s;
    this->current_operation = gate_status_to_cover_operation(s);

    this->publish_state(true);

    // This timestamp is used to generate position deltas on every loop() while
    // the gate is moving. Bump it on each state transition so the first tick
    // doesn't generate a huge delta.
    this->last_recompute_time_ = millis();
  });
}

void NovofermCover::control(const cover::CoverCall &call) {
  if (call.get_stop()) {
    this->parent_->perform_gate_action(GateAction::STOP_OR_CLOSE);
    return;
  }

  if (call.get_position().has_value()) {
    auto pos = call.get_position().value();
    this->control_position_(pos);
    return;
  }
}

// Wrap the Cover's publish_state with a rate limiter. Publishes if the last
// publish was longer than ratelimit milliseconds ago. 0 to disable.
void NovofermCover::publish_state(bool save, uint32_t ratelimit) {
  auto now = millis();
  if ((now - this->last_publish_time_) < ratelimit) {
    return;
  }
  this->last_publish_time_ = now;

  Cover::publish_state(save);
};

// Recalibrate the gate's estimated open or close duration based on the
// actual time the operation took.
void NovofermCover::recalibrate_duration_(GateStatus s) {
  auto now = millis();
  auto old = this->current_status_;

  // Gate paused halfway through opening or closing, invalidate the start time
  // of the current operation. Close/open durations can only be accurately
  // calibrated on full open or close cycle due to motor acceleration.
  if (s == PAUSED) {
    ESP_LOGD(TAG, "Gate paused, clearing direction start time");
    this->direction_start_time_ = 0;
    return;
  }

  // Record the start time of a state transition if the gate was in the fully
  // open or closed position before the command.
  if ((old == CLOSED && s == OPENING) || (old == OPENED && s == CLOSING)) {
    ESP_LOGD(TAG, "Gate started moving from fully open or closed state");
    this->direction_start_time_ = now;
    return;
  }

  // The gate was resumed from a paused state, don't attempt recalibration.
  if (this->direction_start_time_ == 0) {
    return;
  }

  if (s == OPENED) {
    this->open_duration_ = now - this->direction_start_time_;
    ESP_LOGI(TAG, "Recalibrated the gate's open duration to %dms", this->open_duration_);
  }
  if (s == CLOSED) {
    this->close_duration_ = now - this->direction_start_time_;
    ESP_LOGI(TAG, "Recalibrated the gate's close duration to %dms", this->close_duration_);
  }

  this->direction_start_time_ = 0;
}

// Recompute the gate's position and publish the results while
// the gate is moving. No-op when the gate is idle.
void NovofermCover::recompute_position_() {
  if (this->current_operation == COVER_OPERATION_IDLE) {
    return;
  }

  const uint32_t now = millis();
  uint32_t diff = now - this->last_recompute_time_;

  auto direction = +1.0f;
  uint32_t duration = this->open_duration_;
  if (this->current_operation == COVER_OPERATION_CLOSING) {
    direction = -1.0f;
    duration = this->close_duration_;
  }

  auto delta = direction * diff / duration;

  this->position = clamp(this->position + delta, cover::COVER_CLOSED, cover::COVER_OPEN);

  this->last_recompute_time_ = now;

  this->publish_state(true, 250);
}

// Start moving the gate in the direction of the target position.
void NovofermCover::control_position_(float target) {
  if (target == this->position) {
    return;
  }

  if (target == cover::COVER_OPEN) {
    ESP_LOGI(TAG, "Fully opening gate");
    this->parent_->perform_gate_action(GateAction::OPEN);
    return;
  }
  if (target == cover::COVER_CLOSED) {
    ESP_LOGI(TAG, "Fully closing gate");
    this->parent_->perform_gate_action(GateAction::STOP_OR_CLOSE);
    return;
  }

  // Don't set target position when fully opening or closing the gate, the gate
  // stops automatically when it reaches the configured open/closed positions.
  this->target_position_ = target;

  if (target > this->position) {
    ESP_LOGI(TAG, "Opening gate towards %.1f", target);
    this->parent_->perform_gate_action(GateAction::OPEN);
    return;
  }

  if (target < this->position) {
    ESP_LOGI(TAG, "Closing gate towards %.1f", target);
    this->parent_->perform_gate_action(GateAction::STOP_OR_CLOSE);
    return;
  }
}

// Stop the gate if it is moving at or beyond its target position. Target
// position is only set when the gate is requested to move to a halfway
// position.
void NovofermCover::stop_at_target_() {
  if (this->current_operation == COVER_OPERATION_IDLE) {
    return;
  }
  if (!this->target_position_) {
    return;
  }
  auto target = this->target_position_.value();

  if (this->current_operation == COVER_OPERATION_OPENING && this->position < target) {
    return;
  }
  if (this->current_operation == COVER_OPERATION_CLOSING && this->position > target) {
    return;
  }

  this->parent_->perform_gate_action(GateAction::STOP_OR_CLOSE);
  this->target_position_.reset();
}

void NovofermCover::ventilation_mode(bool active) {
  ESP_LOGI(TAG, "Setting ventilation mode to: %s", active ? "true" : "false");
  if (active)
    this->parent_->perform_gate_action(GateAction::VENTILATE);
  else
    this->parent_->perform_gate_action(GateAction::STOP_OR_CLOSE);
}

void NovofermCover::dump_config() {
  LOG_COVER("", "Novoferm Cover", this);

  ESP_LOGCONFIG(TAG,
                "  Open Duration: %.1fs\n"
                "  Close Duration: %.1fs",
                this->open_duration_ / 1e3f, this->close_duration_ / 1e3f);

  LOG_UPDATE_INTERVAL(this);
  if (this->learn_cycle_times_) {
    ESP_LOGCONFIG(TAG, "  Learn open/close durations from movement: YES");
  }
  auto restore = this->restore_state_();
  if (restore.has_value()) {
    ESP_LOGCONFIG(TAG, "  Saved position %d%%", (int) (restore->position * 100.f));
  }
}

cover::CoverTraits NovofermCover::get_traits() {
  auto traits = cover::CoverTraits();
  traits.set_supports_stop(true);
  traits.set_supports_position(true);
  traits.set_is_assumed_state(false);
  return traits;
}

}  // namespace novoferm
}  // namespace esphome

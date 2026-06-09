#include "esphome/core/log.h"
#include "novoferm_light.h"
#include "esphome/core/helpers.h"

namespace esphome {
namespace novoferm {

static const char *const TAG = "novoferm.light";

void NovofermLight::setup() {
  this->parent_->set_light_state_listener([this](const LightStatus &status) {
    // After the first update was received we can do duplicate checks, this feels wrong fixmey
    if (first_update_received_ && this->current_status_ == status) {
      return;
    }

    ESP_LOGD(TAG, "Status changed from %s to %s", light_status_to_str(this->current_status_),
             light_status_to_str(status));
    this->current_status_ = status;

    auto call = this->state_->make_call();
    call.set_state(status == LightStatus::ON);
    call.set_save(false);
    call.perform();
    first_update_received_ = true;
  });
}

void NovofermLight::update() { this->parent_->request_light_status(); }

void NovofermLight::setup_state(light::LightState *state) { state_ = state; }

void NovofermLight::write_state(light::LightState *state) {
  bool current = (this->current_status_ == LightStatus::ON);
  bool requested = state->current_values.is_on();

  if (current == requested) {
    return;
  }

  ESP_LOGI(TAG, "Changing state from %s to %s", ONOFF(current), ONOFF(requested));
  this->parent_->perform_light_action(requested);
}

void NovofermLight::dump_config() {
  ESP_LOGCONFIG(TAG, "Novoferm Light");
  LOG_UPDATE_INTERVAL(this);
}

light::LightTraits NovofermLight::get_traits() {
  auto traits = light::LightTraits();
  traits.set_supported_color_modes({light::ColorMode::ON_OFF});
  return traits;
}
}  // namespace novoferm
}  // namespace esphome

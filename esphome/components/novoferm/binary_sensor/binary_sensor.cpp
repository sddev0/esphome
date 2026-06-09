#include "binary_sensor.h"
#include "esphome/core/log.h"

namespace esphome {
namespace novoferm {

static const char *const TAG = "novoferm.binary_sensor";

void NovofermBinarySensor::setup() {
  this->parent_->set_ventilation_state_listener([this](const bool &on_off) {
    ESP_LOGV(TAG, "Novoferm ventilation state is: %s", ONOFF(on_off));
    this->publish_state(on_off);
  });
}

void NovofermBinarySensor::dump_config() { ESP_LOGCONFIG(TAG, "Novoferm Ventilation Binary Sensor"); }

}  // namespace novoferm
}  // namespace esphome

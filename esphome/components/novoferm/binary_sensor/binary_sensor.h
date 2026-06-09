#pragma once
#include "esphome/components/binary_sensor/binary_sensor.h"
#include "esphome/components/novoferm/novoferm.h"

namespace esphome {
namespace novoferm {

class NovofermBinarySensor : public binary_sensor::BinarySensor, public Component {
 public:
  void setup() override;
  void dump_config() override;

  void set_novoferm_parent(Novoferm *parent) { this->parent_ = parent; }

 protected:
  Novoferm *parent_{nullptr};
};

}  // namespace novoferm
}  // namespace esphome

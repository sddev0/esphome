#pragma once

#include "esphome/core/automation.h"
#include "esphome/components/novoferm/cover/novoferm_cover.h"

namespace esphome {
namespace novoferm {

template<typename... Ts> class VentilationAction : public Action<Ts...> {
 public:
  explicit VentilationAction(NovofermCover *cover) : cover_(cover) {}

  TEMPLATABLE_VALUE(bool, stop)

  void play(Ts... x) override {
    if (this->stop_.has_value()) {
      this->cover_->ventilation_mode(false);
    } else {
      this->cover_->ventilation_mode(true);
    }
  }

 protected:
  NovofermCover *cover_;
};

}  // namespace novoferm
}  // namespace esphome

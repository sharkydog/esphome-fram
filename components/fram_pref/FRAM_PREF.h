#pragma once

#include "esphome/core/component.h"
#include "esphome/core/preferences.h"
#include "esphome/components/fram/FRAM.h"

namespace esphome {
namespace fram_pref {

class FRAM_PREF : public Component, public ESPPreferences {
  public:
    FRAM_PREF(fram::FRAM * fram, uint16_t pool_size, uint16_t pool_start);
    
    void setup() override;
    void dump_config() override;
    float get_setup_priority() const override { return setup_priority::BUS; }
    
    ESPPreferenceObject make_preference(size_t length, uint32_t type, bool in_flash) override;
    ESPPreferenceObject make_preference(size_t length, uint32_t type) override;
    bool sync() override;
    bool reset() override;
  
  protected:
    bool _check();
    void _clear();
    
    fram::FRAM * fram_;
    uint16_t pool_size_;
    uint16_t pool_start_;
    uint16_t pool_next_;
    bool pool_cleared_{false};
    ESPPreferences * pref_prev_;
};

}  // namespace fram_pref
}  // namespace esphome

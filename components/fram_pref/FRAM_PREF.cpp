
#include "esphome/core/log.h"
#include "esphome/core/application.h"
#include "FRAM_PREF.h"

namespace esphome {
namespace fram_pref {

static const char * const TAG = "fram_pref";

class FRAMPreferenceBackend : public ESPPreferenceBackend {
  public:
    FRAMPreferenceBackend(fram::FRAM * fram, uint16_t addr) {
      fram_ = fram;
      addr_ = addr;
    }
    
    bool save(const uint8_t *data, size_t len) override {
      fram_->write(addr_, (uint8_t*)data, len);
      return true;
    }
    
    bool load(uint8_t *data, size_t len) override {
      fram_->read(addr_, data, len);
      return true; // <= crash
    }
  
  protected:
    fram::FRAM * fram_;
    uint16_t addr_;
};

FRAM_PREF::FRAM_PREF(fram::FRAM * fram, uint16_t pool_size, uint16_t pool_start=0) {
  fram_ = fram;
  pool_size_ = pool_size;
  pool_start_ = pool_start;
  pool_next_ = pool_start + 4;
}

void FRAM_PREF::setup() {
  uint16_t fram_size = fram_->getSizeBytes();
  uint16_t pool_end = pool_start_ + pool_size_;
  
  if (!fram_size) {
    ESP_LOGE(TAG, "Device returns 0 size!");
    mark_failed();
    return;
  }
  if (pool_end > fram_size) {
    ESP_LOGE(TAG, "Pool (%u-%u) does not fit in FRAM (0-%u)!", pool_start_, (pool_end-1), (fram_size-1));
    mark_failed();
    return;
  }
  if (!fram_->isConnected()) {
    ESP_LOGE(TAG, "Device connect failed!");
    mark_failed();
    return;
  }
  
  uint32_t hash = fnv1_hash(App.get_compilation_time());
  if (hash != fram_->read32(pool_start_)) {
    _clear();
    fram_->write32(pool_start_, hash);
    pool_cleared_ = true;
  }
  
  //pref_prev_ = global_preferences;
  
  global_preferences = this; // <== crash, nope, this is fine
}

void FRAM_PREF::dump_config() {
  uint16_t fram_size = fram_->getSizeBytes();
  uint16_t pool_end = pool_start_ + pool_size_;
  
  ESP_LOGCONFIG(TAG, "FRAM_PREF:");
  ESP_LOGCONFIG(TAG, "  Pool: %u bytes (%u-%u)", pool_size_, pool_start_, (pool_end-1));
  
  if (is_failed()) {
    ESP_LOGE(TAG, "  Failed!");
  }
  if (!fram_size) {
    ESP_LOGE(TAG, "  Device returns 0 size!");
    return;
  }
  
  ESP_LOGCONFIG(TAG, "  Hash: %u", fram_->read32(pool_start_));
  
  if (pool_end > fram_size) {
    ESP_LOGE(TAG, "  Does not fit in FRAM (0-%u)!", (fram_size-1));
  } else {
    if (pool_cleared_) {
      ESP_LOGD(TAG, "  Pool was cleared on boot");
    }
    ESP_LOGCONFIG(TAG, "  Pool: %u bytes used", pool_next_ - pool_start_ - 4);
  }
}

void FRAM_PREF::_clear() {
  uint8_t buff[16];
  uint16_t pool_end = pool_start_ + pool_size_;
  
  for (uint8_t i = 0; i < 16; i++) buff[i] = 0;
  
  for (uint16_t addr = pool_start_; addr < pool_end; addr += 16) {
    fram_->write(addr, buff, std::min(16,pool_end-addr));
  }
  
  ESP_LOGD(TAG, "Pool cleared!");
}

ESPPreferenceObject FRAM_PREF::make_preference(size_t length, uint32_t type, bool in_flash) {
  return make_preference(length, type);
}

ESPPreferenceObject FRAM_PREF::make_preference(size_t length, uint32_t type) {
  uint16_t pool_end = pool_start_ + pool_size_;
  uint16_t next = pool_next_ + length;
  
  if (next > pool_end) {
    return {};
  }
  
  auto *pref = new FRAMPreferenceBackend(fram_, pool_next_);
  pool_next_ = next;
  
  return {pref};
}

bool FRAM_PREF::sync() {
  //pref_prev_->sync();
  return true;
}

bool FRAM_PREF::reset() {
  _clear();
  //pref_prev_->reset();
  return true;
}

}  // namespace fram_pref
}  // namespace esphome

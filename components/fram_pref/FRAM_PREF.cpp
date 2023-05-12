
#include "esphome/core/log.h"
#include "esphome/core/application.h"
#include "FRAM_PREF.h"

namespace esphome {
namespace fram_pref {

static const char * const TAG = "fram_pref";

struct _PREF_STRUCT {
  uint32_t type;
  uint16_t size16;
  uint32_t size32;
  size_t sizet;
  uint16_t addr;
};
struct _SVLD_STRUCT {
  uint32_t type;
  uint16_t size16;
  uint32_t size32;
  size_t sizet;
  uint16_t addr;
  uint8_t scss;
  uint8_t data[3];
};

static uint8_t _pref_counter = 0;
static uint8_t _save_counter = 0;
static uint8_t _load_counter = 0;
static std::vector<_PREF_STRUCT> _prefs;
static std::vector<_SVLD_STRUCT> _saves;
static std::vector<_SVLD_STRUCT> _loads;

class FRAMPreferenceBackend : public ESPPreferenceBackend {
  public:
    FRAMPreferenceBackend(fram::FRAM * fram, uint16_t addr, uint32_t type) {
      fram_ = fram;
      addr_ = addr;
      type_ = type;
    }
    
    bool save(const uint8_t *data, size_t len) override {
      _save_counter++;
      _SVLD_STRUCT _save = {.type=type_, .size16=(uint16_t)len, .size32=len, .sizet=len, .addr=addr_, .scss=0, .data={0,0,0}};
      
      if (!fram_->isConnected()) {
        _saves.push_back(_save);
        return false;
      }
      
      fram_->write(addr_, (uint8_t*)data, len);
      fram_->write(addr_+len, (uint8_t*)&type_, 4);
      
      _save.scss = 1;
      int j = std::min<int>(3,len);
      for (uint8_t i = 0; i < j; i++) _save.data[i] = data[i];
      _saves.push_back(_save);
      
      return true;
    }
    
    bool load(uint8_t *data, size_t len) override {
      _load_counter++;
      _SVLD_STRUCT _load = {.type=type_, .size16=(uint16_t)len, .size32=len, .sizet=len, .addr=addr_, .scss=0, .data={0,0,0}};
      
      if (!fram_->isConnected()) {
        _loads.push_back(_load);
        return false;
      }
      if (type_ != fram_->read32(addr_+len)) {
        _loads.push_back(_load);
        return false;
      }
      
      fram_->read(addr_, data, len);
      
      _load.scss = 1;
      int j = std::min<int>(3,len);
      for (uint8_t i = 0; i < j; i++) _load.data[i] = data[i];
      _loads.push_back(_load);
      
      return true;
    }
  
  protected:
    fram::FRAM * fram_;
    uint16_t addr_;
    uint32_t type_;
};

FRAM_PREF::FRAM_PREF(fram::FRAM * fram, uint16_t pool_size, uint16_t pool_start=0) {
  fram_ = fram;
  pool_size_ = pool_size;
  pool_start_ = pool_start;
  pool_next_ = pool_start + 4;
}

void FRAM_PREF::setup() {
  if (!_check()) {
    mark_failed();
    return;
  }
  
  uint32_t hash = fnv1_hash(App.get_compilation_time());
  if (hash != fram_->read32(pool_start_)) {
    _clear();
    fram_->write32(pool_start_, hash);
    pool_cleared_ = true;
  }
  
  pref_prev_ = global_preferences;
  global_preferences = this;
}

void FRAM_PREF::dump_config() {
  ESP_LOGCONFIG(TAG, "FRAM_PREF:");
  ESP_LOGCONFIG(TAG, "  Pool: %u bytes (%u-%u)", pool_size_, pool_start_, (pool_start_+pool_size_-1));
  
  if (!_check()) {
    return;
  }
  
  if (pool_cleared_) {
    ESP_LOGD(TAG, "  Pool was cleared");
  }
  ESP_LOGCONFIG(TAG, "  Pool: %u bytes used", pool_next_ - pool_start_ - 4);
  
  ESP_LOGCONFIG(TAG, "  Hash: %u", fram_->read32(pool_start_));
  ESP_LOGCONFIG(TAG, "  pref prev: %p", pref_prev_);
  ESP_LOGCONFIG(TAG, "  pref glob: %p", global_preferences);
  
  ESP_LOGD(TAG, "  make_preference() called %u times", _pref_counter);
  for (auto & _pref : _prefs) {
    ESP_LOGD(TAG, "  - Size: uint16_t: %u, uint32_t: %u, size_t: %u", _pref.size16, _pref.size32, _pref.sizet);
    ESP_LOGD(TAG, "    Type: %u, Addr: %u", _pref.type, _pref.addr);
  }
  
  ESP_LOGD(TAG, "  save() called %u times", _save_counter);
  for (auto & _save : _saves) {
    ESP_LOGD(TAG, "  - Size: uint16_t: %u, uint32_t: %u, size_t: %u", _save.size16, _save.size32, _save.sizet);
    ESP_LOGD(TAG, "    Type: %u, Addr: %u, OK: %u", _save.type, _save.addr, _save.scss);
    ESP_LOGD(TAG, "    Data: 0x%X 0x%X 0x%X", _save.data[0], _save.data[1], _save.data[2]);
  }
  
  ESP_LOGD(TAG, "  load() called %u times", _load_counter);
  for (auto & _load : _loads) {
    ESP_LOGD(TAG, "  - Size: uint16_t: %u, uint32_t: %u, size_t: %u", _load.size16, _load.size32, _load.sizet);
    ESP_LOGD(TAG, "    Type: %u, Addr: %u, OK: %u", _load.type, _load.addr, _load.scss);
    ESP_LOGD(TAG, "    Data: 0x%X 0x%X 0x%X", _load.data[0], _load.data[1], _load.data[2]);
  }
}

bool FRAM_PREF::_check() {
  uint16_t fram_size = fram_->getSizeBytes();
  uint16_t pool_end = pool_start_ + pool_size_;
  
  if (!fram_size) {
    ESP_LOGE(TAG, "Device returns 0 size!");
    return false;
  }
  if (pool_end > fram_size) {
    ESP_LOGE(TAG, "Pool (%u-%u) does not fit in FRAM (0-%u)!", pool_start_, (pool_end-1), (fram_size-1));
    return false;
  }
  if (!fram_->isConnected()) {
    ESP_LOGE(TAG, "Device connect failed!");
    return false;
  }
  
  return true;
}

void FRAM_PREF::_clear() {
  uint8_t buff[16];
  uint16_t pool_end = pool_start_ + pool_size_;
  
  for (uint8_t i = 0; i < 16; i++) buff[i] = 0;
  
  for (uint16_t addr = pool_start_+4; addr < pool_end; addr += 16) {
    fram_->write(addr, buff, std::min(16,pool_end-addr));
  }
  
  ESP_LOGD(TAG, "Pool cleared!");
}

ESPPreferenceObject FRAM_PREF::make_preference(size_t length, uint32_t type, bool in_flash) {
  return make_preference(length, type);
}

ESPPreferenceObject FRAM_PREF::make_preference(size_t length, uint32_t type) {
  if (is_failed()) {
    return {};
  }
  
  uint16_t pool_end = pool_start_ + pool_size_;
  uint16_t next = pool_next_ + length + 4;
  
  if (next > pool_end) {
    return {};
  }
  
  _pref_counter++;
  _PREF_STRUCT _pref = {.type=type, .size16=(uint16_t)length, .size32=length, .sizet=length, .addr=pool_next_};
  _prefs.push_back(_pref);
  
  auto *pref = new FRAMPreferenceBackend(fram_, pool_next_, type);
  pool_next_ = next;
  
  return {pref};
}

bool FRAM_PREF::sync() {
  return pref_prev_->sync();
}

bool FRAM_PREF::reset() {
  fram_->write32(pool_start_, 0);
  return pref_prev_->reset();
}

}  // namespace fram_pref
}  // namespace esphome


#include "esphome/core/log.h"
#include "esphome/core/application.h"
#include "FRAM_PREF.h"

namespace esphome {
namespace fram_pref {

static const char * const TAG = "fram_pref";

class FRAMPreferenceBackend : public ESPPreferenceBackend {
  public:
    FRAMPreferenceBackend(FRAM_PREF * comp, uint32_t type, uint8_t idx) {
      comp_ = comp;
      type_ = type;
      idx_ = idx;
    }
    
    bool save(const uint8_t *data, size_t len) override {
      if (!comp_->fram_->isConnected()) {
        return false;
      }
      
      auto & pref = comp_->prefs_[idx_];
      
      if( (pref.size_req-2) != (uint16_t)len ) {
        return false;
      }
      
      uint16_t checksum = checksum_((uint8_t*)data, len);
      
      comp_->fram_->write(pref.addr, (uint8_t*)data, len);
      comp_->fram_->write(pref.addr+len, (uint8_t*)&checksum, 2);
      
      return true;
    }
    
    bool load(uint8_t *data, size_t len) override {
      if (!comp_->fram_->isConnected()) {
        return false;
      }
      
      auto & pref = comp_->prefs_[idx_];
      
      if( (pref.size_req-2) != (uint16_t)len ) {
        return false;
      }
      
      std::vector<uint8_t> buff;
      buff.resize(len);
      comp_->fram_->read(pref.addr, buff.data(), len);
      
      if (checksum_(buff.data(), len) != comp_->fram_->read16(pref.addr+len)) {
        return false;
      }
      
      memcpy(data, buff.data(), len);
      return true;
    }
  
  protected:
    uint16_t checksum_(uint8_t *data, size_t len) {
      uint16_t sum = (type_ >> 16) + (type_ & 0xFFFF);
      
      for (size_t i = 0; i < len; i++) {
        sum += data[i];
      }
      
      return sum;
    }
    
    FRAM_PREF * comp_;
    uint32_t type_;
    uint8_t idx_;
};

FRAM_PREF::FRAM_PREF(fram::FRAM * fram) {
  fram_ = fram;
}

void FRAM_PREF::set_pool(uint16_t pool_size, uint16_t pool_start=0) {
  pool_size_ = pool_size;
  pool_start_ = pool_start;
  pool_next_ = pool_start + 4;
}

void FRAM_PREF::set_static_pref(std::string key, uint16_t addr, uint16_t size, std::function<uint32_t()> && fn, bool persist_key) {
  uint8_t flags = FLAG_STATIC;
  
  if (persist_key) {
    flags |= FLAG_PERSIST_KEY;
  }
  
  prefs_.push_back({.key=key, .addr=addr, .size=size, .size_req=0, .flags=flags});
  prefs_static_cb_.push_back(fn);
}

void FRAM_PREF::setup() {
  if (!_check()) {
    mark_failed();
    return;
  }
  
  uint16_t fram_size = fram_->getSizeBytes();
  size_t v_size = prefs_.size();
  
  for (size_t i = 0; i < v_size; i++) {
    auto & pref = prefs_[i];
    uint16_t addr_end = pref.size ? (pref.addr + pref.size - 1) : 0;
    
    if (fram_size && (addr_end >= fram_size)) {
      pref.flags |= FLAG_ERR|FLAG_ERR_SIZE_FRAM;
    }
    
    prefs_static_map_.insert({(prefs_static_cb_[i])(), i});
  }
  
  prefs_static_cb_.clear();
  
  if (pool_size_) {
    uint32_t hash = fnv1_hash(App.get_compilation_time());
    
    if (hash != fram_->read32(pool_start_)) {
      _clear();
      fram_->write32(pool_start_, hash);
      pool_cleared_ = true;
    }
  }
  
  pref_prev_ = global_preferences;
  global_preferences = this;
}

void FRAM_PREF::dump_config() {
  uint16_t fram_size = fram_->getSizeBytes();
  
  ESP_LOGCONFIG(TAG, "FRAM_PREF:");
  
  if (!_check()) {
    return;
  }
  
  if (pool_size_) {
    uint16_t pool_end = pool_start_ + pool_size_;
    
    ESP_LOGCONFIG(TAG, "  Pool: %u bytes (%u-%u)", pool_size_, pool_start_, (pool_end-1));
    if (pool_end > fram_size) {
      ESP_LOGE(TAG, "  * Does not fit in FRAM (0-%u)!", (fram_size-1));
    }
    
    if (pool_cleared_) {
      ESP_LOGI(TAG, "  Pool was cleared");
    }
    
    ESP_LOGCONFIG(TAG, "  Pool: %u bytes used", pool_next_ - pool_start_);
  }
  
  for (auto & pref : prefs_) {
    std::string msg = str_sprintf("  Pref: key: %s", pref.key.c_str());
    
    if (pref.flags & FLAG_STATIC) {
      //msg += ", STATIC";
      
      if (pref.flags & FLAG_PERSIST_KEY) {
        msg += ", persist_key";
      }
    }
    
    if (pref.size) {
      msg += str_sprintf(", addr: %u-%u", pref.addr, (pref.addr + pref.size - 1));
    }
    if (pref.size_req) {
      msg += str_sprintf(", request size: %u", pref.size_req);
    }
    
    if (!pref.size) {
      //msg += ", IGNORE";
      ESP_LOGW(TAG, "%s", msg.c_str());
    }
    else if (pref.flags & FLAG_ERR) {
      ESP_LOGE(TAG, "%s", msg.c_str());
      
      if (pref.flags & FLAG_ERR_SIZE_REQ) {
        ESP_LOGE(TAG, "  * Requested larger size!");
      }
      if (pref.flags & FLAG_ERR_SIZE_FRAM) {
        ESP_LOGE(TAG, "  * Does not fit in FRAM (0-%u)!", (fram_size-1));
      }
      if (pref.flags & FLAG_ERR_SIZE_POOL) {
        ESP_LOGE(TAG, "  * Does not fit in pool!");
      }
    }
    else {
      ESP_LOGD(TAG, "%s", msg.c_str());
    }
  }
}

bool FRAM_PREF::_check() {
  uint16_t fram_size = fram_->getSizeBytes();
  
  if (!fram_size) {
    ESP_LOGE(TAG, "  Device returns 0 size!");
    return false;
  }
  
  if (!fram_->isConnected()) {
    ESP_LOGE(TAG, "  Device connect failed!");
    return false;
  }
  
  return true;
}

void FRAM_PREF::_clear() {
  if (!pool_size_) {
    return;
  }
  
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
  
  auto pref_static_it = prefs_static_map_.find(type);
  uint16_t size = (uint16_t)length + 2;
  uint16_t addr;
  uint8_t idx;
  
  if(pref_static_it != prefs_static_map_.end()) {
    idx = pref_static_it->second;
    auto & pref = prefs_[idx];
    
    pref.size_req = size;
    
    if (!pref.size) {
      return {};
    }
    if (pref.size < size) {
      pref.flags |= FLAG_ERR|FLAG_ERR_SIZE_REQ;
      return {};
    }
    
    addr = pref.addr;
    
    if (pref.flags & FLAG_PERSIST_KEY) {
      type = fnv1_hash(pref.key);
    }
  }
  else {
    prefs_.push_back({.key=std::to_string(type), .addr=0, .size=0, .size_req=size, .flags=0});
    idx = prefs_.size() - 1;
    
    if(!pool_size_) {
      return {};
    }
    
    uint16_t pool_end = pool_start_ + pool_size_;
    uint16_t next = pool_next_ + size;
    addr = pool_next_;
    
    prefs_[idx].addr = addr;
    prefs_[idx].size = size;
    
    if (next > pool_end) {
      prefs_[idx].flags |= FLAG_ERR|FLAG_ERR_SIZE_POOL;
      return {};
    }
    
    pool_next_ = next;
  }
  
  auto * pref = new FRAMPreferenceBackend(this, type, idx);
  
  return {pref};
}

bool FRAM_PREF::sync() {
  return pref_prev_->sync();
}

bool FRAM_PREF::reset() {
  if (pool_size_) {
    fram_->write32(pool_start_, 0);
  }
  return pref_prev_->reset();
}

}  // namespace fram_pref
}  // namespace esphome

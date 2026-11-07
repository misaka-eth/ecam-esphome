#include "ecam.h"
#include "esphome/core/hal.h"
#include "esphome/core/log.h"
#include "esphome/core/defines.h"
#ifdef USE_WIFI
#include "esphome/components/wifi/wifi_component.h"
#endif
#ifdef USE_API
#include "esphome/components/api/api_server.h"
#endif

namespace esphome {
namespace ecam {

static const char *const TAG = "ecam";

// Request: 0d <len> <payload> <crc16>; answer: d0 <len> <payload> <crc16>; len = total - 1.
static const uint8_t REQ = 0x0D;
static const uint8_t ANS = 0xD0;
static const uint32_t POLL_MS = 1000;
static const uint32_t WIFI_MS = 5000;
static const uint32_t STALE_MS = 5000;

uint16_t Ecam::crc_(const uint8_t *data, size_t len) {
  uint16_t c = 0x1D0F;
  for (size_t i = 0; i < len; i++) {
    c ^= uint16_t(data[i]) << 8;
    for (int b = 0; b < 8; b++)
      c = (c & 0x8000) ? uint16_t((c << 1) ^ 0x1021) : uint16_t(c << 1);
  }
  return c;
}

void Ecam::setup() { this->rx_.reserve(64); }

// Icon on the machine display (ed f0 3x, mapped by hand on the Eletta Explore):
// 35 grey = no wifi, 32 cloud crossed = wifi but no Home Assistant, otherwise bars by RSSI:
// 31 four, 33 three, 34 one, 36 two red (very weak).
uint8_t Ecam::wifi_icon_now_() const {
  if (this->wifi_icon_)
    return this->wifi_icon_;
#ifdef USE_WIFI
  if (wifi::global_wifi_component == nullptr || !wifi::global_wifi_component->is_connected())
    return 0x35;
#ifdef USE_API
  if (api::global_api_server == nullptr || !api::global_api_server->is_connected())
    return 0x32;
#endif
  int rssi = wifi::global_wifi_component->wifi_rssi();
  if (rssi >= -60) return 0x31;
  if (rssi >= -70) return 0x33;
  if (rssi >= -80) return 0x34;
  return 0x36;
#else
  return 0x30;
#endif
}

void Ecam::send_(const std::vector<uint8_t> &payload) {
  std::vector<uint8_t> f;
  f.reserve(payload.size() + 4);
  f.push_back(REQ);
  f.push_back(uint8_t(payload.size() + 3));
  f.insert(f.end(), payload.begin(), payload.end());
  uint16_t c = crc_(f.data(), f.size());
  f.push_back(c >> 8);
  f.push_back(c & 0xFF);
  this->write_array(f);
}

void Ecam::enqueue_(std::vector<uint8_t> payload, int retries, uint32_t timeout_ms,
                    std::function<void(const std::vector<uint8_t> *)> done, bool front) {
  Request r{std::move(payload), retries, timeout_ms, std::move(done)};
  // Commands jump the queue, but never ahead of the request already on the wire.
  if (front && this->in_flight_)
    this->queue_.insert(this->queue_.begin() + 1, std::move(r));
  else if (front)
    this->queue_.push_front(std::move(r));
  else
    this->queue_.push_back(std::move(r));
}

void Ecam::loop() {
  // Read and frame answers.
  while (this->available()) {
    uint8_t b;
    this->read_byte(&b);
    if (this->rx_.empty() && b != ANS)
      continue;
    this->rx_.push_back(b);
    if (this->rx_.size() >= 2) {
      size_t total = size_t(this->rx_[1]) + 1;
      if (this->rx_[1] < 4 || total > 64) {
        this->rx_.clear();
        continue;
      }
      if (this->rx_.size() == total) {
        uint16_t got = (uint16_t(this->rx_[total - 2]) << 8) | this->rx_[total - 1];
        if (crc_(this->rx_.data(), total - 2) == got) {
          this->on_frame_(this->rx_);
        } else {
          ESP_LOGV(TAG, "bad crc, dropping frame");
        }
        this->rx_.clear();
      }
    }
  }

  uint32_t now = millis();

  // Request in flight: wait for its answer or retry/give up.
  if (this->in_flight_) {
    if (now - this->sent_at_ < this->queue_.front().timeout_ms)
      return;
    Request &r = this->queue_.front();
    if (++this->attempts_ < r.retries) {
      this->send_(r.payload);
      this->sent_at_ = now;
      return;
    }
    ESP_LOGD(TAG, "no answer to %02X", r.payload[0]);
    auto done = std::move(r.done);
    this->queue_.pop_front();
    this->in_flight_ = false;
    if (done)
      done(nullptr);
  }

  // Background traffic, only when nothing else is queued.
  if (this->queue_.empty()) {
    if (now - this->wifi_at_ >= WIFI_MS) {
      // The stock module repeats its wifi state every 5 s; no answer comes back.
      this->wifi_at_ = now;
      this->send_({0xED, 0xF0, this->wifi_icon_now_()});
      return;
    }
    if (now - this->status_at_ >= POLL_MS)
      this->enqueue_({0x75, 0x0F}, 1, 300);
  }
  if (this->status_valid_ && now - this->status_at_ > STALE_MS) {
    this->status_valid_ = false;
    for (auto &cb : this->status_cbs_)
      cb();
  }

  if (!this->queue_.empty()) {
    this->attempts_ = 0;
    this->send_(this->queue_.front().payload);
    this->sent_at_ = now;
    this->in_flight_ = true;
  }
}

void Ecam::on_frame_(const std::vector<uint8_t> &f) {
  if (f[2] == 0x75 && f[1] == 0x12) {
    this->status_.accessory = f[4];
    this->status_.switches = f[5] | (uint16_t(f[6]) << 8);
    this->status_.alarms = f[7] | (uint16_t(f[8]) << 8);
    this->status_.state = f[9];
    this->status_.phase = f[10];
    this->status_.progress = f[11];
    this->status_.flags = f[12];
    this->status_valid_ = true;
    this->status_at_ = millis();
    for (auto &cb : this->status_cbs_)
      cb();
  }
  if (this->in_flight_ && f[2] == this->queue_.front().payload[0]) {
    auto done = std::move(this->queue_.front().done);
    this->queue_.pop_front();
    this->in_flight_ = false;
    if (done)
      done(&f);
  }
}

void Ecam::power_on() {
  this->enqueue_({0x84, 0x0F, 0x02, 0x01}, 3, 800, [](const std::vector<uint8_t> *f) {
    ESP_LOGI(TAG, "power on: %s", f ? "ok" : "no answer");
  }, true);
}

void Ecam::power_off() {
  this->enqueue_({0x84, 0x0F, 0x01, 0x01}, 3, 800, [](const std::vector<uint8_t> *f) {
    ESP_LOGI(TAG, "standby: %s", f ? "ok" : "no answer");
  }, true);
}

bool Ecam::brew(int drink, int coffee_ml, int water_ml, float milk_s, int strength, int temp) {
  if (drink <= 0 || drink > 0xFF) {
    ESP_LOGW(TAG, "brew: bad drink id %d", drink);
    return false;
  }
  if (!this->idle() || this->status_.state != 0x07) {
    ESP_LOGW(TAG, "brew refused: machine not ready");
    return false;
  }
  // 83 f0 <drink> 03 <tlv...> 06 ; missing params come from the stored recipe.
  std::vector<uint8_t> p{0x83, 0xF0, uint8_t(drink), 0x03};
  if (coffee_ml > 0) { p.push_back(0x01); p.push_back(coffee_ml >> 8); p.push_back(coffee_ml & 0xFF); }
  if (strength > 0) { p.push_back(0x02); p.push_back(uint8_t(strength)); }
  if (water_ml > 0) { p.push_back(0x0F); p.push_back(water_ml >> 8); p.push_back(water_ml & 0xFF); }
  if (milk_s > 0) {
    int ds = int(milk_s * 10 + 0.5f);
    p.push_back(0x09); p.push_back(ds >> 8); p.push_back(ds & 0xFF);
  }
  if (temp >= 0 && temp <= 3) {
    // The app pairs 0d (water temp 0..3) with 1b 01 on max and 1b 04 otherwise.
    p.push_back(0x1B); p.push_back(temp == 3 ? 0x01 : 0x04);
    p.push_back(0x0D); p.push_back(uint8_t(temp));
  }
  p.push_back(0x06);
  this->last_drink_ = uint8_t(drink);
  // Retrying a brew could start it twice: one attempt, generous timeout.
  this->enqueue_(std::move(p), 1, 1500, [drink](const std::vector<uint8_t> *f) {
    bool ok = f && f->size() >= 6 && (*f)[4] == 0x01 && (*f)[5] == 0x00;
    ESP_LOGI(TAG, "brew 0x%02X: %s", drink, f ? (ok ? "accepted" : "rejected") : "no answer");
  }, true);
  return true;
}

void Ecam::stop() {
  if (!this->last_drink_) {
    ESP_LOGW(TAG, "stop: no drink started from here");
    return;
  }
  this->enqueue_({0x83, 0xF0, this->last_drink_, 0x02, 0x06}, 3, 800, [](const std::vector<uint8_t> *f) {
    ESP_LOGI(TAG, "stop: %s", f ? "ok" : "no answer");
  }, true);
}

void Ecam::read_param(uint16_t pid, std::function<void(uint32_t)> cb) {
  this->enqueue_({0x95, 0x0F, uint8_t(pid >> 8), uint8_t(pid & 0xFF), 0x01}, 2, 500,
                 [cb](const std::vector<uint8_t> *f) {
                   if (f && f->size() >= 12)
                     cb((uint32_t((*f)[6]) << 24) | (uint32_t((*f)[7]) << 16) | (uint32_t((*f)[8]) << 8) | (*f)[9]);
                 });
}

void Ecam::raw(const std::string &hex) {
  std::vector<uint8_t> p;
  int hi = -1;
  for (char ch : hex) {
    int v = (ch >= '0' && ch <= '9') ? ch - '0' : (ch >= 'a' && ch <= 'f') ? ch - 'a' + 10
          : (ch >= 'A' && ch <= 'F') ? ch - 'A' + 10 : -1;
    if (v < 0)
      continue;
    if (hi < 0) {
      hi = v;
    } else {
      p.push_back(uint8_t(hi << 4 | v));
      hi = -1;
    }
  }
  if (p.empty() || hi >= 0) {
    ESP_LOGW(TAG, "raw: bad hex '%s'", hex.c_str());
    return;
  }
  std::string tx = format_hex(p);
  this->enqueue_(std::move(p), 1, 800, [tx](const std::vector<uint8_t> *f) {
    ESP_LOGI(TAG, "raw %s -> %s", tx.c_str(), f ? format_hex(*f).c_str() : "no answer");
  });
}

std::string Ecam::state_text() const {
  if (!this->status_valid_)
    return "offline";
  switch (this->status_.state) {
    case 0x00: return "standby";
    case 0x01: return "heating";
    case 0x02: return "turning off";
    case 0x04: return "descaling";
    case 0x05: return "steam";
    case 0x06: return "recovery";
    case 0x07: return this->status_.phase ? "brewing" : "ready";
    case 0x08: return "rinsing";
    case 0x0A: return "milk";
    case 0x0B: return "hot water";
    case 0x0C: return "milk clean";
    case 0x10: return "chocolate";
    default: return "state " + to_string(this->status_.state);
  }
}

std::string Ecam::accessory_text() const {
  switch (this->status_.accessory) {
    case 0x00: return "none";
    case 0x01: return "hot water spout";
    case 0x02: return "milk jug";
    case 0x04: return "milk jug (clean)";
    case 0x06: return "cold milk jug";
    default: return "0x" + format_hex(this->status_.accessory);
  }
}

std::string Ecam::alarms_text() const {
  static const char *const NAMES[16] = {
      "tank empty",     "grounds full",       "descale",       "replace filter",
      "grind too fine", "beans empty",        "service",       "heater probe",
      "too much coffee", "brew unit motor",   "steamer probe", "empty drip tray",
      "hydraulic problem", "insert tank",     "clean knob",    "beans empty"};
  if (!this->status_valid_)
    return "";
  std::string out;
  for (int i = 0; i < 16; i++) {
    if (this->status_.alarms & (1 << i)) {
      if (!out.empty())
        out += ", ";
      out += NAMES[i];
    }
  }
  if ((this->status_.flags & 0x10) && (this->status_.alarms & ((1 << 5) | (1 << 15))) == 0)
    out += out.empty() ? "beans empty" : ", beans empty";
  return out.empty() ? "none" : out;
}

}  // namespace ecam
}  // namespace esphome

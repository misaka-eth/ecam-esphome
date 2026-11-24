#include "ecam.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <strings.h>
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

// Newer ESPHome removed the std::string-returning format_hex(); local replacement.
static std::string format_hex(const uint8_t *data, size_t len) {
  static const char *const DIGITS = "0123456789abcdef";
  std::string s;
  s.reserve(len * 2);
  for (size_t i = 0; i < len; i++) {
    s += DIGITS[data[i] >> 4];
    s += DIGITS[data[i] & 0x0F];
  }
  return s;
}
static std::string format_hex(const std::vector<uint8_t> &v) { return format_hex(v.data(), v.size()); }
static std::string format_hex(uint8_t b) { return format_hex(&b, 1); }

// Request: 0d <len> <payload> <crc16>; answer: d0 <len> <payload> <crc16>; len = total - 1.
static const uint8_t REQ = 0x0D;
static const uint8_t ANS = 0xD0;
static const uint32_t POLL_MS = 1000;
static const uint32_t WIFI_MS = 5000;
static const uint32_t STALE_MS = 5000;
static const uint32_t SCAN_MS = 100;

uint16_t Ecam::crc_(const uint8_t *data, size_t len) {
  uint16_t c = 0x1D0F;
  for (size_t i = 0; i < len; i++) {
    c ^= uint16_t(data[i]) << 8;
    for (int b = 0; b < 8; b++)
      c = (c & 0x8000) ? uint16_t((c << 1) ^ 0x1021) : uint16_t(c << 1);
  }
  return c;
}

void Ecam::setup() { this->rx_.reserve(256); }

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
      // Profile and custom-drink lists run to ~140 bytes; LEN is one byte, so a frame is at most 256.
      if (this->rx_[1] < 4) {
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
    else if (this->scan_left_ && now - this->scan_at_ >= SCAN_MS)
      this->scan_step_();  // one read per gap between polls, so the monitor never goes stale
  }
  if (this->status_valid_ && now - this->status_at_ > STALE_MS) {
    this->status_valid_ = false;
    for (auto &cb : this->status_cbs_)
      cb();
#ifdef USE_SENSOR
    for (auto &c : this->counters_)
      c.second->publish_state(NAN);
#endif
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
    this->status_.alarms = f[7] | (uint32_t(f[8]) << 8) | (uint32_t(f[12]) << 16) | (uint32_t(f[13]) << 24);
    this->status_.state = f[9];
    this->status_.phase = f[10];
    this->status_.progress = f[11];
    this->status_.result = f[14];
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

// Recipe TLVs: tags 01, 09, 0f carry a u16, the rest a u8.
static bool tag16(uint8_t t) { return t == 0x01 || t == 0x09 || t == 0x0F; }

static std::vector<std::pair<uint8_t, uint16_t>> parse_tlv(const uint8_t *p, size_t n) {
  std::vector<std::pair<uint8_t, uint16_t>> out;
  for (size_t i = 0; i < n;) {
    uint8_t t = p[i];
    size_t w = tag16(t) ? 2 : 1;
    if (i + 1 + w > n)
      break;
    out.push_back({t, w == 2 ? uint16_t((p[i + 1] << 8) | p[i + 2]) : p[i + 1]});
    i += 1 + w;
  }
  return out;
}

static void put_tlv(std::vector<uint8_t> &p, uint8_t t, uint16_t v) {
  p.push_back(t);
  if (tag16(t))
    p.push_back(v >> 8);
  p.push_back(v & 0xFF);
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
  std::map<uint8_t, uint16_t> over;
  if (coffee_ml > 0) over[0x01] = coffee_ml;
  if (strength > 0) over[0x02] = strength;
  if (water_ml > 0) over[0x0F] = water_ml;
  if (milk_s > 0) over[0x09] = int(milk_s * 10 + 0.5f);
  if (temp >= 0 && temp <= 3) {
    // The app pairs 0d (water temp 0..3) with 1b 01 on max and 1b 04 otherwise.
    over[0x1B] = temp == 3 ? 0x01 : 0x04;
    over[0x0D] = temp;
  }
  return this->brew_with(drink, std::move(over));
}

bool Ecam::brew_with(int drink, std::map<uint8_t, uint16_t> over) {
  if (drink <= 0 || drink > 0xFF) {
    ESP_LOGW(TAG, "brew: bad drink id %d", drink);
    return false;
  }
  if (!this->idle() || this->status_.state != 0x07) {
    ESP_LOGW(TAG, "brew refused: machine not ready");
    return false;
  }
  // Like the app, send the stored recipe with the overrides applied. A milk drink with parameters
  // stops at the milk ("clean knob", then "steamer probe", monitor byte 10 = 01) unless it carries the
  // recipe's accessory tag and leaves out froth (0b): the froth knob is mechanical and no recipe of the
  // ECAM450.76 lists it. Without any parameters the machine uses its stored recipe and that works too.
  over.erase(0x0B);
  if (over.empty()) {
    this->brew_send_(uint8_t(drink), {0x83, 0xF0, uint8_t(drink), 0x03, this->tail_(2)});
    return true;
  }
  uint8_t prof = this->active_profile_ ? this->active_profile_ : 1;
  this->enqueue_({0xA6, 0xF0, prof, uint8_t(drink)}, 2, 500, [this, drink, over](const std::vector<uint8_t> *f) {
    // 83 f0 <drink> 03 <tlv...> <tail>; the app sends tags below 0x17 plus 1b, 1c, 1f, 21, 26, 27.
    if (!f || f->size() < 8) {
      // Overrides alone would stop a milk drink halfway; better not to start.
      ESP_LOGW(TAG, "brew 0x%02X: recipe read failed, not started", drink);
      char text[8];
      snprintf(text, sizeof(text), "%02x ", drink);
      this->last_brew_ = std::string(text) + "failed recipe read";
      this->brew_answers_++;
      return;
    }
    std::vector<uint8_t> p{0x83, 0xF0, uint8_t(drink), 0x03};
    auto left = over;
    {
      for (auto &tv : parse_tlv(f->data() + 6, f->size() - 8)) {
        uint8_t t = tv.first;
        if (t == 0x0B || !(t < 0x17 || t == 0x1B || t == 0x1C || t == 0x1F || t == 0x21 || t == 0x26 || t == 0x27))
          continue;
        auto it = left.find(t);
        put_tlv(p, t, it != left.end() ? it->second : tv.second);
        if (it != left.end())
          left.erase(it);
      }
    }
    for (auto &tv : left)
      put_tlv(p, tv.first, tv.second);
    p.push_back(this->tail_(2));
    this->brew_send_(uint8_t(drink), std::move(p));
  }, true);
  return true;
}

bool Ecam::brew_recipe_edits() {
  if (!this->recipe_drink_) {
    ESP_LOGW(TAG, "brew: no recipe shown");
    return false;
  }
  auto over = this->recipe_edits_;
  // As in brew(): the app pairs a water temperature with 1b 01 on max and 1b 04 otherwise.
  auto t = over.find(0x0D);
  if (t != over.end())
    over[0x1B] = t->second == 3 ? 0x01 : 0x04;
  return this->brew_with(this->recipe_drink_, std::move(over));
}

void Ecam::brew_send_(uint8_t drink, std::vector<uint8_t> p) {
  this->last_drink_ = uint8_t(drink);
  // Retrying a brew could start it twice: one attempt, generous timeout.
  this->enqueue_(std::move(p), 1, 1500, [this, drink](const std::vector<uint8_t> *f) {
    bool ok = f && f->size() >= 6 && (*f)[4] == 0x01 && (*f)[5] == 0x00;
    const char *result = f ? (ok ? "accepted" : "rejected") : "no answer";
    ESP_LOGI(TAG, "brew 0x%02X: %s -> %s", drink, result, f ? format_hex(*f).c_str() : "no answer");
    // d0 <len> 83 f0 <answer...> <crc16>: the answer bytes say why a brew was refused.
    char text[8];
    snprintf(text, sizeof(text), "%02x ", drink);
    std::string out = std::string(text) + result;
    for (size_t i = 4; f && i + 2 < f->size(); i++) {
      snprintf(text, sizeof(text), " %02x", (*f)[i]);
      out += text;
    }
    this->last_brew_ = out;
    this->brew_answers_++;
  }, true);
}

void Ecam::stop() {
  if (!this->last_drink_) {
    ESP_LOGW(TAG, "stop: no drink started from here");
    return;
  }
  this->enqueue_({0x83, 0xF0, this->last_drink_, 0x02, this->tail_(2)}, 3, 800, [](const std::vector<uint8_t> *f) {
    ESP_LOGI(TAG, "stop: %s", f ? "ok" : "no answer");
  }, true);
}

static uint32_t be32(const uint8_t *p) {
  return (uint32_t(p[0]) << 24) | (uint32_t(p[1]) << 16) | (uint32_t(p[2]) << 8) | p[3];
}

void Ecam::read_param(uint16_t pid, std::function<void(uint32_t)> cb) {
  this->enqueue_({0x95, 0x0F, uint8_t(pid >> 8), uint8_t(pid & 0xFF), 0x01}, 2, 500,
                 [this, pid, cb](const std::vector<uint8_t> *f) {
                   if (!f || f->size() < 12)
                     return;
                   uint32_t v = be32(f->data() + 6);
                   this->params_[pid] = v;
                   if (cb)
                     cb(v);
                 });
}

void Ecam::write_param(uint16_t pid, uint32_t value) {
  this->enqueue_({0x90, 0x0F, uint8_t(pid >> 8), uint8_t(pid & 0xFF), uint8_t(value >> 24), uint8_t(value >> 16),
                  uint8_t(value >> 8), uint8_t(value)},
                 2, 800, [this, pid, value](const std::vector<uint8_t> *f) {
                   bool ok = f && f->size() >= 9 && (*f)[6] == 0x00;
                   ESP_LOGI(TAG, "write PAR %u = %u: %s", pid, (unsigned) value, f ? (ok ? "ok" : "rejected") : "no answer");
                   this->read_param(pid);
                 }, true);
}

void Ecam::write_param_bits(uint16_t pid, uint32_t mask, bool on) {
  // Read first: the word holds other settings, and a stale cache would overwrite them.
  this->read_param(pid, [this, pid, mask, on](uint32_t cur) {
    uint32_t v = on ? (cur | mask) : (cur & ~mask);
    if (v != cur)
      this->write_param(pid, v);
  });
}

optional<uint32_t> Ecam::param(uint16_t pid) const {
  if (!this->status_valid_)
    return {};
  auto it = this->params_.find(pid);
  if (it == this->params_.end())
    return {};
  return it->second;
}

optional<bool> Ecam::param_bit(uint16_t pid, uint32_t mask) const {
  auto v = this->param(pid);
  if (!v)
    return {};
  return (*v & mask) != 0;
}

optional<uint32_t> Ecam::stat(uint16_t id) const {
  auto it = this->stats_.find(id);
  if (!this->status_valid_ || it == this->stats_.end())
    return {};
  return it->second;
}

// ---- text helpers: names are 20 bytes UTF-16BE, zero padded (10 characters, BMP only).

static std::string utf16be_to_utf8(const uint8_t *p, size_t n) {
  std::string out;
  for (size_t i = 0; i + 1 < n; i += 2) {
    uint16_t c = (uint16_t(p[i]) << 8) | p[i + 1];
    if (c == 0)
      break;
    if (c < 0x80) {
      out += char(c);
    } else if (c < 0x800) {
      out += char(0xC0 | (c >> 6));
      out += char(0x80 | (c & 0x3F));
    } else {
      out += char(0xE0 | (c >> 12));
      out += char(0x80 | ((c >> 6) & 0x3F));
      out += char(0x80 | (c & 0x3F));
    }
  }
  return out;
}

// UTF-8 -> UTF-16BE, cut and NUL-padded to `bytes`.
static std::vector<uint8_t> utf8_to_utf16be(const std::string &s, size_t bytes) {
  std::vector<uint8_t> out;
  for (size_t i = 0; i < s.size() && out.size() < bytes;) {
    uint8_t b = s[i];
    uint32_t c;
    size_t n;
    if (b < 0x80) { c = b; n = 1; }
    else if ((b & 0xE0) == 0xC0) { c = b & 0x1F; n = 2; }
    else if ((b & 0xF0) == 0xE0) { c = b & 0x0F; n = 3; }
    else { c = '?'; n = (b & 0xF8) == 0xF0 ? 4 : 1; }  // outside the BMP
    for (size_t k = 1; k < n && i + k < s.size(); k++)
      c = (c << 6) | (uint8_t(s[i + k]) & 0x3F);
    if (n == 4)
      c = '?';
    i += n;
    out.push_back(uint8_t(c >> 8));
    out.push_back(uint8_t(c));
  }
  out.resize(bytes, 0);
  return out;
}

// ---- refresh

void Ecam::refresh(bool device_info) {
  // Settings: 50 water hardness, 61 coffee temperature, 62 auto-off, 63 flags, 64/65 autostart time.
  for (uint16_t pid : {50, 61, 62, 63, 64, 65})
    this->read_param(pid);
  if (device_info) {
    // 199 EEPROM version, 200-203 production date, 205-209 serial number, 210 PIN.
    for (uint16_t pid : {199, 200, 201, 202, 203, 205, 206, 207, 208, 209, 210})
      this->read_param(pid);
    for (uint8_t n = 1; n <= 6; n++)
      this->read_bean_(n);
  }
  // Active profile: answer a9 f0 .. <slot>, the last byte before the CRC.
  this->enqueue_({0xA9, 0xF0}, 2, 500, [this](const std::vector<uint8_t> *f) {
    if (!f || f->size() < 7)
      return;
    uint8_t slot = (*f)[f->size() - 3];
    if (slot >= 1 && slot <= 4)
      this->active_profile_ = slot;
    this->read_favorites_();
  });
  // Profile names: a4 f0 01 04 -> 4 x (name 20 B, icon, mug) from byte 6.
  this->enqueue_({0xA4, 0xF0, 0x01, 0x04}, 2, 500, [this](const std::vector<uint8_t> *f) {
    if (!f || f->size() < 6 + 4 * 22 + 2)
      return;
    for (int i = 0; i < 4; i++)
      this->profile_names_[i] = utf16be_to_utf8(f->data() + 6 + i * 22, 20);
    this->profiles_valid_ = true;
  });
  // Custom drink names: aa f0 01 06 -> 6 x (name 20 B, image) from byte 6.
  this->enqueue_({0xAA, 0xF0, 0x01, 0x06}, 2, 500, [this](const std::vector<uint8_t> *f) {
    if (!f || f->size() < 6 + 6 * 21 + 2)
      return;
    for (int i = 0; i < 6; i++)
      this->custom_names_[i] = utf16be_to_utf8(f->data() + 6 + i * 21, 20);
    this->customs_valid_ = true;
  });
  this->read_stats_(0, 0);
#ifdef USE_SENSOR
  this->read_counters_();
#endif
}

#ifdef USE_SENSOR
// The counter sensors read the live parameters: 95 0f <id u16> <n> answers n consecutive u32 values from
// byte 6 (up to 48 tried). A2 only catches up when the machine goes to standby.
void Ecam::read_counters_() {
  static const uint16_t BLOCK = 48;
  std::sort(this->counters_.begin(), this->counters_.end(),
            [](const auto &a, const auto &b) { return a.first < b.first; });
  for (size_t i = 0; i < this->counters_.size();) {
    uint16_t first = this->counters_[i].first;
    size_t end = i;
    while (end < this->counters_.size() && this->counters_[end].first < first + BLOCK)
      end++;
    uint16_t n = this->counters_[end - 1].first - first + 1;
    this->enqueue_({0x95, 0x0F, uint8_t(first >> 8), uint8_t(first & 0xFF), uint8_t(n)}, 2, 500,
                   [this, i, end, first, n](const std::vector<uint8_t> *f) {
                     if (!f || f->size() < 6u + 4u * n + 2u)
                       return;
                     for (size_t k = i; k < end; k++)
                       this->counters_[k].second->publish_state(
                           float(be32(f->data() + 6 + 4 * (this->counters_[k].first - first))));
                   });
    i = end;
  }
}
#endif

// a2 0f <start u16> 0a -> up to 10 x (id u16, value u32) from byte 4, ids >= start.
void Ecam::read_stats_(uint16_t start, int pages) {
  if (start == 0 && pages == 0)
    this->stats_.clear();
  this->enqueue_({0xA2, 0x0F, uint8_t(start >> 8), uint8_t(start & 0xFF), 0x0A}, 2, 500,
                 [this, start, pages](const std::vector<uint8_t> *f) {
                   int last = -1;
                   for (size_t i = 4; f && i + 6 <= f->size() - 2; i += 6) {
                     uint16_t id = (uint16_t((*f)[i]) << 8) | (*f)[i + 1];
                     this->stats_[id] = be32(f->data() + i + 2);
                     last = id;
                   }
                   // More pages while the ids keep growing; the cap guards against a machine that repeats.
                   if (last >= start && last < 0xFFFF && pages < 40) {
                     this->read_stats_(last + 1, pages + 1);
                     return;
                   }
                   ESP_LOGD(TAG, "statistics: %u counters", (unsigned) this->stats_.size());
                 });
}

// ---- profiles

void Ecam::select_profile(uint8_t slot) {
  if (slot < 1 || slot > 4)
    return;
  this->enqueue_({0xA9, 0xF0, slot}, 2, 800, [this, slot](const std::vector<uint8_t> *f) {
    ESP_LOGI(TAG, "select profile %u: %s", slot, f ? "ok" : "no answer");
    if (f)
      this->active_profile_ = slot;
    this->read_favorites_();
  }, true);
}

optional<std::string> Ecam::profile_name(uint8_t slot) const {
  if (!this->status_valid_ || !this->profiles_valid_ || slot < 1 || slot > 4)
    return {};
  return this->profile_names_[slot - 1];
}

void Ecam::rename_profile(uint8_t slot, const std::string &name) {
  if (slot < 1 || slot > 4)
    return;
  // a5 f0 <slot> <slot> <name 20 B> <icon> <mug> keeps icon and mug: read the profiles first (a4 f0 01 04 ->
  // 4 x (name 20 B, icon, mug) from byte 6; the machine does not answer a read of a single profile).
  this->enqueue_({0xA4, 0xF0, 0x01, 0x04}, 2, 500, [this, slot, name](const std::vector<uint8_t> *f) {
    if (!f || f->size() < 6 + 4 * 22 + 2) {
      ESP_LOGW(TAG, "rename profile %u: read failed, nothing written", slot);
      return;
    }
    const uint8_t *entry = f->data() + 6 + (slot - 1) * 22;
    // An unset icon reads ff, but the machine rejects ff in a write (answer 01); it stores 1 as 2.
    uint8_t icon = entry[20] == 0xFF ? 0x02 : entry[20], mug = entry[21];
    std::vector<uint8_t> p{0xA5, 0xF0, slot, slot};
    auto n = utf8_to_utf16be(name, 20);
    p.insert(p.end(), n.begin(), n.end());
    p.push_back(icon);
    p.push_back(mug);
    this->enqueue_(std::move(p), 2, 800, [this, slot](const std::vector<uint8_t> *f) {
      ESP_LOGI(TAG, "rename profile %u: %s", slot, f ? ((*f)[4] == 0 ? "ok" : "rejected") : "no answer");
      this->refresh(false);
    }, true);
  }, true);
}

// ---- favourites (ac f0 <profile> -> d0 12 ac f0 <profile> <11 ids, newest first, 00 = empty>)

void Ecam::read_favorites_() {
  if (!this->active_profile_)
    return;
  this->enqueue_({0xAC, 0xF0, this->active_profile_}, 2, 500, [this](const std::vector<uint8_t> *f) {
    if (!f || f->size() < 16 + 2)
      return;
    this->favorites_.clear();
    for (size_t i = 5; i < 16; i++)
      if ((*f)[i])
        this->favorites_.push_back((*f)[i]);
    this->favorites_valid_ = true;
  });
}

optional<std::string> Ecam::favorites_text() const {
  if (!this->status_valid_ || !this->favorites_valid_)
    return {};
  std::string out;
  for (uint8_t id : this->favorites_) {
    if (!out.empty())
      out += ", ";
    out += drink_name(id);
  }
  return out.empty() ? std::string("none") : out;
}

void Ecam::set_favorites(const std::string &drinks) {
  if (!this->active_profile_) {
    ESP_LOGW(TAG, "favourites: active profile not known yet");
    return;
  }
  // Comma-separated names or hex ids, newest first; the app always writes all 11 entries, 00 = empty.
  std::vector<uint8_t> p{0xAD, 0xF0, this->active_profile_};
  size_t pos = 0;
  while (pos <= drinks.size()) {
    size_t end = drinks.find(',', pos);
    if (end == std::string::npos)
      end = drinks.size();
    std::string item = drinks.substr(pos, end - pos);
    item.erase(0, item.find_first_not_of(' '));
    item.erase(item.find_last_not_of(' ') + 1);
    pos = end + 1;
    if (item.empty())
      continue;
    int id = drink_id(item);
    if (id <= 0) {
      ESP_LOGW(TAG, "favourites: unknown drink '%s', nothing written", item.c_str());
      return;
    }
    if (p.size() == 3 + 11) {
      ESP_LOGW(TAG, "favourites: at most 11, nothing written");
      return;
    }
    p.push_back(uint8_t(id));
  }
  p.resize(3 + 11, 0);
  this->enqueue_(std::move(p), 2, 800, [this](const std::vector<uint8_t> *f) {
    ESP_LOGI(TAG, "favourites: %s", f ? "ok" : "no answer");
    this->read_favorites_();
  }, true);
}

// ---- custom drinks (slots 1..6 = drinks e6..eb)

optional<std::string> Ecam::custom_drinks_text() const {
  if (!this->status_valid_ || !this->customs_valid_)
    return {};
  std::string out;
  for (int i = 0; i < 6; i++) {
    if (this->custom_names_[i].empty())
      continue;
    if (!out.empty())
      out += ", ";
    out += to_string(i + 1) + " " + this->custom_names_[i];
  }
  return out.empty() ? std::string("none") : out;
}

// ---- Bean Adapt slots (app: BeanSystem). ba f0 n -> n, name 20 x UTF-16BE, grind (half steps),
// temperature, aroma, 00, set-up flag, enabled; bb f0 n <same, without the last byte> writes a slot.

static const char *const BEAN_TEMPERATURES[] = {"low", "mid", "high", "very high"};
static const char *const BEAN_AROMAS[] = {"pre-ground", "extra mild", "mild", "normal", "strong", "extra strong"};

template<size_t N> static optional<uint8_t> index_of(const char *const (&names)[N], const std::string &s) {
  for (size_t i = 0; i < N; i++)
    if (s == names[i])
      return i;
  return {};
}

void Ecam::read_bean_(uint8_t slot) {
  this->enqueue_({0xBA, 0xF0, slot}, 2, 500, [this, slot](const std::vector<uint8_t> *f) {
    if (!f || f->size() < 5 + 40 + 6 + 2 || (*f)[4] != slot)
      return;
    Bean &b = this->beans_[slot - 1];
    b.name = utf16be_to_utf8(f->data() + 5, 40);
    b.grind = (*f)[45] == 13 ? 14 : (*f)[45];  // as the app: 13 reads as 7.0
    b.temperature = (*f)[46];
    b.aroma = (*f)[47];
    b.used = (*f)[49] == 1;
    this->beans_read_ |= 1 << (slot - 1);
    if (slot == this->bean_slot_)
      this->edit_bean(slot);
  });
}

optional<std::string> Ecam::bean_slots_text() const {
  if (this->beans_read_ != 0x3F)
    return {};
  std::string out;
  char buf[64];
  for (int i = 0; i < 6; i++) {
    const Bean &b = this->beans_[i];
    if (!b.used)
      continue;
    snprintf(buf, sizeof(buf), " (grind %.1f, %s, %s)", b.grind / 2.0f,
             b.temperature < 4 ? BEAN_TEMPERATURES[b.temperature] : "?", b.aroma < 6 ? BEAN_AROMAS[b.aroma] : "?");
    if (!out.empty())
      out += ", ";
    out += to_string(i + 1) + " " + b.name + buf;
  }
  return out.empty() ? std::string("none") : out;
}

void Ecam::edit_bean(uint8_t slot) {
  if (slot < 1 || slot > 6)
    return;
  this->bean_slot_ = slot;
  this->bean_edit_ = this->beans_[slot - 1];
  if (!this->bean_edit_.used) {  // a new bean starts from the app's defaults
    this->bean_edit_.grind = 8;
    this->bean_edit_.temperature = 1;
    this->bean_edit_.aroma = 3;
  }
}

optional<std::string> Ecam::bean_name() const {
  if (!(this->beans_read_ & (1 << (this->bean_slot_ - 1))))
    return {};
  return this->bean_edit_.name;
}

optional<float> Ecam::bean_grind() const {
  if (!(this->beans_read_ & (1 << (this->bean_slot_ - 1))))
    return {};
  return this->bean_edit_.grind / 2.0f;
}

optional<std::string> Ecam::bean_temperature() const {
  if (!(this->beans_read_ & (1 << (this->bean_slot_ - 1))) || this->bean_edit_.temperature >= 4)
    return {};
  return std::string(BEAN_TEMPERATURES[this->bean_edit_.temperature]);
}

optional<std::string> Ecam::bean_aroma() const {
  if (!(this->beans_read_ & (1 << (this->bean_slot_ - 1))) || this->bean_edit_.aroma >= 6)
    return {};
  return std::string(BEAN_AROMAS[this->bean_edit_.aroma]);
}

void Ecam::set_bean_grind(float grind) {
  int half = (int) lroundf(grind * 2);
  this->bean_edit_.grind = (uint8_t) std::min(14, std::max(2, half));
}

void Ecam::set_bean_temperature(const std::string &name) {
  if (auto i = index_of(BEAN_TEMPERATURES, name))
    this->bean_edit_.temperature = *i;
}

void Ecam::set_bean_aroma(const std::string &name) {
  if (auto i = index_of(BEAN_AROMAS, name))
    this->bean_edit_.aroma = *i;
}

void Ecam::write_bean_(uint8_t slot, const Bean &b) {
  std::vector<uint8_t> p{0xBB, 0xF0, slot};
  auto n = utf8_to_utf16be(b.name, 40);
  p.insert(p.end(), n.begin(), n.end());
  p.insert(p.end(), {b.grind, b.temperature, b.aroma, 0x00, (uint8_t) (b.used ? 1 : 0)});
  std::string tx = format_hex(p);
  this->enqueue_(std::move(p), 2, 1500, [this, slot, tx](const std::vector<uint8_t> *f) {
    ESP_LOGI(TAG, "bean slot %u: %s -> %s", slot, tx.c_str(), f ? format_hex(*f).c_str() : "no answer");
    this->read_bean_(slot);
  }, true);
}

void Ecam::save_bean() {
  Bean b = this->bean_edit_;
  if (b.name.empty())
    b.name = "Bean " + to_string(this->bean_slot_);
  b.used = true;
  this->write_bean_(this->bean_slot_, b);
}

void Ecam::delete_bean() { this->write_bean_(this->bean_slot_, Bean{}); }


void Ecam::delete_custom_drink(uint8_t slot) {
  if (slot < 1 || slot > 6 || !this->active_profile_)
    return;
  uint8_t bev = 0xE5 + slot;
  // The app deletes by re-sending the stored recipe with 0b ff, 27 00 and tail 04 (save is 05).
  this->enqueue_({0xA6, 0xF0, this->active_profile_, bev}, 2, 500, [this, bev](const std::vector<uint8_t> *f) {
    if (!f || f->size() < 8) {
      ESP_LOGW(TAG, "delete custom drink: read failed, nothing sent");
      return;
    }
    std::vector<uint8_t> p{0x83, 0xF0, bev, 0x00};
    for (auto &tv : parse_tlv(f->data() + 6, f->size() - 8))
      if (tv.first != 0x0B && tv.first != 0x19 && tv.first != 0x27)
        put_tlv(p, tv.first, tv.second);
    p.insert(p.end(), {0x0B, 0xFF, 0x27, 0x00, this->tail_(0)});
    this->enqueue_(std::move(p), 1, 1500, [this, bev](const std::vector<uint8_t> *f) {
      bool ok = f && f->size() >= 6 && (*f)[4] == 0x00 && (*f)[5] == 0x01;
      ESP_LOGI(TAG, "delete custom drink %02X: %s", bev, f ? (ok ? "ok" : "rejected") : "no answer");
      this->refresh(false);
    }, true);
  }, true);
}

// ---- recipes

// Recipe tags as the app names them (enum TEMP(0) .. RINSE(39)); 08 is not in the app's enum.
static const char *tag_name(uint8_t t) {
  switch (t) {
    case 0x00: return "temp";
    case 0x01: return "coffee_ml";
    case 0x02: return "strength";
    case 0x03: return "grind";
    case 0x04: return "blend";
    case 0x05: return "infusion_speed";
    case 0x06: return "preinfusion";
    case 0x07: return "crema";
    case 0x08: return "2x";
    case 0x09: return "milk_0.1s";
    case 0x0A: return "milk_temp";
    case 0x0B: return "froth";
    case 0x0C: return "inversion";
    case 0x0D: return "water_temp";
    case 0x0E: return "tea_profile";
    case 0x0F: return "water_ml";
    case 0x18: return "programmable";
    case 0x19: return "visible";
    case 0x1A: return "visible_in_programming";
    case 0x1B: return "index_length";
    case 0x1C: return "accessory";
    case 0x1F: return "iced";
    case 0x20: return "mug_size";
    case 0x21: return "mug_adjust";
    case 0x25: return "ice_cubes";
    case 0x26: return "intensity";
    case 0x27: return "rinse";
    default: return nullptr;
  }
}

void Ecam::read_recipe(int drink, int profile) {
  uint8_t prof = profile > 0 ? uint8_t(profile) : this->active_profile_;
  if (drink <= 0 || drink > 0xFF || prof < 1 || prof > 4) {
    ESP_LOGW(TAG, "recipe: bad drink or profile not known yet");
    return;
  }
  this->enqueue_({0xA6, 0xF0, prof, uint8_t(drink)}, 2, 500, [this, prof, drink](const std::vector<uint8_t> *f) {
    std::string out = "profile " + to_string(prof) + " " + drink_name(drink) + ":";
    this->recipe_drink_ = uint8_t(drink);
    this->recipe_profile_ = prof;
    this->recipe_tlv_.clear();
    this->recipe_edits_.clear();
    if (!f || f->size() < 8) {
      out += " no answer";
    } else {
      this->recipe_tlv_ = parse_tlv(f->data() + 6, f->size() - 8);
      for (auto &tv : this->recipe_tlv_) {
        const char *n = tag_name(tv.first);
        out += " ";
        out += n ? n : format_hex(tv.first);
        out += "=" + to_string(tv.second) + ",";
      }
      if (out.back() == ',')
        out.pop_back();
    }
    ESP_LOGI(TAG, "recipe %s", out.c_str());
    this->recipe_text_ = out;
  });
  this->read_limits_(uint8_t(drink));
}

// b0 f0 <drink> -> from byte 5: (tag, min, default, max), u16 values for the u16 tags (as the app decodes it).
void Ecam::read_limits_(uint8_t drink) {
  this->recipe_limits_.clear();
  this->enqueue_({0xB0, 0xF0, drink}, 2, 500, [this, drink](const std::vector<uint8_t> *f) {
    if (!f || f->size() < 8 || (*f)[4] != drink)
      return;
    std::map<uint8_t, Limit> limits;
    size_t i = 5, end = f->size() - 2;
    while (i < end) {
      uint8_t t = (*f)[i];
      size_t w = tag16(t) ? 2 : 1;
      if (i + 1 + 3 * w > end) {
        ESP_LOGW(TAG, "limits %02X: odd length, ignored: %s", drink, format_hex(*f).c_str());
        return;
      }
      auto rd = [&](size_t k) -> uint16_t {
        return w == 2 ? uint16_t(((*f)[k] << 8) | (*f)[k + 1]) : (*f)[k];
      };
      limits[t] = {rd(i + 1), rd(i + 1 + w), rd(i + 1 + 2 * w)};
      i += 1 + 3 * w;
    }
    this->recipe_limits_ = std::move(limits);
    ESP_LOGD(TAG, "limits %02X: %s", drink, this->recipe_limits_text().c_str());
  });
}

void Ecam::set_recipe_value(uint8_t tag, uint16_t value) {
  auto l = this->recipe_limits_.find(tag);
  if (l != this->recipe_limits_.end()) {
    if (value < l->second.min)
      value = l->second.min;
    if (value > l->second.max)
      value = l->second.max;
  }
  this->recipe_edits_[tag] = value;
}

std::string Ecam::recipe_limits_text() const {
  std::string out;
  for (auto &tv : this->recipe_tlv_) {
    auto l = this->recipe_limits_.find(tv.first);
    const char *n = tag_name(tv.first);
    if (l == this->recipe_limits_.end() || !n || l->second.min == l->second.max)
      continue;
    if (!out.empty())
      out += ", ";
    out += std::string(n) + " " + to_string(l->second.min) + "-" + to_string(l->second.max);
  }
  return out;
}

void Ecam::set_clock(uint8_t hour, uint8_t minute) {
  // Machines without a clock keep alarm bit 26 set and refuse or ignore E2: stop trying until reboot.
  if (hour > 23 || minute > 59 || !this->clock_supported_ || (this->status_.alarms & (1u << 26)))
    return;
  this->enqueue_({0xE2, 0xF0, hour, minute}, 2, 800, [this, hour, minute](const std::vector<uint8_t> *f) {
    bool ok = f && f->size() > 4 && (*f)[4] == 0;
    ESP_LOGI(TAG, "clock %02u:%02u: %s", hour, minute, ok ? "ok" : f ? "rejected" : "no answer");
    if (!ok) {
      this->clock_supported_ = false;
      ESP_LOGW(TAG, "clock: not supported by this machine, no more clock syncs");
    }
  });
}

void Ecam::scan_params(uint16_t start, uint16_t count) {
  if (!this->scan_done()) {
    ESP_LOGW(TAG, "scan: already running");
    return;
  }
  this->scan_.clear();
  this->scan_answered_ = 0;
  this->scan_next_ = start;
  this->scan_left_ = count;
  ESP_LOGI(TAG, "scan PAR %u..%u", start, start + count - 1);
}

// Called from loop() only when nothing else is queued.
void Ecam::scan_step_() {
  this->scan_at_ = millis();
  uint16_t pid = this->scan_next_++;
  this->scan_left_--;
  this->enqueue_({0x95, 0x0F, uint8_t(pid >> 8), uint8_t(pid & 0xFF), 0x01}, 1, 300,
                 [this, pid](const std::vector<uint8_t> *f) {
                   if (f && f->size() >= 12) {
                     this->scan_answered_++;
                     uint32_t v = be32(f->data() + 6);
                     if (v)
                       this->scan_[pid] = v;
                   }
                   if (this->scan_left_ == 0)
                     ESP_LOGI(TAG, "scan done: %u answered, %u non-zero", this->scan_answered_,
                              (unsigned) this->scan_.size());
                 });
}

void Ecam::skip_rinse() {
  if (!this->last_drink_) {
    ESP_LOGW(TAG, "skip rinse: no drink started from here");
    return;
  }
  this->enqueue_({0x83, 0xF0, this->last_drink_, 0x06, 0x27, 0x00, this->tail_(2)}, 1, 800,
                 [](const std::vector<uint8_t> *f) {
                   ESP_LOGI(TAG, "skip rinse -> %s", f ? format_hex(*f).c_str() : "no answer");
                 }, true);
}

void Ecam::dump_counters() const {
  ESP_LOGI(TAG, "%u counters:", (unsigned) this->stats_.size());
  for (auto &kv : this->stats_)
    ESP_LOGI(TAG, "  %u = %u", kv.first, (unsigned) kv.second);
}

void Ecam::save_recipe(int drink, int coffee_ml, int water_ml, float milk_s, int strength, int temp) {
  if (drink <= 0 || drink > 0xFF) {
    ESP_LOGW(TAG, "save recipe: bad drink");
    return;
  }
  // Values <= 0 (temp < 0) stay as stored.
  std::map<uint8_t, uint16_t> changes;
  if (coffee_ml > 0) changes[0x01] = coffee_ml;
  if (water_ml > 0) changes[0x0F] = water_ml;
  if (milk_s > 0) changes[0x09] = uint16_t(milk_s * 10 + 0.5f);
  if (strength > 0) changes[0x02] = strength;
  if (temp >= 0 && temp <= 3) changes[0x0D] = temp;
  this->save_recipe_(uint8_t(drink), std::move(changes));
}

optional<uint16_t> Ecam::recipe_value(uint8_t tag) const {
  if (!this->status_valid_)
    return {};
  for (auto &tv : this->recipe_tlv_) {
    if (tv.first != tag)
      continue;
    auto it = this->recipe_edits_.find(tag);
    return it != this->recipe_edits_.end() ? it->second : tv.second;
  }
  return {};
}

void Ecam::save_recipe_edits() {
  if (!this->recipe_drink_ || this->recipe_edits_.empty()) {
    ESP_LOGI(TAG, "save recipe: nothing changed");
    return;
  }
  if (this->recipe_profile_ != this->active_profile_) {
    ESP_LOGW(TAG, "save recipe: the recipe shown is not from the active profile, read it again");
    return;
  }
  this->save_recipe_(this->recipe_drink_, this->recipe_edits_);
}

// Read the stored recipe of the active profile, change the given tags, save it back (op 00, tail 05, as the
// app's delete with tail 04). Tags the recipe does not have are not added.
void Ecam::save_recipe_(uint8_t drink, std::map<uint8_t, uint16_t> changes) {
  if (!this->active_profile_) {
    ESP_LOGW(TAG, "save recipe: profile not known yet");
    return;
  }
  // The app pairs 0d (water temp 0..3) with 1b 01 on max and 1b 04 otherwise.
  auto t = changes.find(0x0D);
  if (t != changes.end())
    changes[0x1B] = t->second == 3 ? 0x01 : 0x04;
  this->enqueue_({0xA6, 0xF0, this->active_profile_, drink}, 2, 500,
                 [this, drink, changes](const std::vector<uint8_t> *f) {
    if (!f || f->size() < 8) {
      ESP_LOGW(TAG, "save recipe: read failed, nothing written");
      return;
    }
    std::vector<uint8_t> p{0x83, 0xF0, drink, 0x00};
    for (auto &tv : parse_tlv(f->data() + 6, f->size() - 8)) {
      if (tv.first == 0x27)
        continue;
      auto c = changes.find(tv.first);
      put_tlv(p, tv.first, c != changes.end() ? c->second : tv.second);
    }
    p.insert(p.end(), {0x27, 0x00, this->tail_(1)});
    std::string tx = format_hex(p);
    this->enqueue_(std::move(p), 1, 1500, [this, drink, tx](const std::vector<uint8_t> *f) {
      ESP_LOGI(TAG, "save recipe %s -> %s", tx.c_str(), f ? format_hex(*f).c_str() : "no answer");
      this->read_recipe(drink, 0);
    }, true);
  }, true);
}

void Ecam::select_bean_adapt(uint8_t slot) {
  this->enqueue_({0xB9, 0xF0, slot}, 2, 800, [slot](const std::vector<uint8_t> *f) {
    ESP_LOGI(TAG, "bean adapt slot %u: %s", slot, f ? format_hex(*f).c_str() : "no answer");
  }, true);
}

// ---- drink names: ids and names as in the app's model data for the ECAM450.76.T (MachinesModels.json)

static const struct {
  uint8_t id;
  const char *name;
} DRINKS[] = {
    {0x01, "espresso"}, {0x02, "coffee"}, {0x03, "long coffee"}, {0x04, "2x espresso"}, {0x05, "doppio+"},
    {0x06, "americano"}, {0x07, "cappuccino"}, {0x08, "latte macchiato"}, {0x09, "caffelatte"},
    {0x0A, "flat white"}, {0x0B, "espresso macchiato"}, {0x0C, "hot milk"}, {0x0D, "cappuccino doppio+"},
    {0x0F, "cappuccino reverse"}, {0x10, "hot water"}, {0x16, "tea"}, {0x17, "coffee pot"}, {0x18, "cortado"},
    {0x1B, "brew over ice"}, {0x32, "iced americano"}, {0x33, "iced cappuccino"},
    {0x34, "iced latte macchiato"}, {0x35, "iced cappuccino mix"}, {0x36, "iced flat white"},
    {0x37, "iced cold milk"}, {0x38, "iced caffelatte"}, {0x50, "mug americano"}, {0x51, "mug cappuccino"},
    {0x52, "mug latte macchiato"}, {0x53, "mug caffelatte"}, {0x54, "mug cappuccino mix"},
    {0x55, "mug flat white"}, {0x56, "mug hot milk"}, {0x64, "mug brew over ice"}, {0x65, "mug iced americano"},
    {0x66, "mug iced cappuccino"}, {0x67, "mug iced latte macchiato"}, {0x68, "mug iced caffelatte"},
    {0x69, "mug iced cappuccino mix"}, {0x6A, "mug iced flat white"}, {0x6B, "mug iced cold milk"},
    {0xC8, "bean adapt espresso"}, {0xE6, "custom 1"}, {0xE7, "custom 2"}, {0xE8, "custom 3"},
    {0xE9, "custom 4"}, {0xEA, "custom 5"}, {0xEB, "custom 6"},
};

std::string Ecam::drink_name(uint8_t id) {
  for (auto &d : DRINKS)
    if (d.id == id)
      return d.name;
  return "0x" + format_hex(id);
}

int Ecam::drink_id(const std::string &name) {
  for (auto &d : DRINKS)
    if (strcasecmp(d.name, name.c_str()) == 0)
      return d.id;
  const char *s = name.c_str();
  if (s[0] == '0' && (s[1] == 'x' || s[1] == 'X'))
    s += 2;
  char *end;
  long v = strtol(s, &end, 16);
  return (*s && *end == 0 && v > 0 && v <= 0xFF) ? int(v) : -1;
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
  this->last_raw_ = tx + " -> ...";
  this->enqueue_(std::move(p), 1, 800, [this, tx](const std::vector<uint8_t> *f) {
    ESP_LOGI(TAG, "raw %s -> %s", tx.c_str(), f ? format_hex(*f).c_str() : "no answer");
    this->last_raw_ = tx + " -> " + (f ? format_hex(*f) : std::string("no answer"));
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

std::string Ecam::phase_text() const {
  if (!this->status_valid_)
    return "";
  if (this->status_.result == 0x01)
    return "aborted";
  uint8_t p = this->status_.phase;
  switch (this->status_.state) {
    case 0x01:  // power-on
      return p == 0x05 ? "rinsing" : p == 0x06 ? "finishing" : "heating";
    case 0x07:
      switch (p) {
        case 0x00: return "";
        case 0x04: case 0x05: return "grinding";
        case 0x06: return "brew unit";
        case 0x08: case 0x09: case 0x0A: return "pre-infusion";
        case 0x0B: return "extraction";
        case 0x0D: case 0x0E: return "finishing";
        case 0x10: return "waiting for beans";
        // Americano: water added after the coffee.
        case 0x7B: return "preparing";
        case 0x7F: return "hot water";
        case 0x80: case 0x81: return "finishing";
        default: {
          char text[16];
          snprintf(text, sizeof(text), "phase 0x%02X", p);
          return text;
        }
      }
    case 0x08: return "rinsing";
    case 0x0A:
      return p == 0x01 ? "milk heating" : p == 0x06 ? "aborted" : p == 0x07 ? "finishing" : "milk";
    case 0x0B:
      return p == 0x02 ? "preparing" : p == 0x03 ? "hot water" : "finishing";
    default: return "";
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
  // Names from the app (enum m6.l). Bit 16 ("tank too full") is set in normal operation and the app
  // ignores it, as it does 7, 10, 13, 20 and 21; they are listed here anyway except 16. Bit 26 (clock/BT
  // comm) stays set on models without a clock module (ECAM450.65.G), so it is left out too.
  static const char *const NAMES[28] = {
      "tank empty",        "grounds full",       "descale",          "replace filter",
      "grind too fine",    "beans empty",        "service",          "heater probe",
      "too much coffee",   "brew unit motor",    "steamer probe",    "empty drip tray",
      "hydraulic problem", "insert tank",        "clean knob",       "beans empty",
      nullptr,             "bean hopper absent", "grid presence",    "infuser sense",
      "beans empty",       "expansion comm",     "expansion modules", "grinder 1",
      "grinder 2",         "condenser fan",      nullptr,            "SPI comm"};
  if (!this->status_valid_)
    return "";
  std::string out;
  for (int i = 0; i < 32; i++) {
    if (!(this->status_.alarms & (1u << i)))
      continue;
    const char *n = i < 28 ? NAMES[i] : "";
    if (n == nullptr)
      continue;
    std::string name = *n ? std::string(n) : "alarm " + to_string(i);
    if (out.find(name) != std::string::npos)
      continue;
    if (!out.empty())
      out += ", ";
    out += name;
  }
  return out.empty() ? "none" : out;
}

}  // namespace ecam
}  // namespace esphome

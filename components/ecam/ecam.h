#pragma once

#include <array>
#include <deque>
#include <functional>
#include <map>
#include <string>
#include <vector>

#include "esphome/core/component.h"
#include "esphome/core/defines.h"
#include "esphome/core/optional.h"
#include "esphome/components/uart/uart.h"
#ifdef USE_SENSOR
#include "esphome/components/sensor/sensor.h"
#endif

namespace esphome {
namespace ecam {

// Protocol: docs/protocol.md. Monitor answer (75 0f): [0] accessory, [1..2] switches LE, [3..4] alarms LE,
// [5] state, [6] phase, [7] progress %, [8] flags (bit 4: a brew waits for beans), [9..12] unknown.
struct Status {
  uint8_t accessory{0};
  uint16_t switches{0};
  uint16_t alarms{0};
  uint8_t state{0};
  uint8_t phase{0};
  uint8_t flags{0};
  uint8_t progress{0};
};

class Ecam : public Component, public uart::UARTDevice {
 public:
  void setup() override;
  void loop() override;
  float get_setup_priority() const override { return setup_priority::DATA; }

  void set_wifi_icon(uint8_t v) { wifi_icon_ = v; }

  // Commands. Return false when refused locally (machine busy / bad args).
  void power_on();
  void power_off();
  bool brew(int drink, int coffee_ml, int water_ml, float milk_s, int strength, int temp);
  void stop();
  // Parameters (95 read / 90 write). Every read lands in a cache that param() returns; a write re-reads.
  void read_param(uint16_t pid, std::function<void(uint32_t)> cb = nullptr);
  void write_param(uint16_t pid, uint32_t value);
  // Read-modify-write of flag bits (PAR 63); never writes when the read fails.
  void write_param_bits(uint16_t pid, uint32_t mask, bool on);
  optional<uint32_t> param(uint16_t pid) const;
  optional<bool> param_bit(uint16_t pid, uint32_t mask) const;

  // Everything the entities show besides the monitor: settings, profiles, favourites, custom drinks and
  // statistics; with device_info also serial number, production date, EEPROM version and PIN.
  void refresh(bool device_info);

  // Statistics (A2 0f): all counters by id, published to the ecam counter sensors.
  optional<uint32_t> stat(uint16_t id) const;
#ifdef USE_SENSOR
  void add_counter(uint16_t id, sensor::Sensor *s) { this->counters_.push_back({id, s}); }
#endif

  // Profiles: slot 1..4, 0 = not read yet.
  uint8_t active_profile() const { return this->active_profile_; }
  void select_profile(uint8_t slot);
  optional<std::string> profile_name(uint8_t slot) const;
  void rename_profile(uint8_t slot, const std::string &name);

  // Favourites of the active profile (drink names, newest first) and the custom drink slots 1..6.
  optional<std::string> favorites_text() const;
  void set_favorites(const std::string &drinks);
  optional<std::string> custom_drinks_text() const;
  void delete_custom_drink(uint8_t slot);

  // Stored recipes (A6 read, 83 .. 00 .. 05 save). The text is "<profile> <drink>: tag value, ...".
  void read_recipe(int drink, int profile);
  void save_recipe(int drink, int coffee_ml, int water_ml, float milk_s, int strength, int temp);
  const std::string &recipe_text() const { return this->recipe_text_; }
  // Recipe editor: the last recipe read (read_recipe) by TLV tag, with edits staged until save_recipe_edits().
  // A tag the drink does not have reads empty, so the entity shows unknown.
  optional<uint16_t> recipe_value(uint8_t tag) const;
  // Edits are clamped to the drink's limits (B0) when the machine reported them.
  void set_recipe_value(uint8_t tag, uint16_t value);
  void save_recipe_edits();
  // "coffee_ml 20-180, strength 1-5, ..." for the recipe shown; empty until the limits arrived.
  std::string recipe_limits_text() const;

  // Machine clock (E2 f0 hh mm, as the app sends it).
  void set_clock(uint8_t hour, uint8_t minute);
  // Logs every statistics counter (id = value) at INFO.
  void dump_counters() const;
  const std::map<uint16_t, uint32_t> &counters() const { return this->stats_; }

  // Exploration: read PAR start .. start+count-1 one by one in the background (reads only); keeps the
  // non-zero values. scan_done() is false while it runs.
  void scan_params(uint16_t start, uint16_t count);
  const std::map<uint16_t, uint32_t> &scan_results() const { return this->scan_; }
  bool scan_done() const { return this->scan_left_ == 0; }
  uint16_t scan_answered() const { return this->scan_answered_; }
  uint16_t scan_next() const { return this->scan_next_; }
  // "<tx> -> <rx>" of the last raw request.
  const std::string &last_raw() const { return this->last_raw_; }

  // Skip the rinse before the drink started from here (the app's SKIP_RINSE: op 06, RINSE 27 00).
  void skip_rinse();

  void select_bean_adapt(uint8_t slot);

  // Drinks: id <-> name ("espresso", "flat white", ...); unknown ids read as hex, names also parse hex.
  static std::string drink_name(uint8_t id);
  static int drink_id(const std::string &name);
  // Debug: send any payload (hex, spaces allowed) and log the answer at INFO as "raw <tx> -> <rx>".
  void raw(const std::string &hex);

  // The machine's answer to the last brew sent from here, "<drink> accepted|rejected|no answer" and then
  // the answer's bytes after 83 f0 (accepted is 01 00). brew_answers() counts the answers, so a repeat of
  // the same text is still a new answer.
  const std::string &last_brew() const { return last_brew_; }
  uint32_t brew_answers() const { return brew_answers_; }

  bool has_status() const { return status_valid_; }
  const Status &status() const { return status_; }
  // Standby (00) keeps the phase byte of the last drink, so the phase only counts in state 07.
  bool idle() const {
    return status_valid_ && (status_.state == 0x00 || (status_.state == 0x07 && status_.phase == 0));
  }
  bool brewing() const {
    return status_valid_ && !idle() && status_.state != 0x01 && status_.state != 0x02;
  }

  std::string state_text() const;
  // Beans empty: the alarm bits (an idle machine) or flags bit 4 (a brew that ran out).
  bool beans_empty() const {
    return status_valid_ && ((status_.alarms & ((1 << 5) | (1 << 15))) != 0 || (status_.flags & 0x10) != 0);
  }
  std::string alarms_text() const;
  std::string accessory_text() const;

  // Called after every new monitor answer.
  void add_on_status_callback(std::function<void()> cb) { status_cbs_.push_back(std::move(cb)); }

 protected:
  struct Request {
    std::vector<uint8_t> payload;
    int retries;
    uint32_t timeout_ms;
    std::function<void(const std::vector<uint8_t> *)> done;  // nullptr arg = no answer
  };

  void enqueue_(std::vector<uint8_t> payload, int retries, uint32_t timeout_ms,
                std::function<void(const std::vector<uint8_t> *)> done = nullptr, bool front = false);
  void send_(const std::vector<uint8_t> &payload);
  void on_frame_(const std::vector<uint8_t> &f);
  static uint16_t crc_(const uint8_t *data, size_t len);

  std::vector<uint8_t> rx_;
  std::deque<Request> queue_;
  bool in_flight_{false};
  uint32_t sent_at_{0};
  int attempts_{0};

  Status status_;
  bool status_valid_{false};
  uint32_t status_at_{0};
  uint32_t wifi_at_{0};
  uint8_t wifi_icon_{0};  // 0 = follow the real wifi / Home Assistant state
  uint8_t wifi_icon_now_() const;
  uint8_t last_drink_{0};
  std::string last_brew_;
  uint32_t brew_answers_{0};
  std::vector<std::function<void()>> status_cbs_;

  void read_stats_(uint16_t start, int pages);
  void read_favorites_();
  void save_recipe_(uint8_t drink, std::map<uint8_t, uint16_t> changes);
  std::map<uint16_t, uint32_t> params_;
  std::map<uint16_t, uint32_t> stats_;
  uint8_t active_profile_{0};
  std::array<std::string, 4> profile_names_;
  bool profiles_valid_{false};
  std::vector<uint8_t> favorites_;
  bool favorites_valid_{false};
  std::array<std::string, 6> custom_names_;
  bool customs_valid_{false};
  std::string recipe_text_;
  uint8_t recipe_drink_{0};
  uint8_t recipe_profile_{0};
  std::vector<std::pair<uint8_t, uint16_t>> recipe_tlv_;
  std::map<uint8_t, uint16_t> recipe_edits_;
  std::map<uint16_t, uint32_t> scan_;
  uint16_t scan_next_{0};
  uint16_t scan_left_{0};
  uint16_t scan_answered_{0};
  uint32_t scan_at_{0};
  void scan_step_();
  std::string last_raw_;
  struct Limit { uint16_t min, def, max; };
  std::map<uint8_t, Limit> recipe_limits_;
  void read_limits_(uint8_t drink);
  // Last byte of 83 f0: (profile << 2) | operation (0 delete, 1 save, 2 prepare).
  uint8_t tail_(uint8_t op) const { return uint8_t(((this->active_profile_ ? this->active_profile_ : 1) << 2) | op); }
#ifdef USE_SENSOR
  std::vector<std::pair<uint16_t, sensor::Sensor *>> counters_;
#endif
};

}  // namespace ecam
}  // namespace esphome

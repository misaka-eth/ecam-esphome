#pragma once

#include <array>
#include <deque>
#include <functional>
#include <string>
#include <vector>

#include "esphome/core/component.h"
#include "esphome/components/uart/uart.h"

namespace esphome {
namespace ecam {

// Protocol: docs/protocol.md. Monitor answer (75 0f): [0] accessory, [1..2] switches LE, [3..4] alarms LE,
// [5] state, [6] phase, [7] progress %, [8..12] unknown.
struct Status {
  uint8_t accessory{0};
  uint16_t switches{0};
  uint16_t alarms{0};
  uint8_t state{0};
  uint8_t phase{0};
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
  void read_param(uint16_t pid, std::function<void(uint32_t)> cb);
  // Debug: send any payload (hex, spaces allowed) and log the answer at INFO as "raw <tx> -> <rx>".
  void raw(const std::string &hex);

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
  std::vector<std::function<void()>> status_cbs_;
};

}  // namespace ecam
}  // namespace esphome

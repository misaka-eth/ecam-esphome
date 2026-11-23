#pragma once

#include "esphome/core/component.h"
#include "esphome/components/web_server_base/web_server_base.h"

namespace esphome::ecam_web {

// Serves the gzipped UI bundle at /ecam.js.
class EcamWeb : public Component, public AsyncWebHandler {
 public:
  EcamWeb(const uint8_t *data, size_t size) : data_(data), size_(size) {}
  void setup() override;
  float get_setup_priority() const override { return setup_priority::WIFI - 1.0f; }

  bool canHandle(AsyncWebServerRequest *request) const override;
  void handleRequest(AsyncWebServerRequest *request) override;

 protected:
  const uint8_t *data_;
  size_t size_;
};

}  // namespace esphome::ecam_web

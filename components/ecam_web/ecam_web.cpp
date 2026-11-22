#include "ecam_web.h"

namespace esphome::ecam_web {

void EcamWeb::setup() { web_server_base::global_web_server_base->add_handler(this); }

bool EcamWeb::canHandle(AsyncWebServerRequest *request) const {
#ifdef USE_ESP32
  char buf[AsyncWebServerRequest::URL_BUF_SIZE];
  StringRef url = request->url_to(buf);
#else
  const auto &url = request->url();
#endif
  return request->method() == HTTP_GET && url == "/ecam.js";
}

void EcamWeb::handleRequest(AsyncWebServerRequest *request) {
  AsyncWebServerResponse *response = request->beginResponse(200, "text/javascript", this->data_, this->size_);
  response->addHeader("Content-Encoding", "gzip");
  response->addHeader("Cache-Control", "no-cache");
  request->send(response);
}

}  // namespace esphome::ecam_web

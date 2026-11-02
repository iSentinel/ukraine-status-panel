#pragma once
#include "esphome/components/web_server_idf/web_server_idf.h"
namespace esphome::web_server_base {
struct WebServerBase {
  int protected_handlers=0;
  void add_handler(web_server_idf::AsyncWebHandler *) {++protected_handlers;}
};
inline WebServerBase *global_web_server_base=nullptr;
}

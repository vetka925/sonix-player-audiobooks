#include "src/system/bluetooth/btstack.h"
#include <assert.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>

#define BT_MAX_DEVICES 24
#define CONNECT_TIMEOUT_MS 8000
static bool wanted = true, sink_ready = true, connect_ok = true;
static bool headphones, other_phone;
static int calls;
static btstack_device_t device;
static bool receiver_profile_wanted(void) { return wanted; }
static bool bluealsa_has_sink(void) { return sink_ready; }
int btstack_devices(btstack_device_t *out, int max) {
  *out = device;
  return 1;
}
bool btstack_audio_sink(char *out, size_t n) { return headphones; }
bool btstack_audio_source(char *out, size_t n) {
  if (other_phone)
    snprintf(out, n, "other");
  return other_phone;
}
bool btstack_connect(const char *mac, int timeout, char *error, size_t n) {
  calls++;
  assert(!strcmp(mac, "phone"));
  return connect_ok;
}
#include "receiver-worker.h"

int main(void) {
  snprintf(device.address, sizeof(device.address), "phone");
  device.paired = true;
  device.audio_source = true;
  assert(connect_receiver_device("phone") && calls == 1);
  wanted = false;
  assert(!connect_receiver_device("phone") && calls == 1);
  wanted = true;
  sink_ready = false;
  assert(!connect_receiver_device("phone") && calls == 1);
  sink_ready = true;
  device.audio_source = false;
  assert(!connect_receiver_device("phone") && calls == 1);
  device.audio_source = true;
  device.paired = false;
  assert(!connect_receiver_device("phone") && calls == 1);
  device.paired = true;
  headphones = true;
  assert(!connect_receiver_device("phone") && calls == 1);
  headphones = false;
  other_phone = true;
  assert(!connect_receiver_device("phone") && calls == 1);
  other_phone = false;
  connect_ok = false;
  assert(!connect_receiver_device("phone") && calls == 2);
  puts("PASS receiver connection, cancellation, profile readiness, source "
       "selection, failure and occupied output");
}

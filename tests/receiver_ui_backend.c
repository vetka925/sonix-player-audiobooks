// Link-time backend for exercising the real Receiver UI in the host simulator.
// Never linked into target firmware.
#include "lvgl/lvgl.h"
#include "src/system/bluetooth/bluetooth.h"
#include "src/system/bluetooth/btreceiver.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static bool enabled, profile, connected, active, pending;
static unsigned serial, attempts;
static uint32_t requested;
void __wrap_bluetooth_init(void) {}
bool __wrap_bluetooth_available(void) { return true; }
bool __wrap_bluetooth_get_enabled(void) { return enabled; }
void __wrap_bluetooth_set_enabled(bool on) {
  enabled = on;
  serial++;
}
bool __wrap_bluetooth_busy(void) { return false; }
bool __wrap_bluetooth_scan_running(void) { return false; }
bool __wrap_bluetooth_audio_active(void) { return false; }
void __wrap_bluetooth_set_receiver_profile(bool on) { profile = on; }
void __wrap_bluetooth_set_discoverable(bool on) {}
uint32_t __wrap_bluetooth_devices_serial(void) { return serial; }
int __wrap_bluetooth_get_paired(bt_device_t *out, int max) {
	if (getenv("SONIX_TEST_NO_SOURCES")) return 0;
  if (max < 2)
    return 0;
  out[0] = (bt_device_t){.mac = "AA:00:00:00:00:01",
                         .name = "Galaxy Z Fold7",
                         .paired = true,
                         .audio_source = true};
  out[1] = (bt_device_t){.mac = "AA:00:00:00:00:02",
                         .name = "Headphones (must not appear in Receiver)",
                         .paired = true};
  return 2;
}
void __wrap_bluetooth_connect_receiver(const char *mac) {
  if (!profile || strcmp(mac, "AA:00:00:00:00:01"))
    return;
  pending = true;
  requested = lv_tick_get();
  attempts++;
  fprintf(stderr, "UI TEST: receiver attempt %u\n", attempts);
}
bt_op_t __wrap_bluetooth_take_op_result(char *out, int size) {
  if (!pending)
    return BT_OP_IDLE;
  if (lv_tick_get() - requested < 1800)
    return BT_OP_BUSY;
  pending = false;
  if (attempts == 1)
    return BT_OP_FAILED;
  connected = true;
  serial++;
  return BT_OP_OK;
}
bool __wrap_bluetooth_receiver_device(char *mac, size_t n, char *name,
                                      size_t size) {
  if (!connected)
    return false;
  if (mac)
    snprintf(mac, n, "AA:00:00:00:00:01");
  if (name)
    snprintf(name, size, "Galaxy Z Fold7");
  return true;
}
bool __wrap_btreceiver_is_active(void) { return active; }
bool __wrap_btreceiver_start(void) {
  active = connected;
  serial++;
  return active;
}
void __wrap_btreceiver_stop(void) { active = false; }
unsigned __wrap_btreceiver_serial(void) { return serial; }
void __wrap_btreceiver_get_state(btreceiver_state_t *out) {
  *out = (btreceiver_state_t){
      .active = active, .streaming = active, .sample_rate = 44100};
  snprintf(out->device, sizeof(out->device), "Galaxy Z Fold7");
  snprintf(out->codec, sizeof(out->codec), "aptX");
}

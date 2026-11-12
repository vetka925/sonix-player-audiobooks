#include "src/system/bluetooth/bluetooth.h"
#include <assert.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>
typedef struct {
  const char *mac;
} lv_event_t;
static bool connecting, connect_failed = true;
static char started_for[24] = "phone";
static int calls;
bool switcher_back_drag_active(void) { return false; }
void *lv_event_get_user_data(lv_event_t *e) { return (void *)e->mac; }
bt_op_t bluetooth_take_op_result(char *name, int size) { return BT_OP_IDLE; }
void bluetooth_connect_receiver(const char *mac) {
  assert(!strcmp(mac, "phone"));
  calls++;
}
#include "receiver-click.h"
int main(void) {
  lv_event_t event = {.mac = "phone"};
  source_clicked(&event);
  assert(connecting && !connect_failed && calls == 1);
  // A previous capture failure for this peer must not suppress the retry.
  assert(started_for[0] == '\0');
  source_clicked(&event);
  assert(calls == 1);
  puts("PASS receiver retry after capture failure and duplicate-tap "
       "suppression");
}

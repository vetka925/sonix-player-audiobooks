"""Run production idle branches with scheduling/socket boundaries controlled."""
from pathlib import Path
import subprocess, tempfile
root = Path(__file__).resolve().parent.parent
bt = (root/'sonix-player/src/system/bluetooth/btvolume.c').read_text()
start = bt.index('unsigned wait_ms = IDLE_WAIT_MS;')
branch = bt[start:bt.index('} else if (!conn)', start)] + '}'
remote = (root/'sonix-player/src/system/remote/sonixlink.c').read_text()
start = remote.index('if (!sonixlink_get_enabled())')
end = remote.index('\n\t\tif (listener < 0)', start)
remote_branch = remote[start:end]
constants = '\n'.join(line for line in bt.splitlines() if line.startswith('#define IDLE_'))
fixtures = {
'btvolume-idle': r'''
#include <assert.h>
#include <stdbool.h>
#include <stddef.h>
''' + constants + r'''
static int disconnected;
static bool dbus_alive(void *p) { return true; }
static void dbus_disconnect(void *p) { disconnected++; }
static unsigned run(const char *path, void *conn) {
 enum { BTV_SETUP, BTV_IDLE }; int state=0;
''' + branch + r'''
 return wait_ms;
}
int main(void) {
 assert(run("", (void*)1)==60000); assert(disconnected==1);
 assert(run("/pcm", (void*)1)==600); assert(disconnected==1);
}
''',
'sonixlink-disabled': r'''
#include <assert.h>
#include <stdbool.h>
#include <stdio.h>
#include <setjmp.h>
#define MAX_CLIENTS 2
static struct { int fd; } clients[MAX_CLIENTS];
static int lock, enabled_cond, listener=-1, beacon=-1, mdns=-1;
static char address_text[8];
static bool enabled;
static int waits, polls, closed;
static jmp_buf done;
static int sonixlink_get_enabled(void) { return enabled; }
static void client_close(void *p) { closed++; ((typeof(clients[0])*)p)->fd=-1; }
static void close(int fd) {}
static void pthread_mutex_lock(void *p) {}
static void pthread_mutex_unlock(void *p) {}
static void pthread_cond_wait(void *c, void *m) { waits++; enabled=true; }
static void usleep(int n) { polls++; longjmp(done,1); }
static void run(void) { for (int i=0;i<2;i++) {
''' + remote_branch + r'''
 break;
}}
int main(void) {
 clients[0].fd=42; clients[1].fd=-1;
 if (!setjmp(done)) run();
 assert(polls==0); assert(waits==1); assert(closed==1);
}
'''}
failed=0
with tempfile.TemporaryDirectory() as td:
 for name, source in fixtures.items():
  c=Path(td)/(name+'.c'); c.write_text(source); exe=c.with_suffix('')
  subprocess.run(['cc','-std=gnu11','-O0',str(c),'-o',str(exe)],check=True)
  r=subprocess.run([str(exe)],capture_output=True)
  print(('PASS ' if r.returncode==0 else 'FAIL ')+name, flush=True)
  if r.returncode: print(r.stderr.decode()); failed+=1
raise SystemExit(bool(failed))

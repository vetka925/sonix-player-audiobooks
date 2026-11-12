"""Execute actual external PCM write/recovery functions against ALSA fault injection."""
from pathlib import Path
import subprocess,tempfile
root=Path(__file__).resolve().parent.parent
text=(root/'sonix-player/src/system/audio/audio.c').read_text()
def fn(name):
 import re
 m=re.search(r'^(?:static )?[^\n;{}]*\b'+name+r'\([^;]*?\) \{',text,re.M)
 if not m:return ''
 start=m.start(); depth=1; end=m.end()
 while depth:
  if text[end]=='{': depth+=1
  elif text[end]=='}': depth-=1
  end+=1
 return text[start:end]+'\n'
source=r'''
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdatomic.h>
#include <stdio.h>
#include <errno.h>
#include <pthread.h>
#include <string.h>
#include <time.h>
typedef void snd_pcm_t;
typedef long snd_pcm_sframes_t;
typedef unsigned long snd_pcm_uframes_t;
#ifndef ESTRPIPE
#define ESTRPIPE 86
#endif
#define BT_WRITE_STALL_MS 2000
static snd_pcm_t *external_pcm=(void*)1;
static pthread_mutex_t external_lock=PTHREAD_MUTEX_INITIALIZER;
static atomic_uint external_stopping;
static int external_frame_bytes=4,external_channels=2,external_bits=16,external_rate=44100;
static int external_buffer_ms,pcm_forced_buffer_ms,external_s24;
static bool pcm_device_open=true,external_yielding;
static int external_route;
static char external_device[160];
static long external_checked_ms,external_underrun_logged_ms;
static unsigned external_underruns;
static int calls,opens,drops,closes,scenario;
static uint64_t elapsed;
static long log_ms(void) {return (long)(elapsed+=100);}
static int fake_clock_gettime(int id,struct timespec *ts) {elapsed+=100;ts->tv_sec=elapsed/1000;ts->tv_nsec=(elapsed%1000)*1000000;return 0;}
#define clock_gettime fake_clock_gettime
static void usleep(int us) {elapsed+=us/1000;}
static bool external_follow_output(void) {return true;}
static bool pcm_takes_s24(snd_pcm_t *p,int bits) {return false;}
static void *s24_from_s32(void *b,const void *f,size_t n) {return (void*)f;}
static const char *snd_strerror(int n) {return "injected";}
static int snd_pcm_drop(snd_pcm_t *p) {drops++;return 0;}
static int snd_pcm_close(snd_pcm_t *p) {closes++;return 0;}
static int snd_pcm_wait(snd_pcm_t *p,int ms) {elapsed+=ms;return 1;}
static int pcm_wait_bluetooth(snd_pcm_t *p,int ms) {elapsed+=ms;return 1;}
static int snd_pcm_prepare(snd_pcm_t *p) {return scenario==4 ? -EIO:0;}
static int snd_pcm_resume(snd_pcm_t *p) {assert(++calls<100);elapsed+=100;return -EAGAIN;}
static snd_pcm_t *open_pcm_device(int c,int r,int b,snd_pcm_uframes_t *period) {
 assert(c==2 && r==44100 && b==16);opens++;return (void*)1;
}
static bool external_open_locked(void) {opens++;external_pcm=(void*)1;return true;}
static long snd_pcm_writei(snd_pcm_t *p,const void *data,unsigned long count) {
 assert(++calls<100);elapsed+=100;
 if(scenario==0 || (scenario==7 && opens) || (scenario==8 && calls>1)) return count;
 if(scenario==8) return -EPIPE;
 if(scenario==5) {atomic_store(&external_stopping,1);return -EAGAIN;}
 if(scenario==6 && calls==1) return 2;
 if(scenario==2 || scenario==4) return -EPIPE;
 if(scenario==3) return -ESTRPIPE;
 return -EAGAIN;
}
'''
for name in ['external_write_cancelled','external_clock_ms','external_reopen_pcm_locked','external_wait_room','audio_external_write','audio_external_end']:
 source+=fn(name)
source+=r'''
int main(int argc,char **argv) {
 scenario=atoi(argv[1]); int frames[16]={0};
 int n=audio_external_write(frames,8);
 if(scenario==0) assert(n==8 && calls==1);
 else if(scenario==7 || scenario==8) assert(n==8);
 else if(scenario==6) assert(n==2);
 else assert(n==0);
 assert(opens<=1 && calls<50 && elapsed<5000);
 if(scenario==4) assert(calls==1);
 if(scenario==5) assert(calls==1);
 atomic_store(&external_stopping,0);
 audio_external_end(); assert(!external_pcm && !atomic_load(&external_stopping));
 puts("PASS external PCM scenario");
}
'''
source='#include <stdlib.h>\n'+source
failed=0
with tempfile.TemporaryDirectory() as td:
 c=Path(td)/'test.c';c.write_text(source); exe=c.with_suffix('')
 subprocess.run(['cc','-std=gnu11','-O1',str(c),'-pthread','-o',str(exe)],check=True)
 for case in range(9):
  result=subprocess.run([str(exe),str(case)],capture_output=True,timeout=5)
  print(('PASS ' if not result.returncode else 'FAIL ')+str(case))
  if result.returncode:failed+=1; print(result.stderr.decode()[-350:])
raise SystemExit(bool(failed))

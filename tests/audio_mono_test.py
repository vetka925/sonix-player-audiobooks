"""Execute production Bluetooth open routing with real ALSA configuration parsing."""
from pathlib import Path
import subprocess,tempfile
root=Path(__file__).resolve().parent.parent
text=(root/'sonix-player/src/system/audio/audio.c').read_text()
start=text.index('static int open_bluetooth_at_rate(')
helper=text[start:text.index('\n}\n',start)+3]
start=text.index('\tunsigned convert_to = 0;',text.index('static snd_pcm_t *open_pcm_device_now'))
end=text.index('\n\t// Headphones that walked out',start)
route=text[start:end]
source=r'''
#include <alsa/asoundlib.h>
#include <assert.h>
#include <stdbool.h>
#include <unistd.h>
#define BT_CONVERTED_PCM "sonix_test"
static char output_pcm[256],bt_converted_device[256];
static snd_pcm_t *bt_converted_pcm;
static unsigned sink_rate=48000;
static int follow, seen_channels, seen_rate, direct;
static int config_get_int(const char *a,const char *b,int d) {return follow;}
static unsigned bluetooth_sink_rate(void) {return sink_rate;}
static bool bluetooth_audio_active(void) {return true;}
static int fake_open(snd_pcm_t **out,const char *name,snd_pcm_stream_t stream,int mode) {
 direct++; *out=(snd_pcm_t*)1; return 0;
}
static int fake_lconf(snd_pcm_t **out,const char *name,snd_pcm_stream_t stream,int mode,snd_config_t *conf) {
 snd_config_t *node; long n;
 assert(snd_config_search(conf,"pcm.sonix_test.slave.rate",&node)==0);
 assert(snd_config_get_integer(node,&n)==0); seen_rate=n;
 if(snd_config_search(conf,"pcm.sonix_test.slave.channels",&node)==0) {
  assert(snd_config_get_integer(node,&n)==0); seen_channels=n;
 }
 *out=(snd_pcm_t*)1; return 0;
}
#define snd_pcm_open fake_open
#define snd_pcm_open_lconf fake_lconf
'''+helper+r'''
static void run(bool bluetooth,int channels,int sample_rate) {
 snd_pcm_t *pcm_handle=NULL; const char *device=bluetooth?"bluealsa:DEV=AA":"hw:0"; int mode=0;
 seen_channels=seen_rate=direct=0;
'''+route+r'''
 assert(err==0);
}
int main(void) {
 run(true,1,44100); assert(seen_channels==2 && seen_rate==48000 && !direct);
 run(true,1,48000); assert(seen_channels==2 && seen_rate==48000 && !direct);
 follow=1; run(true,1,44100); assert(seen_channels==2 && seen_rate==44100);
 follow=0; run(true,2,48000); assert(direct==1 && seen_channels==0);
 run(true,2,44100); assert(!direct && seen_channels==0 && seen_rate==48000);
 run(false,1,44100); assert(direct==1 && seen_channels==0);
 puts("PASS mono Bluetooth routing, equal/different rates, follow-rate, stereo and wired paths");
}
'''
with tempfile.TemporaryDirectory() as td:
 c=Path(td)/'test.c';c.write_text(source); exe=c.with_suffix('')
 subprocess.run(['cc','-O1',str(c),'-lasound','-o',str(exe)],check=True)
 subprocess.run([str(exe)],check=True)

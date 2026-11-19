#ifndef YOUTUBEPAGE_H
#define YOUTUBEPAGE_H
#include "src/gui/shell/gui.h"

extern lv_obj_t *youtube_screen;
bool youtube_offline_downloading(void);
void youtubepage_init(gui_config_t *cfg);
#endif

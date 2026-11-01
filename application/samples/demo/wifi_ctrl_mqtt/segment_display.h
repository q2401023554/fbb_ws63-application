#ifndef SEGMENT_DISPLAY_H
#define SEGMENT_DISPLAY_H

#include <stdint.h>

void segment_display_init(void);
void segment_display_set_number(uint16_t number);
void segment_display_refresh_once(void);
void segment_display_clear(void);

#endif

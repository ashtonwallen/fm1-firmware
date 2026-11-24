/* SPDX-License-Identifier: MPL-2.0 */
#ifndef SHORTWAVE_RADIO_H
#define SHORTWAVE_RADIO_H

#include <stdint.h>

enum { RADIO_STATIONS = 4 };

int radio_station_center(int station);
int radio_signal(int dial, int station);
int radio_level(void);

void radio_init(void);
void radio_controls(int dial, int noise, int motion, int playback);
void radio_render(int32_t *lr, uint32_t count, uint32_t gain_q12);

#endif

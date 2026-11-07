// APU task glue between minigb_apu and M5Unified Speaker.
#pragma once

#include "fe_config.h"

namespace fe {

void audioInit();          // initialise minigb_apu state
void audioTaskEntry(void*);  // FreeRTOS task entry (priority 6, core 0)
void audioTaskBody();        // one batch of 548 samples

extern uint8_t gApuVolume;
extern volatile bool gAudioPause;

}  // namespace fe

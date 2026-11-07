// APU task: minigb_apu -> 8-bit unsigned mono -> M5Unified Speaker.

#include <Arduino.h>
#include <M5Cardputer.h>
#include <math.h>
#include <string.h>

#include "fe_walnut.h"
#include "fe_audio.h"
#include "fe_state.h"

namespace fe {

uint8_t gApuVolume = kDefaultVolume;
volatile bool gAudioPause = false;

namespace {

int16_t sApuSamples[kAudioSamples * 2];       // stereo interleaved
uint8_t sAudioOut[2][kAudioSamples];          // double buffer
uint8_t sBufferIndex = 0;

// The firmware's apu_volume_convert() divides by apu_volume_curve
// (0x422D3333 == 43.3f) rather than by the 0..127 range.
constexpr float kVolumeCurve = 43.3f;

}  // namespace

void audioInit()
{
    audio_init();
    gApuVolume = gSettings.volumeStart ? gSettings.volumeStart : kDefaultVolume;
    gAudioPause = false;
}

void audioTaskBody()
{
    if (gApuVolume == 0 || gAudioPause) {
        return;
    }

    while (!gSharedFlags[kSharedAudioPause] && gApuVolume != 0) {
        // Fill 548 stereo int16 samples.
        audio_callback(nullptr, reinterpret_cast<uint8_t*>(sApuSamples),
                       sizeof(sApuSamples));

        const float volumeScale = static_cast<float>(gApuVolume) / kVolumeCurve;
        uint8_t* out = sAudioOut[sBufferIndex];

        for (int i = 0; i < kAudioSamples; i++) {
            const int32_t left  = sApuSamples[i * 2 + 0];
            const int32_t right = sApuSamples[i * 2 + 1];
            int32_t mixed = (left + right) / 2;
            mixed = static_cast<int32_t>(static_cast<float>(mixed) * volumeScale);
            mixed >>= 8;
            if (mixed < -128) mixed = -128;
            if (mixed > 127)  mixed = 127;
            // The firmware emits unsigned 8-bit PCM with a 0x80 midpoint.
            out[i] = static_cast<uint8_t>(mixed + 128);
        }

        M5Cardputer.Speaker.playRaw(out, kAudioSamples, kSampleRate,
                                    false, 1, -1, false);
        sBufferIndex ^= 1;
    }
}

void audioTaskEntry(void* /*arg*/)
{
    while (gAppRunning) {
        if (!gSharedFlags[kSharedAudioPause] && gApuVolume != 0) {
            audioTaskBody();
        }
        vTaskDelay(10);
    }
    vTaskDelete(nullptr);
}

}  // namespace fe

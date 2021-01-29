/*
 * Copyright (c) 2021, Johan Venter <johan.venter@gmail.com>
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 * 1. Redistributions of source code must retain the above copyright notice, this
 *    list of conditions and the following disclaimer.
 *
 * 2. Redistributions in binary form must reproduce the above copyright notice,
 *    this list of conditions and the following disclaimer in the documentation
 *    and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 * DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
 * SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER
 * CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
 * OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#pragma once

#include "AudioDecoder.h"
#include <AK/Format.h>

class TwosAudioDecoder : public AudioDecoder {
public:
    TwosAudioDecoder(int bits_per_sample, int sample_rate, int channels)
        : AudioDecoder(bits_per_sample, sample_rate, channels) {};

    virtual void decode_samples(const u8* src, u32 sample_count, Vector<Audio::Sample>& dst) override
    {
        ASSERT(m_bits_per_sample == 16);
        ASSERT(m_channels == 2);

        u32 samples_decoded = 0;

        while (sample_count--) {
            i16 left = src[0] << 8 | src[1];
            i16 right = src[2] << 8 | src[3];

            dst.append(Audio::Sample {
                -1.0 + 2.0 * (((double)left + 32768.0) / 65535.0),
                -1.0 + 2.0 * (((double)right + 32768.0) / 65535.0),
            });

            samples_decoded++;
            src += 4;
        }
    }
};

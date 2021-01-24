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
#include "VideoDecoder.h"
#include <AK/MappedFile.h>
#include <AK/MemoryStream.h>
#include <AK/OwnPtr.h>
#include <AK/RefCounted.h>
#include <AK/String.h>
#include <AK/Vector.h>
#include <LibGfx/Bitmap.h>
#include <LibGfx/Size.h>

#define ATOM_HEADER_SIZE 8
#define ATOM_TYPE(a, b, c, d) (d << 24 | c << 16 | b << 8 | a)

class MOVFile : public RefCounted<MOVFile> {
public:
    MOVFile(const StringView& path);

    bool has_error() const { return !m_error_string.is_null(); }
    const char* error_string() const { return m_error_string.characters(); }

    Gfx::IntSize frame_size() const;
    u16 depth() const;
    u32 duration() const;
    u32 frame_count() const;
    u32 ms_per_frame() const;
    u32 audio_sample_count() const;
    u32 audio_samples_per_frame() const;
    u32 audio_sample_size() const;

    RefPtr<Gfx::Bitmap> frame(u32);
    Vector<Audio::Sample> decode_audio_samples(u32 frame_index);

private:
    enum class TrackType {
        Audio,
        Video
    };

    struct SampleDescriptionEntry {
        u32 size;
        u32 format;
        u16 reference_index;

        union {
            struct {
                u16 width;
                u16 height;
                u16 depth;
                u16 frames_per_sample;
            };

            struct {
                u16 version;
                u16 channels;
                u16 sample_size;
                u32 sample_rate;
                u32 samples_per_packet;
                u32 bytes_per_packet;
                u32 bytes_per_frame;
                u32 bytes_per_sample;
            };
        };
    };

    struct TimeToSampleEntry {
        u32 sample_count;
        u32 sample_duration;
    };

    struct SampleToChunkEntry {
        u32 first_chunk;
        u32 chunk_count;
        u32 samples_per_chunk;
        u32 sample_description_id;
    };

    struct Track {
        TrackType type;
        u32 duration;
        u32 width;
        u32 height;
        SampleDescriptionEntry sample_description;
        Vector<TimeToSampleEntry> time_to_sample_entries;
        Vector<u32> sync_sample_entries;
        Vector<SampleToChunkEntry> sample_to_chunk_entries;
        u32 sample_size;
        Vector<u32> sample_size_entries;
        Vector<u32> chunk_offset_entries;
        u32 sample_count;
        u32 chunk_count;
        u32 time_scale;
        Vector<u32> first_sample_index_in_chunk;
    };

    bool parse_atom();
    bool parse_mvhd();
    bool parse_trak();
    bool parse_tkhd();
    bool parse_mdhd();
    bool parse_hdlr();
    bool parse_stsd();
    bool parse_stts();
    bool parse_stss();
    bool parse_stsc();
    bool parse_stsz();
    bool parse_stco();

    // FIXME: Only handles single tracks for now
    const Track* video_track() const;
    const Track* audio_track() const;

    void create_video_decoder();
    void create_audio_decoder();
    u32 sample_index_at_time(const Track*, u32) const;
    u32 chunk_index_for_sample(const Track*, u32) const;
    const SampleToChunkEntry& chunk_entry_for_sample(const Track*, u32) const;
    u32 offset_for_chunk(const Track*, u32) const;
    u32 first_sample_in_chunk(const Track*, u32) const;
    u32 sample_offset_in_chunk(const Track*, u32 chunk, u32 sample) const;
    u32 size_for_sample(const Track*, u32) const;

    NonnullRefPtr<MappedFile> m_file;
    OwnPtr<InputMemoryStream> m_stream;
    String m_error_string;

    u32 m_duration = 0;
    u32 m_time_scale = 0;
    Vector<Track> m_tracks;
    RefPtr<VideoDecoder> m_video_decoder;
    RefPtr<AudioDecoder> m_audio_decoder;
};

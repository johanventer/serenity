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

#include "MOVFile.h"
#include "RLEVideoDecoder.h"
#include "StreamUtils.h"
#include "TwosAudioDecoder.h"

// #define DEBUG_MOV

MOVFile::MOVFile(const StringView& path)
    : m_file(MappedFile::map(path).release_value())
{
    // FIXME: Deal with bad / non-existent files

    InputMemoryStream stream { { (const u8*)m_file->data(), m_file->size() } };
    m_stream = make<InputMemoryStream>(stream);

    while (parse_atom()) { }

    if (m_tracks.is_empty()) {
        m_error_string = "No tracks in video file";
        return;
    }

    create_video_decoder();
    create_audio_decoder();
}

bool MOVFile::parse_atom()
{
    if (m_stream->eof())
        return false;

    bool is_container = false;
    size_t offset = m_stream->offset();
    u32 size = read_be<u32>(*m_stream);
    u32 type = read<u32>(*m_stream);

#ifdef DEBUG_MOV
    StringView type_str { reinterpret_cast<const char*>(&type), 4 };
    dbgln("MOVFile: {} [offset: {}, size: {}]", type_str, offset, size);
#endif

    switch (type) {
    case ATOM_TYPE('m', 'o', 'o', 'v'):
        is_container = true;
        break;
    case ATOM_TYPE('m', 'v', 'h', 'd'):
        if (!parse_mvhd())
            return false;
        break;
    case ATOM_TYPE('t', 'r', 'a', 'k'):
        is_container = true;
        if (!parse_trak())
            return false;
        break;
    case ATOM_TYPE('t', 'k', 'h', 'd'):
        if (!parse_tkhd())
            return false;
        break;
    case ATOM_TYPE('m', 'd', 'i', 'a'):
        is_container = true;
        break;
    case ATOM_TYPE('m', 'd', 'h', 'd'):
        if (!parse_mdhd())
            return false;
        break;
    case ATOM_TYPE('h', 'd', 'l', 'r'):
        if (!parse_hdlr())
            return false;
        break;
    case ATOM_TYPE('m', 'i', 'n', 'f'):
        is_container = true;
        break;
    case ATOM_TYPE('s', 't', 'b', 'l'):
        is_container = true;
        break;
    case ATOM_TYPE('s', 't', 's', 'd'):
        if (!parse_stsd())
            return false;
        break;
    case ATOM_TYPE('s', 't', 't', 's'):
        if (!parse_stts())
            return false;
        break;
    case ATOM_TYPE('s', 't', 's', 's'):
        if (!parse_stss())
            return false;
        break;
    case ATOM_TYPE('s', 't', 's', 'c'):
        if (!parse_stsc())
            return false;
        break;
    case ATOM_TYPE('s', 't', 's', 'z'):
        if (!parse_stsz())
            return false;
        break;
    case ATOM_TYPE('s', 't', 'c', 'o'):
        if (!parse_stco())
            return false;
        break;
    }

    if (is_container) {
        while (!m_stream->eof() && m_stream->offset() <= offset + size) {
            if (!parse_atom())
                return false;
        }
    } else {
        // Advance the stream past the atom data
        if (size == 0) {
            m_stream->seek_to_end();
        } else if (size == 1) {
            // FIXME: Support for extended size attribute
            ASSERT_NOT_REACHED();
        } else {
            if (offset + size < m_stream->bytes().size()) {
                m_stream->seek(offset + size);
            } else {
                m_stream->seek_to_end();
            }
        }
    }

    return true;
}

bool MOVFile::parse_mvhd()
{
    skip_bytes(*m_stream, 1); // Version
    skip_bytes(*m_stream, 3); // Flags
    skip_bytes(*m_stream, 4); // Creation time
    skip_bytes(*m_stream, 4); // Modification time
    m_time_scale = read_be<u32>(*m_stream);
    m_duration = read_be<u32>(*m_stream);
    skip_bytes(*m_stream, 4);  // Preferred rate
    skip_bytes(*m_stream, 2);  // Preferred volume
    skip_bytes(*m_stream, 10); // Reserved
    skip_bytes(*m_stream, 36); // Matrix structure
    skip_bytes(*m_stream, 4);  // Preview time
    skip_bytes(*m_stream, 4);  // Preview duration
    skip_bytes(*m_stream, 4);  // Poster time
    skip_bytes(*m_stream, 4);  // Selection time
    skip_bytes(*m_stream, 4);  // Selection duration
    skip_bytes(*m_stream, 4);  // Current time
    skip_bytes(*m_stream, 4);  // Next track ID

#ifdef DEBUG_MOV
    dbgln("MOVFile:  time_scale: {}, duration: {}", m_time_scale, m_duration);
#endif

    return true;
}

bool MOVFile::parse_trak()
{
    m_tracks.append(Track {});
    return true;
}

bool MOVFile::parse_tkhd()
{
    if (m_tracks.is_empty()) {
        m_error_string = "tkhd not inside a trak";
        return false;
    }

    Track& track = m_tracks.last();

    skip_bytes(*m_stream, 1); // Version
    skip_bytes(*m_stream, 3); // Flags
    skip_bytes(*m_stream, 4); // Creation time
    skip_bytes(*m_stream, 4); // Modification time
    skip_bytes(*m_stream, 4); // Track ID
    skip_bytes(*m_stream, 4); // Reserved
    track.duration = read_be<u32>(*m_stream);
    skip_bytes(*m_stream, 8);  // Reserved
    skip_bytes(*m_stream, 2);  // Layer
    skip_bytes(*m_stream, 2);  // Alternate group
    skip_bytes(*m_stream, 2);  // Volume
    skip_bytes(*m_stream, 2);  // Reserved
    skip_bytes(*m_stream, 36); // Matrix structure
    track.width = read_be<u32>(*m_stream) >> 16;
    track.height = read_be<u32>(*m_stream) >> 16;

#ifdef DEBUG_MOV
    dbgln("MOVFile:  duration: {}, width: {}, height: {}", track.duration, track.width, track.height);
#endif

    return true;
}

bool MOVFile::parse_mdhd()
{
    if (m_tracks.is_empty()) {
        m_error_string = "mdhd not inside a trak";
        return false;
    }

    Track& track = m_tracks.last();

    skip_bytes(*m_stream, 1); // Version
    skip_bytes(*m_stream, 3); // Flags
    skip_bytes(*m_stream, 4); // Creation time
    skip_bytes(*m_stream, 4); // Modification time
    track.time_scale = read_be<u32>(*m_stream);
    skip_bytes(*m_stream, 4); // Duration
    skip_bytes(*m_stream, 2); // Language
    skip_bytes(*m_stream, 2); // Quality

#ifdef DEBUG_MOV
    dbgln("MOVFile:  time_scale: {}", track.time_scale);
#endif

    return true;
}

bool MOVFile::parse_hdlr()
{
    if (m_tracks.is_empty()) {
        m_error_string = "hdlr not inside a trak";
        return false;
    }

    Track& track = m_tracks.last();

    skip_bytes(*m_stream, 1); // Version
    skip_bytes(*m_stream, 3); // Flags

    u32 component_type = read<u32>(*m_stream);
    u32 component_sub_type = read<u32>(*m_stream);

#ifdef DEBUG_MOV
    StringView component_type_str = { reinterpret_cast<const char*>(&component_type), 4 };
    StringView component_sub_type_str = { reinterpret_cast<const char*>(&component_sub_type), 4 };
    dbgln("MOVFile:  component_type: {}, component_sub_type: {}", component_type_str, component_sub_type_str);
#endif

    if (component_type == ATOM_TYPE('m', 'h', 'l', 'r')) {
        if (component_sub_type == ATOM_TYPE('v', 'i', 'd', 'e')) {
            track.type = TrackType::Video;
            return true;
        } else if (component_sub_type == ATOM_TYPE('s', 'o', 'u', 'n')) {
            track.type = TrackType::Audio;
            return true;
        }

        m_error_string = "Unsupported media handler";
        return false;
    }

    // FIXME: component_type = 'dhlr' data handler atom

    return true;
}

bool MOVFile::parse_stsd()
{
    if (m_tracks.is_empty()) {
        m_error_string = "stsd not inside a trak";
        return false;
    }

    Track& track = m_tracks.last();

    skip_bytes(*m_stream, 1); // Version
    skip_bytes(*m_stream, 3); // Flags

    u32 entries = read_be<u32>(*m_stream);

    if (entries != 1) {
        m_error_string = "Only a single sample description entry is supported in a track";
        return false;
    }

    SampleDescriptionEntry& entry = track.sample_description;
    entry.size = read_be<u32>(*m_stream);
    entry.format = read<u32>(*m_stream);
    skip_bytes(*m_stream, 6); // Reserved
    entry.reference_index = read<u16>(*m_stream);

#ifdef DEBUG_MOV
    StringView format_str { reinterpret_cast<const char*>(&entry.format), 4 };
#endif

    switch (track.type) {
    case TrackType::Video:
        skip_bytes(*m_stream, 2); // Version
        skip_bytes(*m_stream, 2); // Revision level
        skip_bytes(*m_stream, 4); // Vendor
        skip_bytes(*m_stream, 4); // Temporal quality
        skip_bytes(*m_stream, 4); // Spatial quality
        entry.width = read_be<u16>(*m_stream);
        entry.height = read_be<u16>(*m_stream);
        skip_bytes(*m_stream, 4); // Horizontal resolution
        skip_bytes(*m_stream, 4); // Vertical resolution
        skip_bytes(*m_stream, 4); // Data size
        entry.frames_per_sample = read_be<u16>(*m_stream);
        skip_bytes(*m_stream, 32); // Compressor name
        entry.depth = read_be<u16>(*m_stream);
        skip_bytes(*m_stream, 2); // Color table ID

        if (entry.frames_per_sample != 1) {
            m_error_string = "Only frames_per_sample = 1 is supported";
            return false;
        }

#ifdef DEBUG_MOV
        dbgln("MOVFile:  format: {}, width: {}, height: {}, depth: {}, frames_per_sample: {}",
            format_str, entry.width, entry.height, entry.depth, entry.frames_per_sample);
#endif
        break;

    case TrackType::Audio:
        entry.version = read_be<u16>(*m_stream);
        skip_bytes(*m_stream, 2); // Revision level
        skip_bytes(*m_stream, 4); // Vendor
        entry.channels = read_be<u16>(*m_stream);
        entry.sample_size = read_be<u16>(*m_stream);
        skip_bytes(*m_stream, 2); // Compression ID
        skip_bytes(*m_stream, 2); // Packet size
        entry.sample_rate = read_be<u32>(*m_stream) >> 16;
        entry.samples_per_packet = entry.version == 1 ? read_be<u32>(*m_stream) : 0;
        entry.bytes_per_packet = entry.version == 1 ? read_be<u32>(*m_stream) : 0;
        entry.bytes_per_frame = entry.version == 1 ? read_be<u32>(*m_stream) : 0;
        entry.bytes_per_sample = entry.version == 1 ? read_be<u32>(*m_stream) : 0;

#ifdef DEBUG_MOV
        dbgln("MOVFile:  format: {}, version: {}, channels: {}, sample_size: {}, sample_rate: {}, samples/packet: {}, bytes/packet: {}, bytes/frame: {}, bytes/sample: {}",
            format_str, entry.version, entry.channels, entry.sample_size, entry.sample_rate, entry.samples_per_packet, entry.bytes_per_packet, entry.bytes_per_frame, entry.bytes_per_sample);
#endif

        break;
    }

    return true;
}

bool MOVFile::parse_stts()
{
    if (m_tracks.is_empty()) {
        m_error_string = "stts not inside a trak";
        return false;
    }

    Track& track = m_tracks.last();

    skip_bytes(*m_stream, 1); // Version
    skip_bytes(*m_stream, 3); // Flags

    u32 entries = read_be<u32>(*m_stream);

    if (!entries) {
        m_error_string = "Invalid stts";
        return false;
    }

    track.time_to_sample_entries.ensure_capacity(entries);
    track.sample_count = 0;

    for (u32 i = 0; i < entries; i++) {
        u32 sample_count = read_be<u32>(*m_stream);
        u32 sample_duration = read_be<u32>(*m_stream);
        TimeToSampleEntry entry = {
            sample_count,
            sample_duration,
        };
        track.time_to_sample_entries.append(entry);
        track.sample_count += sample_count;

#ifdef DEBUG_MOV
        dbgln("MOVFile:  sample_count: {}, sample_duration: {}", entry.sample_count, entry.sample_duration);
#endif
    }

    return true;
}

bool MOVFile::parse_stss()
{
    if (m_tracks.is_empty()) {
        m_error_string = "stss not inside a trak";
        return false;
    }

    Track& track = m_tracks.last();

    skip_bytes(*m_stream, 1); // Version
    skip_bytes(*m_stream, 3); // Flags

    u32 entries = read_be<u32>(*m_stream);

    if (!entries) {
        m_error_string = "Invalid stss";
        return false;
    }

    track.sync_sample_entries.ensure_capacity(entries);

    for (u32 i = 0; i < entries; i++)
        track.sync_sample_entries.append(read_be<u32>(*m_stream));

    return true;
}

bool MOVFile::parse_stsc()
{
    if (m_tracks.is_empty()) {
        m_error_string = "stsc not inside a trak";
        return false;
    }

    Track& track = m_tracks.last();

    skip_bytes(*m_stream, 1); // Version
    skip_bytes(*m_stream, 3); // Flags

    u32 entries = read_be<u32>(*m_stream);

    if (!entries) {
        m_error_string = "Invalid stsc";
        return false;
    }

    track.sample_to_chunk_entries.ensure_capacity(entries);

    for (u32 i = 0; i < entries; i++) {
        SampleToChunkEntry entry = {
            read_be<u32>(*m_stream) - 1,
            read_be<u32>(*m_stream),
            read_be<u32>(*m_stream),
        };
        track.sample_to_chunk_entries.append(entry);

#ifdef DEBUG_MOV
        dbgln("MOVFile:  first_chunk: {}, samples_per_chunk: {}, sample_description_Id: {}",
            entry.first_chunk, entry.samples_per_chunk, entry.sample_description_id);
#endif
    }

    return true;
}

bool MOVFile::parse_stsz()
{
    if (m_tracks.is_empty()) {
        m_error_string = "stsz not inside a trak";
        return false;
    }

    Track& track = m_tracks.last();

    skip_bytes(*m_stream, 1); // Version
    skip_bytes(*m_stream, 3); // Flags

    track.sample_size = read_be<u32>(*m_stream);

    if (track.sample_size == 0) {
        u32 entries = read_be<u32>(*m_stream);

        if (!entries) {
            m_error_string = "Invalid stsc";
            return false;
        }

        track.sample_size_entries.ensure_capacity(entries);

        for (u32 i = 0; i < entries; i++) {
            u32 sample_size = read_be<u32>(*m_stream);
            track.sample_size_entries.append(sample_size);
        }
    }

    return true;
}

bool MOVFile::parse_stco()
{
    if (m_tracks.is_empty()) {
        m_error_string = "stco not inside a trak";
        return false;
    }

    Track& track = m_tracks.last();

    skip_bytes(*m_stream, 1); // Version
    skip_bytes(*m_stream, 3); // Flags

    u32 entries = read_be<u32>(*m_stream);

    if (!entries) {
        m_error_string = "Invalid stco";
        return false;
    }

    track.chunks.ensure_capacity(entries);

    for (u32 i = 0; i < entries; i++) {
        auto offset = read_be<u32>(*m_stream);
        track.chunks.append({ offset, 0, 0, 0 });
    }

    if (track.sample_to_chunk_entries.is_empty()) {
        m_error_string = "Expected stsc before stco";
        return false;
    }

    u32 chunk_index = 0;
    u32 sample_index = 0;

    for (size_t i = 0; i < track.sample_to_chunk_entries.size(); i++) {
        auto& entry = track.sample_to_chunk_entries[i];

        u32 chunk_count = i + 1 < track.sample_to_chunk_entries.size()
            ? track.sample_to_chunk_entries[i + 1].first_chunk - entry.first_chunk
            : track.chunks.size() - entry.first_chunk;

        while (chunk_count--) {
            track.chunks[chunk_index].sample_count = entry.samples_per_chunk;
            track.chunks[chunk_index].sample_description_id = entry.sample_description_id;
            track.chunks[chunk_index].first_sample_index = sample_index;
            chunk_index++;
            sample_index += entry.samples_per_chunk;
        }
    }

    return true;
}

const MOVFile::Track* MOVFile::video_track() const
{
    for (auto& track : m_tracks) {
        if (track.type == TrackType::Video)
            return &track;
    }

    return nullptr;
}

const MOVFile::Track* MOVFile::audio_track() const
{
    for (auto& track : m_tracks) {
        if (track.type == TrackType::Audio)
            return &track;
    }

    return nullptr;
}

Gfx::IntSize MOVFile::frame_size() const
{
    auto track = video_track();

    if (track)
        return { track->sample_description.width, track->sample_description.height };

    return { 0, 0 };
}

u16 MOVFile::depth() const
{
    auto track = video_track();

    if (track)
        return track->sample_description.depth;

    return 0;
}

u32 MOVFile::duration() const
{
    return (float)m_duration / (float)m_time_scale * 1000.f;
}

u32 MOVFile::frame_count() const
{
    auto track = video_track();

    if (track)
        return track->sample_description.frames_per_sample * track->sample_count;

    return 0;
}

u32 MOVFile::audio_sample_count() const
{
    auto track = audio_track();
    if (track)
        return track->sample_count;

    return 0;
}

u32 MOVFile::audio_samples_per_frame() const
{
    auto track = audio_track();
    if (track) {
        auto samples_per_ms = ceilf((float)track->sample_description.sample_rate / 1000.f);
        return ms_per_frame() * samples_per_ms;
    }

    return 0;
}

u32 MOVFile::audio_sample_size() const
{
    auto track = audio_track();
    if (track) {
        return track->sample_description.sample_size / 8 * track->sample_description.channels;
    }

    return 0;
}

u32 MOVFile::ms_per_frame() const
{
    return (float)duration() / (float)frame_count();
}

void MOVFile::create_video_decoder()
{
    auto track = video_track();

    if (track) {

        switch (track->sample_description.format) {
        case ATOM_TYPE('r', 'l', 'e', ' '):
            m_video_decoder = adopt(*new RLEVideoDecoder(frame_size(), depth(), Gfx::BitmapFormat::RGBA32));
            return;
        default:
#ifdef DEBUG_MOV
            StringView format_str { reinterpret_cast<const char*>(&track->sample_description.format), 4 };
            dbgln("MOVFile: Unknown video format: {}", format_str);
#endif
            m_error_string = "No decoder for video format";
        }
    }

    m_error_string = "Could not create video decoder";
}

void MOVFile::create_audio_decoder()
{
    auto track = audio_track();

    if (track) {
        switch (track->sample_description.format) {
        case ATOM_TYPE('t', 'w', 'o', 's'):
            m_audio_decoder = adopt(*new TwosAudioDecoder(track->sample_description.sample_size, track->sample_description.sample_rate, track->sample_description.channels));
            return;
#ifdef DEBUG_MOV
            StringView format_str { reinterpret_cast<const char*>(&track->sample_description.format), 4 };
            dbgln("MOVFile: Unknown audio format: {}", format_str);
#endif
            m_error_string = "No decoder for audio format";
        }
    }

    m_error_string = "Could not create audio decoder";
}

u32 MOVFile::sample_at_time(const Track* track, u32 time) const
{
    u32 total_time = 0;
    u32 sample_index = 0;

    for (auto& entry : track->time_to_sample_entries) {
        u32 current_time = total_time;
        total_time += entry.sample_count * entry.sample_duration;

        if (total_time >= time) {
            // The time we are looking for is in this entry
            for (u32 i = 0; i < entry.sample_count; i++) {
                if (current_time >= time) {
                    return sample_index;
                }
                current_time += entry.sample_duration;
                sample_index++;
            }
        }

        sample_index += entry.sample_count;
    }

    return UINT32_MAX;
}

const MOVFile::Chunk& MOVFile::chunk_for_sample(const Track& track, u32 sample_index) const
{
    ASSERT(track.chunks.size());
    ASSERT(sample_index < track.sample_count);

    u32 sample_count = 0;
    auto previous_chunk = &track.chunks[0];

    for (auto& chunk : track.chunks) {
        if (sample_count > sample_index)
            return *previous_chunk;
        previous_chunk = &chunk;
        sample_count += chunk.sample_count;
    }

    ASSERT_NOT_REACHED();
}

u32 MOVFile::sample_size(const Track& track, u32 sample_index) const
{
    if (track.sample_size) {
        // All the samples are the same size
        return track.sample_size;
    }

    ASSERT(sample_index < track.sample_size_entries.size());

    return track.sample_size_entries[sample_index];
}

RefPtr<Gfx::Bitmap> MOVFile::decode_frame(u32 frame)
{
    ASSERT(m_video_decoder);
    ASSERT(frame < frame_count());

    auto track = video_track();
    if (!track)
        return nullptr;

    auto& chunk = chunk_for_sample(*track, frame);
    auto sample_size = this->sample_size(*track, frame);
    auto offset_in_chunk = (frame - chunk.first_sample_index) * sample_size;
    auto offset_in_file = chunk.offset + offset_in_chunk;

    ASSERT(offset_in_file < m_file->size());

    auto span = Span<const u8> { (const u8*)m_file->data() + offset_in_file, sample_size };

    return m_video_decoder->decode(span);
}

Vector<Audio::Sample> MOVFile::decode_audio_samples(u32 first_sample_index, u32 max_samples)
{
    // dbgln("first_sample_index={}, max_samples={}", first_sample_index, max_samples);

    Vector<Audio::Sample> samples;

    auto track = audio_track();
    if (!track)
        return samples;

    ASSERT(first_sample_index < track->sample_count);

    auto samples_left = min(track->sample_count - first_sample_index, max_samples);
    auto sample_index = first_sample_index;

    // dbgln("track->sample_count={}, sample_count={}", track->sample_count, sample_count);

    samples.ensure_capacity(samples_left);

    while (samples_left) {
        auto& chunk = chunk_for_sample(*track, sample_index);
        auto sample_size = this->sample_size(*track, sample_index);
        auto relative_sample_index = sample_index - chunk.first_sample_index;
        auto samples_to_decode = min(samples_left, chunk.sample_count - relative_sample_index);
        auto offset_in_chunk = relative_sample_index * sample_size;
        auto offset_in_file = chunk.offset + offset_in_chunk;

        // dbgln("sample_size={}, relative_sample_index={}, chunk.first_sample_index={}, samples_to_decode={}, offset_in_chunk={}, offset_in_file={}",
        //     sample_size, relative_sample_index, chunk.first_sample_index, samples_to_decode, offset_in_chunk, offset_in_file);

        ASSERT(offset_in_file < m_file->size());

        m_audio_decoder->decode_samples((const u8*)m_file->data() + offset_in_file, samples_to_decode, samples);

        sample_index += samples_to_decode;
        samples_left -= samples_to_decode;

        // dbgln("sample_index={}, samples_left={}", sample_index, samples_left);
    }

    return samples;
}


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

#include "VideoWidget.h"
#include <LibCore/File.h>
#include <LibGUI/Painter.h>

#define VIDEOPLAYER_BUFFER_TIME 1500

//#include <LibGfx/BMPWriter.h>
// static void dump(RefPtr<Gfx::Bitmap> bitmap, u32 index)
// {
//     Gfx::BMPWriter dumper;
//     auto bmp = dumper.dump(bitmap);
//     auto path = String::formatted("frame{}.bmp", index);
//     auto file = fopen(path.characters(), "wb");
//     fwrite(bmp.data(), sizeof(u8), bmp.size(), file);
//     fclose(file);
// }

VideoWidget::VideoWidget(GUI::Window& window, NonnullRefPtr<Audio::ClientConnection> connection)
    : m_window(window)
    , m_connection(connection)
    , m_video_thread(LibThread::Thread::construct([this] { return video_thread(); }, "VideoPlayer[video]"))
    , m_frame_timer(Core::Timer::construct(0, [this]() { on_frame_timer(); }))
    , m_audio_timer(Core::Timer::construct(100, [this]() { on_audio_timer(); }))
    , m_seek_debounce_timer(Core::Timer::construct(0, [this]() { on_seek_debounce_timer(); }))

{
    set_fill_with_background_color(false);
    m_frame_timer->stop();
    m_audio_timer->stop();
    m_seek_debounce_timer->stop();
    m_video_thread->start();
}

VideoWidget::~VideoWidget()
{
    m_state = State::Teardown;
    (void)m_video_thread->join();
}

void VideoWidget::play()
{
    if (m_state != State::Stopped && m_state != State::Paused)
        return;

    m_state = State::Playing;
    m_frame_timer->start(m_file->ms_per_frame());
    m_audio_timer->start();
}

void VideoWidget::pause()
{
    if (m_state != State::Playing)
        return;

    m_frame_timer->stop();
    m_audio_timer->stop();
    m_state = State::Paused;
}

void VideoWidget::reset_buffers()
{
    // FIXME: This is stupid, perhaps replace with condvar or have the buffer loop spin up a new event loop to handle
    // commands, but for now it makes the thread synchronisation easier.
    while (!m_video_thread_waiting) { }

    m_next_frame_to_buffer = 0;
    m_buffer_percent = 0;
    m_initial_buffer_full = false;
    if (m_video_buffer)
        m_video_buffer->reset();
    // if (m_audio_buffer)
    //     m_audio_buffer->reset();
}

void VideoWidget::stop()
{
    if (m_state != State::Playing && m_state != State::Paused)
        return;

    m_frame_timer->stop();
    m_audio_timer->stop();
    m_state = State::Stopped;

    reset_buffers();

    m_last_frame = nullptr;
    m_played_frames = 0;
    m_elapsed_time = 0;

    update();
}

void VideoWidget::seek_to_frame(u32)
{
    // m_frame_to_seek_to = min(max(frame, 0u), m_file->frame_count() - 1);

    // m_frame_timer->stop();
    // m_state = State::Stopped;

    // reset_buffers();

    // // FIXME: This is the dumbest seeking ever, it doesn't know anything about keyframes
    // m_next_frame_to_buffer = m_frame_to_seek_to;
    // // FIXME: Audio seeking
    // m_played_frames = m_frame_to_seek_to;
    // m_elapsed_time = m_frame_to_seek_to * m_file->ms_per_frame();

    // m_seek_debounce_timer->stop();
    // m_seek_debounce_timer->start(500);
}

void VideoWidget::on_seek_debounce_timer()
{
    m_seek_debounce_timer->stop();
    play();
}

void VideoWidget::open_file(String path)
{
    if (m_file) {
        stop();
    }

    m_state = State::Stopped;
    m_file = adopt(*new MOVFile(path));

    if (m_file->has_error()) {
        // FIXME: Show message box
        dbgln("MOV file loading error: {}", m_file->error_string());
        return;
    }

    m_total_time = m_file->duration();

    auto buffer_frames = min(VIDEOPLAYER_BUFFER_TIME / m_file->ms_per_frame(), m_file->frame_count());

    dbgln("Ring buffers initialized for {} frames", buffer_frames);

    auto frame_pitch = Gfx::Bitmap::minimum_pitch(m_file->frame_size().width(), Gfx::BitmapFormat::RGBA32);
    auto frame_bytes = Gfx::Bitmap::size_in_bytes(frame_pitch, m_file->frame_size().height());
    m_video_buffer = new RingBuffer(buffer_frames, frame_bytes);
}

void VideoWidget::paint_event(GUI::PaintEvent& event)
{
    GUI::Widget::paint_event(event);

    GUI::Painter painter(*this);
    painter.add_clip_rect(event.rect());
    painter.add_clip_rect(rect());
    painter.fill_rect(rect(), Color::Black);

    if (m_file && m_last_frame) {
        auto src_size = m_file->frame_size();
        Gfx::IntRect src_rect = { 0, 0, src_size.width(), src_size.height() };
        Gfx::IntRect dst_rect = rect();

        auto src_aspect = (float)src_rect.width() / (float)src_rect.height();
        auto dst_aspect = (float)dst_rect.width() / (float)dst_rect.height();
        auto width = 0.0f, height = 0.0f;

        if (src_aspect > dst_aspect) {
            height = (float)dst_rect.width() / src_aspect;
            width = src_aspect * height;
        } else {
            width = src_aspect * (float)dst_rect.height();
            height = width / src_aspect;
        }

        auto left = max(0.0f, dst_rect.width() - width) / 2.0f + dst_rect.left();
        auto top = max(0.0f, dst_rect.height() - height) / 2.0f + dst_rect.top();
        dst_rect = { (int)left, (int)top, (int)width, (int)height };

        painter.draw_scaled_bitmap(dst_rect, *m_last_frame, src_rect);
    }
}

void VideoWidget::on_frame_timer()
{
    if (m_played_frames >= m_file->frame_count()) {
        stop();
        if (on_finished)
            on_finished();
    }

    // Wait for a full buffer before playing anything
    if (!m_initial_buffer_full) {
        m_initial_buffer_full = m_video_buffer->size() == m_video_buffer->capacity();
        if (!m_initial_buffer_full)
            return;
    }

    auto frame_data = const_cast<u8*>(m_video_buffer->try_peek());
    if (!frame_data) {
        // Buffer is empty, we've fallen behind :(
        m_initial_buffer_full = false;
        return;
    }

    auto frame_pitch = Gfx::Bitmap::minimum_pitch(m_file->frame_size().width(), Gfx::BitmapFormat::RGBA32);
    auto frame = Gfx::Bitmap::create_wrapper(Gfx::BitmapFormat::RGBA32, m_file->frame_size(), 1, frame_pitch, frame_data);

    // Clone the frame here so that the buffer can be moved on and we have a record of what the last
    // frame was. We have no guarantee in what order the paint_event will get fired in the run loop so can't
    // hang on to a pointer to data in the buffer.
    if (!m_last_frame) {
        m_last_frame = frame->clone();
    } else {
        // Fast path, doesn't cause another allocation if we already have a last frame Bitmap
        ASSERT(m_last_frame->size_in_bytes() == frame->size_in_bytes());
        memcpy(m_last_frame->scanline(0), frame->scanline(0), frame->size_in_bytes());
    }
    m_video_buffer->pop();

    // auto sample_data = reinterpret_cast<const Audio::Sample*>(m_audio_buffer->try_peek());
    // if (sample_data) {
    //     Vector<Audio::Sample> samples;
    //     samples.ensure_capacity(m_file->audio_samples_per_frame());
    //     for (u32 i = 0; i < m_file->audio_samples_per_frame(); i++)
    //         samples.append(sample_data[i]);
    //     auto buffer = Audio::Buffer::create_with_samples(move(samples));
    //     m_connection->try_enqueue(*buffer);
    //     m_audio_buffer->pop();
    // }

    // int id = m_connection->get_playing_buffer();
    // dbgln("playing buffer: {}", id);

    // int current_id = -1;
    // if (m_current_audio_buffer) {
    //     current_id = m_current_audio_buffer->id();
    //     dbgln("had a current buffer, it's id is {}", current_id);
    // }

    // if (id >= 0 && id != current_id) {
    //     while (!m_audio_buffers.is_empty()) {
    //         LOCKER(m_audio_buffers_lock);
    //         --m_next_audio_buffer_ptr;
    //         auto buffer = m_audio_buffers.take_first();

    //         if (buffer->id() == id) {
    //             m_current_audio_buffer = buffer;
    //             dbgln("setting current audio buffer");
    //             break;
    //         }
    //     }
    // }

    // if (m_next_audio_buffer) {
    //     dbgln("m_next_audio_buffer size_in_bytes={}, sample_count={}", m_next_audio_buffer->size_in_bytes(), m_next_audio_buffer->sample_count());
    //     m_connection->try_enqueue(*m_next_audio_buffer);
    // }

    m_played_frames++;
    m_elapsed_time += m_file->ms_per_frame();

    update();
}

int VideoWidget::video_thread()
{
    for (;;) {
        auto current_state = m_state.load();

        switch (current_state) {
        case State::Teardown:
            return 0;
        case State::Stopped:
            m_video_thread_waiting = true;
            break;
        case State::Playing:
        case State::Paused:
            m_video_thread_waiting = false;
            while (!m_video_buffer->is_full() && (current_state == State::Playing || current_state == State::Paused)) {
                auto frame = m_file->decode_frame(m_next_frame_to_buffer);
                if (frame) {
                    m_video_buffer->push(frame->scanline_u8(0));
                    m_next_frame_to_buffer++;
                    m_buffer_percent = (float)m_video_buffer->size() / (float)m_video_buffer->capacity() * 100.f;
                }
                current_state = m_state.load();
            }
            break;
        }

        // FIXME: I hate this
        usleep(1000);
    }
}

void VideoWidget::on_audio_timer()
{
    static u32 next_sample = 0;
    static u32 samples_per_tick = 4410;

    if (!m_file)
        return;

    if (next_sample >= m_file->audio_sample_count())
        return;

    auto samples = m_file->decode_audio_samples(next_sample, samples_per_tick);
    next_sample += samples.size();
    auto buffer = Audio::Buffer::create_with_samples(move(samples));
    m_connection->try_enqueue(*buffer);
}

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

#include "LibAV/MOVFile.h"
#include "LibAV/RingBuffer.h"
#include <AK/Atomic.h>
#include <AK/Function.h>
#include <LibAudio/ClientConnection.h>
#include <LibCore/ElapsedTimer.h>
#include <LibCore/Timer.h>
#include <LibGUI/Widget.h>
#include <LibGUI/Window.h>
#include <LibThread/Lock.h>
#include <LibThread/Thread.h>

class VideoWidget final : public GUI::Widget {
    C_OBJECT(VideoWidget)
public:
    enum class State {
        Stopped,
        Playing,
        Paused,
        Teardown
    };

    virtual ~VideoWidget() override;

    void open_file(String path);

    void play();
    void stop();
    void pause();
    void seek_to_frame(u32 frame);

    State state() const { return m_state; }
    bool has_file_loaded() const { return m_file != nullptr; }
    u32 elapsed_time() const { return m_elapsed_time; }
    u32 elapsed_frames() const { return m_played_frames; }
    u32 total_time() const { return m_total_time; }
    u32 buffer_percent() const { return m_buffer_percent; }
    u32 ms_per_frame() const { return m_file ? m_file->ms_per_frame() : 0; }
    u32 frame_count() const { return m_file ? m_file->frame_count() : 0; }
    Gfx::IntSize frame_size() const { return m_file ? m_file->frame_size() : Gfx::IntSize { 0, 0 }; }

    Function<void()> on_finished;

private:
    explicit VideoWidget(GUI::Window&, NonnullRefPtr<Audio::ClientConnection>);

    virtual void paint_event(GUI::PaintEvent&) override;

    void on_frame_timer();
    void on_update_timer();
    void on_seek_debounce_timer();
    int buffer_thread();
    void reset_buffers();

    GUI::Window& m_window;
    NonnullRefPtr<Audio::ClientConnection> m_connection;

    Atomic<State> m_state { State::Stopped };
    u32 m_initial_buffer_full { 0 };
    Atomic<u32> m_buffer_percent { 0 };
    u32 m_played_frames { 0 };
    u32 m_played_samples { 0 };
    u32 m_next_frame_to_buffer { 0 };
    u32 m_next_sample_to_buffer { 0 };
    u32 m_elapsed_time { 0 };
    u32 m_total_time { 0 };
    u32 m_frame_to_seek_to { 0 };

    RefPtr<MOVFile> m_file { nullptr };
    RefPtr<Gfx::Bitmap> m_last_frame { nullptr };
    OwnPtr<RingBuffer> m_video_buffer { nullptr };
    OwnPtr<RingBuffer> m_audio_buffer { nullptr };

    NonnullRefPtr<LibThread::Thread> m_buffer_thread;
    Atomic<bool> m_buffer_thread_waiting { true };

    RefPtr<Core::Timer> m_frame_timer;
    RefPtr<Core::Timer> m_seek_debounce_timer;
};

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

#include "VideoWidget.h"
#include <LibAudio/ClientConnection.h>
#include <LibGUI/Button.h>
#include <LibGUI/Label.h>
#include <LibGUI/Slider.h>
#include <LibGUI/StatusBar.h>
#include <LibGUI/Widget.h>
#include <LibGUI/Window.h>

class VideoPlayerWidget final : public GUI::Widget {
    C_OBJECT(VideoPlayerWidget)
public:
    virtual ~VideoPlayerWidget() = default;

    void open_file(String path);
    void play();
    void pause();
    void toggle_play();
    void stop();

private:
    explicit VideoPlayerWidget(GUI::Window&, NonnullRefPtr<Audio::ClientConnection>);

    virtual void timer_event(Core::TimerEvent&) override;
    virtual void keyup_event(GUI::KeyEvent&) override;

    class Slider final : public GUI::Slider {
        C_OBJECT(Slider)
    public:
        virtual ~Slider() override = default;
        Function<void(int)> on_interaction_change;

        bool interacting() const { return m_interacting; }

        void set_enabled(bool enabled)
        {
            if (!enabled)
                m_interacting = false;
            GUI::Slider::set_enabled(enabled);
        }

        void set_value(int value)
        {
            if (!interacting())
                GUI::Slider::set_value(value);
        }

    private:
        Slider(Orientation orientation)
            : GUI::Slider(orientation)
        {
        }

        virtual void mousedown_event(GUI::MouseEvent& event) override
        {
            auto current_value = value();
            m_interacting = true;
            GUI::Slider::mousedown_event(event);
            if (current_value != value() && on_interaction_change)
                on_interaction_change(value());
        }

        virtual void mouseup_event(GUI::MouseEvent& event) override
        {
            GUI::Slider::mouseup_event(event);
            m_interacting = false;
        }

        virtual void mousemove_event(GUI::MouseEvent& event) override
        {
            auto current_value = value();
            GUI::Slider::mousemove_event(event);
            if (knob_dragging() && current_value != value() && on_interaction_change)
                on_interaction_change(value());
        }

        virtual void mousewheel_event(GUI::MouseEvent& event) override
        {
            auto current_value = value();
            GUI::Slider::mousewheel_event(event);
            if (current_value != value() && on_interaction_change)
                on_interaction_change(value());
        }

        bool m_interacting { false };
    };

    void update_time();
    void update_status();

    GUI::Window& m_window;
    NonnullRefPtr<Audio::ClientConnection> m_connection;

    RefPtr<VideoWidget> m_video_widget;
    RefPtr<GUI::Widget> m_transport_container;
    RefPtr<GUI::Button> m_play_button;
    RefPtr<GUI::Button> m_stop_button;
    RefPtr<Slider> m_slider;
    RefPtr<GUI::Label> m_timer_label;
    RefPtr<GUI::StatusBar> m_status_bar;
    RefPtr<Gfx::Bitmap> m_play_icon { Gfx::Bitmap::load_from_file("/res/icons/16x16/play.png") };
    RefPtr<Gfx::Bitmap> m_pause_icon { Gfx::Bitmap::load_from_file("/res/icons/16x16/pause.png") };
};

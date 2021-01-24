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

#include "VideoPlayerWidget.h"
#include "VideoWidget.h"
#include <AK/StringBuilder.h>
#include <LibGUI/BoxLayout.h>
#include <LibGUI/MessageBox.h>
#include <math.h>

VideoPlayerWidget::VideoPlayerWidget(GUI::Window& window, NonnullRefPtr<Audio::ClientConnection> connection)
    : m_window(window)
    , m_connection(connection)
{
    set_fill_with_background_color(false);
    set_layout<GUI::VerticalBoxLayout>();
    layout()->set_spacing(0);

    m_video_widget = add<VideoWidget>(window, connection);
    m_video_widget->on_finished = [this]() { stop(); };

    m_transport_container = add<GUI::Widget>();
    m_transport_container->set_fill_with_background_color(true);
    m_transport_container->set_layout<GUI::HorizontalBoxLayout>();
    m_transport_container->set_fixed_height(30);
    m_transport_container->layout()->set_margins({ 2, 0, 2, 0 });

    auto& button_container = m_transport_container->add<GUI::Widget>();
    button_container.set_fixed_width(60);
    button_container.set_fixed_height(30);
    button_container.set_fill_with_background_color(true);
    button_container.set_layout<GUI::HorizontalBoxLayout>();

    m_play_button = button_container.add<GUI::Button>();
    m_play_button->set_enabled(false);
    m_play_button->set_focus_policy(GUI::FocusPolicy::NoFocus);
    m_play_button->set_icon(*m_play_icon);
    m_play_button->set_fixed_width(26);
    m_play_button->on_click = [this](auto) {
        toggle_play();
    };

    m_stop_button = button_container.add<GUI::Button>();
    m_stop_button->set_enabled(false);
    m_stop_button->set_focus_policy(GUI::FocusPolicy::NoFocus);
    m_stop_button->set_icon(Gfx::Bitmap::load_from_file("/res/icons/16x16/stop.png"));
    m_stop_button->set_fixed_width(26);
    m_stop_button->on_click = [this](auto) {
        stop();
    };

    m_slider = m_transport_container->add<Slider>(Gfx::Orientation::Horizontal);
    m_slider->set_min(0);
    m_slider->set_enabled(true);
    m_slider->on_interaction_change = [this](int value) {
        (void)this;
        (void)value;
        //m_video_widget->seek_to_frame(value);
    };

    m_timer_label = m_transport_container->add<GUI::Label>();
    m_timer_label->set_fixed_width(45);
    m_timer_label->set_text_alignment(Gfx::TextAlignment::CenterRight);
    m_timer_label->set_text("-:--");

    m_status_bar = add<GUI::StatusBar>();
}

void VideoPlayerWidget::open_file(String path)
{
    stop_timer();
    m_play_button->set_icon(*m_play_icon);
    m_play_button->set_enabled(false);
    m_stop_button->set_enabled(false);

    m_video_widget->open_file(path);
    // FIXME: Check for errors

    m_play_button->set_enabled(true);
    m_slider->set_max(m_video_widget->frame_count());

    play();
}

void VideoPlayerWidget::play()
{
    if (m_video_widget->state() == VideoWidget::State::Playing || !m_video_widget->has_file_loaded())
        return;

    m_video_widget->play();
    m_play_button->set_icon(*m_pause_icon);
    m_stop_button->set_enabled(true);
    m_slider->set_enabled(true);
    start_timer(100);
}

void VideoPlayerWidget::pause()
{
    if (m_video_widget->state() == VideoWidget::State::Paused || !m_video_widget->has_file_loaded())
        return;

    stop_timer();
    m_video_widget->pause();
    m_play_button->set_icon(*m_play_icon);
    m_stop_button->set_enabled(true);
}

void VideoPlayerWidget::toggle_play()
{
    switch (m_video_widget->state()) {
    case VideoWidget::State::Playing:
        pause();
        break;
    case VideoWidget::State::Stopped:
    case VideoWidget::State::Paused:
        play();
        break;
    default:
        break;
    }
}

void VideoPlayerWidget::stop()
{
    if (!m_video_widget->has_file_loaded())
        return;

    stop_timer();
    m_video_widget->stop();
    m_stop_button->set_enabled(false);
    m_play_button->set_icon(*m_play_icon);
    m_slider->set_enabled(false);

    update_time();
    update_status();
}

void VideoPlayerWidget::update_time()
{
    auto elapsed = m_video_widget->elapsed_time();
    if (elapsed > 3600000) {
        auto hours = elapsed / 3600000;
        auto minutes = (elapsed - hours * 3600000) / 60000;
        auto seconds = (elapsed - hours * 3600000 - minutes * 60000) / 1000;
        m_timer_label->set_text(String::formatted("{}:{:#02}:{:#02}", hours, minutes, seconds));
    } else {
        auto minutes = elapsed / 60000;
        auto seconds = (elapsed - minutes * 60000) / 1000;
        m_timer_label->set_text(String::formatted("{}:{:#02}", minutes, seconds));
    }

    m_slider->set_value(m_video_widget->elapsed_frames());
}

void VideoPlayerWidget::timer_event(Core::TimerEvent&)
{
    update_time();
    update_status();
}

void VideoPlayerWidget::update_status()
{
    if (!m_video_widget->has_file_loaded()) {
        m_status_bar->set_text("No video loaded.");
        return;
    }

    m_status_bar->set_text(String::formatted("Video: {}x{}, {}fps, {}% buffered",
        m_video_widget->frame_size().width(),
        m_video_widget->frame_size().height(),
        (int)(1000.f / (float)m_video_widget->ms_per_frame()),
        m_video_widget->buffer_percent()));
}

void VideoPlayerWidget::keyup_event(GUI::KeyEvent& event)
{
    switch (event.key()) {
    case Key_Space:
        toggle_play();
        break;
    case Key_Escape:
        stop();
        break;
    case Key_F:
        // FIXME: The window doesn't redraw in fullscreen, why??
        m_window.set_fullscreen(!m_window.is_fullscreen());
        m_status_bar->set_visible(!m_window.is_fullscreen());
        m_transport_container->set_visible(!m_window.is_fullscreen());
        break;
    default:
        break;
    }
}

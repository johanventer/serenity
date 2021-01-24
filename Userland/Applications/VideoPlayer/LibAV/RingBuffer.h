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

#include <AK/Atomic.h>
#include <AK/ByteBuffer.h>
#include <AK/OwnPtr.h>
#include <AK/RefPtr.h>
#include <LibThread/Lock.h>
#include <string.h>

class RingBuffer {

public:
    explicit RingBuffer(size_t capacity, size_t step)
        : m_capacity(capacity)
        , m_step(step)
        , m_data(new u8[capacity * step]) {};

    RingBuffer() = default;
    ~RingBuffer() = default;

    const u8* try_peek() const
    {
        if (is_empty())
            return nullptr;
        return m_data + m_read * m_step;
    }

    void pop()
    {
        if (is_empty())
            return;

        LOCKER(m_lock);
        m_full = false;
        m_read.store((m_read + 1) % m_capacity);
    }

    void push(u8* bytes)
    {
        if (m_full) {
            ASSERT_NOT_REACHED();
        }
        LOCKER(m_lock);
        memcpy(m_data.ptr() + m_write * m_step, bytes, m_step);
        m_write.store((m_write + 1) % m_capacity);
        m_full = m_write == m_read;
    }

    void reset()
    {
        LOCKER(m_lock);
        m_full = false;
        m_write.store(m_read);
    }

    bool is_empty() const { return !m_full && m_write == m_read; }
    bool is_full() const { return m_full; }
    size_t capacity() const { return m_capacity; }

    size_t size() const
    {
        size_t size = m_capacity;

        if (!m_full) {
            if (m_write >= m_read) {
                size = m_write - m_read;
            } else {
                size = m_capacity + m_write - m_read;
            }
        }

        return size;
    }

private:
    size_t m_capacity { 0 };
    size_t m_step { 0 };
    OwnPtr<u8> m_data { nullptr };

    LibThread::Lock m_lock;
    Atomic<bool> m_full { false };
    Atomic<size_t> m_read { 0 };
    Atomic<size_t> m_write { 0 };
};

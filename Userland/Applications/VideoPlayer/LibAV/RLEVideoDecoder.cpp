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

#include "RLEVideoDecoder.h"
#include "StreamUtils.h"
#include <AK/ByteBuffer.h>
#include <AK/MemoryStream.h>

// #define DEBUG_RLE

RLEVideoDecoder::RLEVideoDecoder(Gfx::IntSize frame_size, int depth, Gfx::BitmapFormat format)
    : VideoDecoder(frame_size, depth, format)
    , m_last_frame(Gfx::Bitmap::create(format, frame_size))
{
    // FIXME: Only supports 32bit output and 24bit input for now
    ASSERT(format == Gfx::BitmapFormat::RGBA32);
    ASSERT(depth == 24);

    m_last_frame->fill({ 0, 0, 0, 0 });
};

RefPtr<Gfx::Bitmap> RLEVideoDecoder::decode(Span<const u8> span)
{
    auto stream = InputMemoryStream { span };
    auto bitmap = m_last_frame->clone();

    u32 chunk_size = read_be<u32>(stream) & 0x3FFFFFFF;
    if (chunk_size < 8) {
        return bitmap;
    }

    u16 header = read_be<u16>(stream);
    u16 start_line = 0;
    u16 lines = m_frame_size.height();

    if (header & 0x0008) {
        start_line = read_be<u16>(stream);
        skip_bytes(stream, 2);
        lines = read_be<u16>(stream);
        skip_bytes(stream, 2);
    }

#ifdef DEBUG_RLE
    dbgln("RLEVideoDecoder: chunk_size: {}, header: {}, start_line: {}, lines: {}", chunk_size, header, start_line, lines);
#endif

    auto row_ptr = bitmap->scanline(start_line);
    auto row_advance = bitmap->pitch() / sizeof(Gfx::RGBA32);

    while (lines--) {
        u8 skip_code;
        i8 rle_code;
        stream >> skip_code;
        auto pixel_ptr = row_ptr + skip_code - 1;
        stream >> rle_code;
        while (rle_code != -1) {
            if (!stream.remaining())
                return bitmap;
            if (rle_code == 0) {
                // There's another skip byte in the stream
                stream >> skip_code;
                pixel_ptr += skip_code - 1;
            } else if (rle_code < 0) {
                // Decode run length code
                rle_code = -rle_code;
                u8 r, g, b;
                stream >> r >> g >> b;
                while (rle_code--)
                    *pixel_ptr++ = 0xff << 24 | r << 16 | g << 8 | b;
            } else {
                // Copy stream out
                while (rle_code--) {
                    u8 r, g, b;
                    stream >> r >> g >> b;
                    *pixel_ptr++ = 0xff << 24 | r << 16 | g << 8 | b;
                }
            }
            stream >> rle_code;
        }
        row_ptr += row_advance;
    }

    m_last_frame = bitmap->clone();

    return bitmap;
}


/*
 * bink2_background_player.h - Headless looping Bink2 video decoder.
 *
 * Drives a single BK2 stream one frame at a time, wrapping back to frame 0
 * at EOF. Intended for UI backgrounds (e.g. faction-select screen) where the
 * existing blocking Bink2_Play_Movie_InProcess path is inappropriate.
 *
 * No audio, no presentation. The caller owns the GL/Renderer side and
 * uploads the exposed Y/U/V planes as it sees fit.
 */

#ifndef BINK2_BACKGROUND_PLAYER_H
#define BINK2_BACKGROUND_PLAYER_H

#include "bink2_decoder.h"
#include "bink2_video.h"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <vector>

// Decode the video packet at `frame_index` into `out`. If the frame is an
// inter frame, `prev` must point at the previous successfully decoded frame;
// for keyframes it is ignored and may be null. Returns true iff the decoded
// frame is complete.
bool Bink2DecodeFrameByIndex(Bink2Decoder& decoder,
                             std::size_t frame_index,
                             const Bink2DecodedFrame* prev,
                             Bink2DecodedFrame& out);

class Bink2BackgroundPlayer {
public:
    Bink2BackgroundPlayer();
    ~Bink2BackgroundPlayer();

    Bink2BackgroundPlayer(const Bink2BackgroundPlayer&) = delete;
    Bink2BackgroundPlayer& operator=(const Bink2BackgroundPlayer&) = delete;

    // The decoder copies `data` internally; callers may free their buffer
    // after this returns.
    bool Open_From_Memory(const void* data, std::size_t size);
    void Close();
    bool Is_Open() const { return is_open_; }

    // Decode the next frame, wrapping to frame 0 at EOF (re-decoding the
    // initial keyframe as a fresh loop). Returns false if decode fails or
    // the player is not open.
    bool Tick();

    // Dimensions of the stream (valid after Open_From_Memory succeeds).
    int Width()  const { return width_; }
    int Height() const { return height_; }

    std::size_t Frame_Count()  const { return frame_count_; }
    std::size_t Frame_Index()  const { return cursor_; }
    std::uint32_t Fps_Num()    const { return fps_num_; }
    std::uint32_t Fps_Den()    const { return fps_den_; }

    // Most recently decoded frame. Null before the first successful Tick().
    const Bink2DecodedFrame* Current_Frame() const { return current_.get(); }

private:
    std::unique_ptr<Bink2Decoder>      decoder_;
    std::unique_ptr<Bink2DecodedFrame> current_;
    std::size_t   cursor_       = 0;  // index of the frame to decode next
    std::size_t   frame_count_  = 0;
    bool          is_open_      = false;
    int           width_        = 0;
    int           height_       = 0;
    std::uint32_t fps_num_      = 0;
    std::uint32_t fps_den_      = 0;
};

#endif /* BINK2_BACKGROUND_PLAYER_H */

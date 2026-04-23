#include "bink2_background_player.h"

bool Bink2DecodeFrameByIndex(Bink2Decoder& decoder,
                             std::size_t frame_index,
                             const Bink2DecodedFrame* prev,
                             Bink2DecodedFrame& out)
{
    std::vector<uint8_t> packet;
    if (!decoder.Read_Video_Packet(frame_index, packet)) return false;

    Bink2PacketHeader pkt_hdr;
    if (!decoder.Parse_Packet_Header(packet, pkt_hdr)) return false;

    Bink2FramePlan plan;
    if (!Bink2PrepareFramePlan(decoder.Header(), pkt_hdr, packet, plan)) return false;

    const Bink2FrameIndexEntry* entry = decoder.Frame_Entry(frame_index);
    const bool is_kf = entry ? entry->keyframe : (frame_index == 0);

    bool ok = false;
    if (is_kf) {
        ok = Bink2DecodeFirstKeyframeFramePrefix(decoder.Header(), plan, packet, out);
    } else {
        if (!prev) return false;
        ok = Bink2DecodeInterFrameSkipPrefix(decoder.Header(), plan, packet, *prev, out);
    }
    return ok && out.complete;
}

Bink2BackgroundPlayer::Bink2BackgroundPlayer() = default;
Bink2BackgroundPlayer::~Bink2BackgroundPlayer() { Close(); }

bool Bink2BackgroundPlayer::Open_From_Memory(const void* data, std::size_t size)
{
    Close();
    if (!data || size < 48) return false;

    decoder_ = std::make_unique<Bink2Decoder>();
    if (!decoder_->Open_Memory(static_cast<const uint8_t*>(data), size)) {
        decoder_.reset();
        return false;
    }
    const Bink2Header& hdr = decoder_->Header();
    if (!hdr.Is_Valid() || hdr.frame_count == 0) {
        decoder_.reset();
        return false;
    }

    width_       = static_cast<int>(hdr.width);
    height_      = static_cast<int>(hdr.height);
    frame_count_ = hdr.frame_count;
    fps_num_     = hdr.fps_num;
    fps_den_     = hdr.fps_den;
    cursor_      = 0;
    current_.reset();
    is_open_     = true;
    return true;
}

void Bink2BackgroundPlayer::Close()
{
    decoder_.reset();
    current_.reset();
    cursor_      = 0;
    frame_count_ = 0;
    width_ = height_ = 0;
    fps_num_ = fps_den_ = 0;
    is_open_     = false;
}

bool Bink2BackgroundPlayer::Tick()
{
    if (!is_open_ || !decoder_ || frame_count_ == 0) return false;

    // Wrap: once the cursor reaches the end, drop accumulated state and
    // restart from the first keyframe. Inter frames past the wrap boundary
    // would otherwise be reconstructed against the last frame of the loop,
    // which is correct for seamless loops but not for arbitrary re-starts —
    // forcing a keyframe decode gives us a clean loop point.
    if (cursor_ >= frame_count_) {
        cursor_ = 0;
        current_.reset();
    }

    auto next = std::make_unique<Bink2DecodedFrame>();
    const bool ok = Bink2DecodeFrameByIndex(
        *decoder_, cursor_, current_.get(), *next);
    if (!ok) return false;

    current_ = std::move(next);
    ++cursor_;
    return true;
}

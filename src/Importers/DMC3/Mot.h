#pragma once
#include <cstdint>
#include <span>
#include <vector>

namespace DMC3
{
/* One key of a channel: frame, value and the in and out tangents (per frame), all decoded to floats. */
struct MotionFrame
{
	float frame;
	float value;
	float inTangent;
	float outTangent;
};

/* The keys of one animated value (a translation axis or a rotation angle), ascending by frame. */
struct MotionChannel
{
	std::vector<MotionFrame> keys;
};

/*
A decoded `MOT\0` entry: its length in frames, its channels in file order and one mask per bone (docs/DMC3_FORMATS.md, "MOT (motion)").
boneMasks[bone] says which of the bone's nine values have a channel: bits 6, 7, 8 = translation x, y, z, bits 3, 4, 5 = rotation x, y, z, bits 0, 1, 2 = scale x, y, z. The channels are the set bits in the order of the bones, and inside a bone in the order translation, rotation, scale.
*/
struct Motion
{
	float frameCount = 0.0f;
	std::vector<uint16_t> boneMasks;
	std::vector<MotionChannel> channels;
};

/*
Decodes a `MOT\0` entry (bytes_ is the entry alone). Throws std::runtime_error naming the offset if the entry does not follow the layout. Which channel is which bone value is in boneMasks.
*/
Motion importMot(std::span<const uint8_t> bytes_);

/* The value of the channel at a frame (cubic Hermite between the two keys around it; before the first key or after the last it is that key's value). */
float sampleChannel(const MotionChannel& rChannel_, float frame_);
}

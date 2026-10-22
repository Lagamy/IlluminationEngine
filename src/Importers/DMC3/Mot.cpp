#include "Mot.h"
#include <cstring>
#include <format>
#include <stdexcept>

namespace DMC3
{
template<typename T>
static T read(std::span<const uint8_t> bytes_, size_t offset_)
{
	if(offset_ + sizeof(T) > bytes_.size())
	{
		throw std::runtime_error(std::format("Mot: read of {} bytes at {} is past the end ({} bytes).", sizeof(T), offset_, bytes_.size()));
	}
	T value;
	std::memcpy(&value, bytes_.data() + offset_, sizeof(T));
	return value;
}

/*
Layout (docs/DMC3_FORMATS.md, "MOT (motion)"): header whose size is the u32 at +0 (80 in all but one entry seen; +4 "MOT\0", +0x0c f32 frame count, +0x1c u8 bone count, u16 per bone mask from +0x1e), then at that offset the u32 channel count and the channels back to back. Channel: u16 byte size, u16 key count, u32, 6 x f32,
then 8-byte keys of 4 x u16: frame (bit 15 set, flag dropped), value, in tangent, out tangent. Each of the three u16 is f[2c] + f[2c + 1] * u / 65535 with its own float pair c.
*/
Motion importMot(std::span<const uint8_t> bytes_)
{
	if(bytes_.size() < 0x54 || std::memcmp(bytes_.data() + 4, "MOT\0", 4) != 0)
	{
		throw std::runtime_error("Mot: not a MOT entry.");
	}
	Motion motion;
	motion.frameCount = read<float>(bytes_, 0x0c);
	size_t headerSize = read<uint32_t>(bytes_, 0);
	uint8_t boneCount = read<uint8_t>(bytes_, 0x1c);
	for(uint8_t bone = 0; bone < boneCount; bone++) motion.boneMasks.push_back(read<uint16_t>(bytes_, 0x1e + 2 * static_cast<size_t>(bone)));
	uint32_t channelCount = read<uint32_t>(bytes_, headerSize);
	size_t at = headerSize + 4;
	for(uint32_t c = 0; c < channelCount; c++)
	{
		uint16_t byteSize = read<uint16_t>(bytes_, at);
		uint16_t keyCount = read<uint16_t>(bytes_, at + 2);
		if(byteSize != 32u + 8u * keyCount)
		{
			throw std::runtime_error(std::format("Mot: channel {} at {} has size {} for {} keys.", c, at, byteSize, keyCount));
		}
		float pair[6];
		for(int i = 0; i < 6; i++) pair[i] = read<float>(bytes_, at + 8 + 4 * i);
		MotionChannel channel;
		channel.keys.reserve(keyCount);
		for(uint16_t k = 0; k < keyCount; k++)
		{
			size_t key = at + 32 + 8 * static_cast<size_t>(k);
			float decoded[3];
			for(int i = 0; i < 3; i++) decoded[i] = pair[2 * i] + pair[2 * i + 1] * static_cast<float>(read<uint16_t>(bytes_, key + 2 + 2 * i)) / 65535.0f;
			channel.keys.push_back(MotionFrame{static_cast<float>(read<uint16_t>(bytes_, key) & 0x7fff), decoded[0], decoded[1], decoded[2]});
		}
		motion.channels.push_back(std::move(channel));
		at += byteSize;
	}
	return motion;
}

/* Hermite form: value0, value1 and the tangents scaled by the span between the keys. That the tangents are used this way is a hypothesis (they match the slope between keys in the idle clip). */
float sampleChannel(const MotionChannel& rChannel_, float frame_)
{
	const std::vector<MotionFrame>& keys = rChannel_.keys;
	if(keys.empty()) return 0.0f;
	if(frame_ <= keys.front().frame) return keys.front().value;
	if(frame_ >= keys.back().frame) return keys.back().value;
	size_t next = 1;
	while(keys[next].frame < frame_) next++;
	const MotionFrame& a = keys[next - 1];
	const MotionFrame& b = keys[next];
	float span = b.frame - a.frame;
	float t = (frame_ - a.frame) / span;
	float t2 = t * t;
	float t3 = t2 * t;
	return (2 * t3 - 3 * t2 + 1) * a.value + (t3 - 2 * t2 + t) * span * a.outTangent + (-2 * t3 + 3 * t2) * b.value + (t3 - t2) * span * b.inTangent;
}
}

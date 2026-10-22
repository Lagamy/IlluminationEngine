#include "Tex.h"
#include <cstring>
#include <format>
#include <stdexcept>

namespace DMC3
{
/* Reads a T at an offset of the entry, throwing if it is not inside it. */
template<typename T>
static T read(std::span<const uint8_t> bytes_, size_t offset_)
{
	if(offset_ + sizeof(T) > bytes_.size())
	{
		throw std::runtime_error(std::format("Tex: read of {} bytes at {} is outside the {} byte entry.", sizeof(T), offset_, bytes_.size()));
	}
	T value;
	std::memcpy(&value, bytes_.data() + offset_, sizeof(T));
	return value;
}

/*
Decodes one 4x4 block of DXT5 (BC3, 16 bytes) into RGBA8 pixels of a width_ wide image. The first 8 bytes are the alpha: two end values and 16 3-bit indices (8 levels, or 6 plus 0 and 255 when the first end value is not larger); the next
8 the color: two RGB565 end values and 16 2-bit indices. The color always uses the 4-color mode in BC3, whatever the order of the end values.
*/
static void decodeBlock(const uint8_t* pBlock_, uint8_t* pPixels_, uint32_t width_)
{
	uint8_t alpha[8] = {pBlock_[0], pBlock_[1]};
	if(alpha[0] > alpha[1])
	{
		for(int i = 1; i < 7; i++) alpha[1 + i] = static_cast<uint8_t>(((7 - i) * alpha[0] + i * alpha[1]) / 7);
	}
	else
	{
		for(int i = 1; i < 5; i++) alpha[1 + i] = static_cast<uint8_t>(((5 - i) * alpha[0] + i * alpha[1]) / 5);
		alpha[6] = 0;
		alpha[7] = 255;
	}
	uint64_t alphaIndices = 0;
	std::memcpy(&alphaIndices, pBlock_ + 2, 6);

	uint16_t end0 = static_cast<uint16_t>(pBlock_[8] | (pBlock_[9] << 8));
	uint16_t end1 = static_cast<uint16_t>(pBlock_[10] | (pBlock_[11] << 8));
	uint8_t colors[4][3];
	for(int e = 0; e < 2; e++)
	{
		uint16_t end = e == 0 ? end0 : end1;
		uint8_t r = static_cast<uint8_t>((end >> 11) & 31);
		uint8_t g = static_cast<uint8_t>((end >> 5) & 63);
		uint8_t b = static_cast<uint8_t>(end & 31);
		colors[e][0] = static_cast<uint8_t>((r << 3) | (r >> 2));
		colors[e][1] = static_cast<uint8_t>((g << 2) | (g >> 4));
		colors[e][2] = static_cast<uint8_t>((b << 3) | (b >> 2));
	}
	for(int c = 0; c < 3; c++)
	{
		colors[2][c] = static_cast<uint8_t>((2 * colors[0][c] + colors[1][c]) / 3);
		colors[3][c] = static_cast<uint8_t>((colors[0][c] + 2 * colors[1][c]) / 3);
	}
	uint32_t colorIndices = static_cast<uint32_t>(pBlock_[12] | (pBlock_[13] << 8) | (pBlock_[14] << 16) | (pBlock_[15] << 24));

	for(int i = 0; i < 16; i++)
	{
		uint8_t* pPixel = pPixels_ + 4 * ((i / 4) * static_cast<size_t>(width_) + (i % 4));
		const uint8_t* pColor = colors[(colorIndices >> (2 * i)) & 3];
		pPixel[0] = pColor[0];
		pPixel[1] = pColor[1];
		pPixel[2] = pColor[2];
		pPixel[3] = alpha[(alphaIndices >> (3 * i)) & 7];
	}
}

/*
Layout read here (file inspection of pl000.pac slot 0, see the format doc): the count and sector sizes are at the start, and the first `DDS ` magic sits 16 bytes after its chunk's header; the table is padded, so the first chunk is found
by searching for that magic, and the next chunks follow at the sector sizes. The DDS header is the plain 124-byte one (no DX10 extension): height at +12, width at +16, the four-character code at +84, the first mip level at +128.
*/
std::vector<Graphics::Texture> importTextures(std::span<const uint8_t> bytes_)
{
	uint32_t count = read<uint32_t>(bytes_, 0);
	if(count == 0 || count > 64)
	{
		throw std::runtime_error(std::format("Tex: texture count {} at 0 is not plausible.", count));
	}
	size_t chunk = 0;
	for(size_t i = 4 + 4 * static_cast<size_t>(count); i + 4 <= bytes_.size(); i++)
	{
		if(std::memcmp(bytes_.data() + i, "DDS ", 4) == 0)
		{
			chunk = i;
			break;
		}
	}
	if(chunk == 0)
	{
		throw std::runtime_error("Tex: no DDS file in the entry.");
	}

	std::vector<Graphics::Texture> textures;
	for(uint32_t t = 0; t < count; t++)
	{
		if(t > 0) chunk += 2048 * static_cast<size_t>(read<uint32_t>(bytes_, 4 + 4 * static_cast<size_t>(t - 1)));
		if(chunk + 128 > bytes_.size() || std::memcmp(bytes_.data() + chunk, "DDS ", 4) != 0)
		{
			throw std::runtime_error(std::format("Tex: texture {} has no DDS magic at {}.", t, chunk));
		}
		if(std::memcmp(bytes_.data() + chunk + 84, "DXT5", 4) != 0)
		{
			throw std::runtime_error(std::format("Tex: texture {} at {} is not DXT5.", t, chunk));
		}
		uint32_t height = read<uint32_t>(bytes_, chunk + 12);
		uint32_t width = read<uint32_t>(bytes_, chunk + 16);
		if(width == 0 || height == 0 || width % 4 != 0 || height % 4 != 0)
		{
			throw std::runtime_error(std::format("Tex: texture {} at {} has size {}x{}.", t, chunk, width, height));
		}
		size_t dataSize = static_cast<size_t>(width / 4) * (height / 4) * 16;
		if(chunk + 128 + dataSize > bytes_.size())
		{
			throw std::runtime_error(std::format("Tex: texture {} at {} runs past the {} byte entry.", t, chunk, bytes_.size()));
		}

		Graphics::Texture& rTexture = textures.emplace_back(width, height, 4);
		rTexture.filter = GL_LINEAR;
		rTexture.wrap = GL_REPEAT;
		const uint8_t* pData = bytes_.data() + chunk + 128;
		for(uint32_t by = 0; by < height / 4; by++)
		{
			for(uint32_t bx = 0; bx < width / 4; bx++)
			{
				decodeBlock(pData + 16 * (static_cast<size_t>(by) * (width / 4) + bx), rTexture.pixels.data() + 4 * (static_cast<size_t>(by) * 4 * width + bx * 4), width);
			}
		}
	}
	return textures;
}
}

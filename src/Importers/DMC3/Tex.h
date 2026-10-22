#pragma once
#include <cstdint>
#include <span>
#include <vector>
#include "Texture.h"

namespace DMC3
{
/*
Reads the textures of a character pac's texture slot (docs/DMC3_FORMATS.md, "Textures of a character pac"; bytes_ is the entry alone): a u32 count, u32 sizes in 2048-byte sectors, then the chunks back to back, each a 16-byte
header and a DXT5 `DDS ` file. Returns one Texture per chunk, in order, decoded from the DDS's first mip level to RGBA8 and not uploaded yet (the mip chain is made by Texture::create). Filter linear, wrap repeat.
Throws std::runtime_error naming the offset if the entry does not follow the layout.
*/
std::vector<Graphics::Texture> importTextures(std::span<const uint8_t> bytes_);
}

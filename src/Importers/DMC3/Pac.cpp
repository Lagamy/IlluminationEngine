#include "Pac.h"
#include <cstring>
#include <format>
#include <stdexcept>

namespace DMC3
{
std::vector<PacEntry> readPac(std::span<const uint8_t> bytes_)
{
	if(bytes_.size() < 8 || (std::memcmp(bytes_.data(), "PAC\0", 4) != 0 && std::memcmp(bytes_.data(), "PNST", 4) != 0))
	{
		throw std::runtime_error("Pac: not a PAC or PNST container.");
	}
	uint32_t count;
	std::memcpy(&count, bytes_.data() + 4, 4);
	if(count == 0 || count > (bytes_.size() - 8) / 4)
	{
		throw std::runtime_error(std::format("Pac: entry count {} does not fit in {} bytes.", count, bytes_.size()));
	}

	std::vector<uint32_t> offsets(count);
	std::memcpy(offsets.data(), bytes_.data() + 8, 4 * static_cast<size_t>(count));
	uint32_t previous = 8 + 4 * count;
	for(uint32_t offset : offsets)
	{
		if(offset == 0) continue;
		if(offset < previous || offset > bytes_.size())
		{
			throw std::runtime_error(std::format("Pac: offset {} is out of order or past the end ({} bytes).", offset, bytes_.size()));
		}
		previous = offset;
	}

	std::vector<PacEntry> entries(count, PacEntry{0, 0});
	for(uint32_t i = 0; i < count; i++)
	{
		if(offsets[i] == 0) continue;
		uint32_t end = static_cast<uint32_t>(bytes_.size());
		for(uint32_t next = i + 1; next < count; next++)
		{
			if(offsets[next] != 0)
			{
				end = offsets[next];
				break;
			}
		}
		entries[i] = PacEntry{offsets[i], end - offsets[i]};
	}
	return entries;
}
}

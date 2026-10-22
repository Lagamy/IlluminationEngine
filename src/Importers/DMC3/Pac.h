#pragma once
#include <cstdint>
#include <span>
#include <vector>

namespace DMC3
{
/* One slot of a PAC container: where its bytes start (from the start of the container) and how many there are. An empty slot has offset 0 and size 0. */
struct PacEntry
{
	uint32_t offset;
	uint32_t size;
};

/*
Splits a `.pac` container (docs/DMC3_FORMATS.md, ".pac container"): magic `PAC\0` or `PNST`, u32 count, u32 offsets[count] (little-endian, from the start of the container, ascending, 0 = empty slot).
Entry i runs to the next non-zero offset, the last to the end. The payloads are not read: what an entry is is told by its first four bytes, and a nested container is split by calling this again on its bytes.
Throws std::runtime_error if the bytes do not follow that layout.
*/
std::vector<PacEntry> readPac(std::span<const uint8_t> bytes_);
}

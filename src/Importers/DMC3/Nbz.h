#pragma once
#include <cstdint>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>
#include "miniz/miniz.h"

namespace DMC3
{
struct NbzFile
{
	mz_uint index;
	uint64_t size;
};

/*
An opened Devil May Cry HD Collection archive (`.nbz`, e.g. data/dmc3/dmc3-0.nbz). It is a plain ZIP (observed: file inspection,
PK signature, deflate and stored entries), read with miniz (third-party/miniz). Only the central directory is read up front;
file contents are inflated from disk on request, so the archive is never loaded whole. Paths are the ZIP entry names with '/'
separators, e.g. "GData.afs/pl000.pac"; directory entries are not listed.
miniz keeps a pointer to the archive struct inside itself, so an Nbz cannot be moved or copied: it is created by openNbz as a unique_ptr.
*/
struct Nbz
{
	mz_zip_archive zip = {};
	std::unordered_map<std::string, NbzFile> files;

	Nbz() = default;
	Nbz(const Nbz&) = delete;
	Nbz& operator=(const Nbz&) = delete;
	~Nbz();

	std::vector<uint8_t> readFile(const std::string& path_);
};

std::unique_ptr<Nbz> openNbz(const char* path_);
}

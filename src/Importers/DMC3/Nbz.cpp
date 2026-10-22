#include "Nbz.h"
#include <format>
#include <stdexcept>

namespace DMC3
{
Nbz::~Nbz()
{
	if(zip.m_pState != nullptr)
	{
		mz_zip_reader_end(&zip);
	}
}

std::unique_ptr<Nbz> openNbz(const char* path_)
{
	auto nbz = std::make_unique<Nbz>();
	if(!mz_zip_reader_init_file(&nbz->zip, path_, 0))
	{
		throw std::runtime_error(std::format("Nbz: failed to open {}: {}", path_, mz_zip_get_error_string(mz_zip_get_last_error(&nbz->zip))));
	}

	mz_uint count = mz_zip_reader_get_num_files(&nbz->zip);
	for(mz_uint i = 0; i < count; i++)
	{
		mz_zip_archive_file_stat stat;
		if(!mz_zip_reader_file_stat(&nbz->zip, i, &stat))
		{
			throw std::runtime_error(std::format("Nbz: {} entry {} unreadable: {}", path_, i, mz_zip_get_error_string(mz_zip_get_last_error(&nbz->zip))));
		}
		if(stat.m_is_directory)
		{
			continue;
		}
		nbz->files[stat.m_filename] = NbzFile{i, stat.m_uncomp_size};
	}
	return nbz;
}

std::vector<uint8_t> Nbz::readFile(const std::string& path_)
{
	auto it = files.find(path_);
	if(it == files.end())
	{
		throw std::runtime_error(std::format("Nbz: no file {}", path_));
	}

	std::vector<uint8_t> bytes(it->second.size);
	if(!bytes.empty() && !mz_zip_reader_extract_to_mem(&zip, it->second.index, bytes.data(), bytes.size(), 0))
	{
		throw std::runtime_error(std::format("Nbz: failed to extract {}: {}", path_, mz_zip_get_error_string(mz_zip_get_last_error(&zip))));
	}
	return bytes;
}
}

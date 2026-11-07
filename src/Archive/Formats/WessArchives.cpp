// -----------------------------------------------------------------------------
// SLADE - It's a Doom Editor
// Filename:    WessArchives.cpp
// Description: WmdArchive, WsdArchive, WddArchive: Doom 64 WESS sound files
//
// This program is free software; you can redistribute it and/or modify it
// under the terms of the GNU General Public License as published by the Free
// Software Foundation; either version 2 of the License, or (at your option)
// any later version.
// -----------------------------------------------------------------------------


// -----------------------------------------------------------------------------
//
// Includes
//
// -----------------------------------------------------------------------------
#include "Main.h"
#include "WessArchives.h"
#include "Archive/EntryType/EntryType.h"
#include "General/UI.h"
#include "WessAudio.h"
#include "WessData.h"
#include "WessSeq.h"
#include <filesystem>
#include <fstream>

using namespace slade;


// -----------------------------------------------------------------------------
//
// Local Functions
//
// -----------------------------------------------------------------------------
namespace
{
const char* wmd_entry_names[] = { "HEADER", "PATCHES", "SUBPATCHES", "WAVTABLE", "LOOPS", "PREDICTORS" };

bool fileStartsWith(const string& filename, const char* magic)
{
	std::ifstream file(std::filesystem::u8path(filename), std::ios::binary);
	char          buf[4] = {};
	return file.read(buf, 4) && std::memcmp(buf, magic, 4) == 0;
}

string lowerExt(const std::filesystem::path& path)
{
	auto ext = path.extension().string();
	for (auto& c : ext)
		c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
	return ext;
}

bool refuse(const char* what)
{
	global::error = what;
	return false;
}

// Reads the codebook kept in the entry's wess_book property (hex string)
bool entryBook(ArchiveEntry* entry, wess::Book& book)
{
	const auto      hex = entry->exProp<string>("wess_book");
	vector<uint8_t> bytes;
	for (size_t i = 0; i + 1 < hex.size(); i += 2)
		bytes.push_back(static_cast<uint8_t>(std::stoi(hex.substr(i, 2), nullptr, 16)));
	return wess::Book::fromBytes(bytes.data(), bytes.size(), book);
}
} // namespace


// -----------------------------------------------------------------------------
//
// WessArchive Class Functions
//
// -----------------------------------------------------------------------------


// -----------------------------------------------------------------------------
// Adds a loaded, unmodified entry with [data] to the end of the archive
// -----------------------------------------------------------------------------
void WessArchive::addLoadedEntry(string_view name, const vector<uint8_t>& data)
{
	auto entry = std::make_shared<ArchiveEntry>(name, static_cast<uint32_t>(data.size()));
	if (!data.empty())
		entry->importMem(data.data(), static_cast<uint32_t>(data.size()));

	// These are never any of the other formats SLADE knows about
	entry->setType(EntryType::unknownType());
	entry->setState(ArchiveEntry::State::Unmodified);
	rootDir()->addEntry(entry);
}

// -----------------------------------------------------------------------------
// Returns the data of all entries in order
// -----------------------------------------------------------------------------
vector<vector<uint8_t>> WessArchive::entryData()
{
	vector<vector<uint8_t>> result;
	for (unsigned a = 0; a < numEntries(); a++)
	{
		auto entry = entryAt(a);
		auto data  = entry->rawData();
		result.emplace_back(data, data + entry->size());
	}
	return result;
}

// -----------------------------------------------------------------------------
// Marks all entries as saved
// -----------------------------------------------------------------------------
void WessArchive::markEntriesSaved()
{
	for (unsigned a = 0; a < numEntries(); a++)
		entryAt(a)->setState(ArchiveEntry::State::Unmodified);
}


// -----------------------------------------------------------------------------
//
// WmdArchive Class Functions
//
// -----------------------------------------------------------------------------


bool WmdArchive::open(MemChunk& mc)
{
	wess::Wmd   wmd;
	string      err;
	if (!wess::parseWmd(mc.data(), mc.size(), wmd, err))
	{
		global::error = err;
		return false;
	}

	ArchiveModSignalBlocker sig_blocker{ *this };

	const vector<uint8_t>* sections[] = { &wmd.header, &wmd.patches, &wmd.subpatches, &wmd.wavtable, &wmd.loops, &wmd.predictors };
	// PATCHES, SUBPATCHES, WAVTABLE and LOOPS are shown as editable tables
	static const char* table_types[] = { nullptr, "wmd_patches", "wmd_subpatches", "wmd_wavtable", "wmd_loops", nullptr };
	for (unsigned a = 0; a < 6; a++)
	{
		addLoadedEntry(wmd_entry_names[a], *sections[a]);
		if (table_types[a])
			if (auto type = EntryType::fromId(table_types[a]))
				entryAt(a)->setType(type);
	}
	tail_ = wmd.tail;

	sig_blocker.unblock();
	setModified(false);
	return true;
}

bool WmdArchive::write(MemChunk& mc, bool update)
{
	if (numEntries() != 6)
	{
		global::error = "A WMD needs exactly its 6 sections";
		return false;
	}

	auto data = entryData();
	if (data[0].size() != wess::wmd_header_size)
	{
		global::error = "The WMD header must be 56 bytes";
		return false;
	}

	wess::Wmd wmd;
	wmd.header     = data[0];
	wmd.patches    = data[1];
	wmd.subpatches = data[2];
	wmd.wavtable   = data[3];
	wmd.loops      = data[4];
	wmd.predictors = data[5];
	wmd.tail       = tail_;

	string err;
	if (!wess::validateWmd(wmd, err))
	{
		global::error = err;
		return false;
	}

	const auto out = wess::buildWmd(wmd);
	mc.clear();
	mc.importMem(out.data(), static_cast<uint32_t>(out.size()));

	if (update)
		markEntriesSaved();
	return true;
}

shared_ptr<ArchiveEntry> WmdArchive::addEntry(shared_ptr<ArchiveEntry>, unsigned, ArchiveDir*)
{
	refuse("A WMD has a fixed set of sections");
	return nullptr;
}
shared_ptr<ArchiveEntry> WmdArchive::addNewEntry(string_view, unsigned, ArchiveDir*)
{
	refuse("A WMD has a fixed set of sections");
	return nullptr;
}
bool WmdArchive::removeEntry(ArchiveEntry*, bool)
{
	return refuse("A WMD has a fixed set of sections");
}
bool WmdArchive::moveEntry(ArchiveEntry*, unsigned, ArchiveDir*)
{
	return refuse("A WMD has a fixed set of sections");
}
bool WmdArchive::swapEntries(unsigned, unsigned, ArchiveDir*)
{
	return refuse("A WMD has a fixed set of sections");
}
bool WmdArchive::swapEntries(ArchiveEntry*, ArchiveEntry*)
{
	return refuse("A WMD has a fixed set of sections");
}

bool WmdArchive::isWmdArchive(MemChunk& mc)
{
	wess::Wmd wmd;
	string    err;
	return mc.size() >= wess::wmd_header_size && wess::parseWmd(mc.data(), mc.size(), wmd, err);
}

bool WmdArchive::isWmdArchive(const string& filename)
{
	return fileStartsWith(filename, "SN64");
}


// -----------------------------------------------------------------------------
//
// WsdArchive Class Functions
//
// -----------------------------------------------------------------------------


bool WsdArchive::open(MemChunk& mc)
{
	wess::Wsd wsd;
	string    err;
	if (!wess::parseWsd(mc.data(), mc.size(), wsd, err))
	{
		global::error = err;
		return false;
	}

	ArchiveModSignalBlocker sig_blocker{ *this };

	header_ = wsd.header;
	auto seq_type = EntryType::fromId("midi_wess");
	for (unsigned a = 0; a < wsd.sequences.size(); a++)
	{
		addLoadedEntry(fmt::format("SEQ{:03d}", a), wsd.sequences[a]);
		if (seq_type)
			entryAt(a)->setType(seq_type);
	}

	sig_blocker.unblock();
	setModified(false);
	return true;
}

bool WsdArchive::write(MemChunk& mc, bool update)
{
	wess::Wsd wsd;
	wsd.header    = header_;
	wsd.sequences = entryData();

	const auto out = wess::buildWsd(wsd);
	mc.clear();
	mc.importMem(out.data(), static_cast<uint32_t>(out.size()));

	if (update)
		markEntriesSaved();
	return true;
}

bool WsdArchive::isWsdArchive(MemChunk& mc)
{
	wess::Wsd wsd;
	string    err;
	return mc.size() >= wess::wsd_header_size && wess::parseWsd(mc.data(), mc.size(), wsd, err);
}

bool WsdArchive::isWsdArchive(const string& filename)
{
	return fileStartsWith(filename, "SSEQ");
}


// -----------------------------------------------------------------------------
//
// WddArchive Class Functions
//
// -----------------------------------------------------------------------------


// -----------------------------------------------------------------------------
// Finds the WMD with the same name as [wdd_filename] in the same folder
// (ignoring case), returns an empty string if there isn't one
// -----------------------------------------------------------------------------
string WddArchive::findWmd(const string& wdd_filename)
{
	namespace fs = std::filesystem;

	if (wdd_filename.empty())
		return {};

	const auto path = fs::u8path(wdd_filename);
	auto       dir  = path.parent_path();
	if (dir.empty())
		dir = ".";

	auto lower = [](string s) {
		for (auto& c : s)
			c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
		return s;
	};
	const auto stem = lower(path.stem().string());

	std::error_code ec;
	for (const auto& item : fs::directory_iterator(dir, ec))
		if (item.is_regular_file(ec) && lowerExt(item.path()) == ".wmd" && lower(item.path().stem().string()) == stem)
			return item.path().u8string();

	return {};
}

bool WddArchive::isWddArchive(const string& filename)
{
	if (lowerExt(std::filesystem::u8path(filename)) != ".wdd")
		return false;

	MemChunk  mc;
	wess::Wmd wmd;
	string    err;
	const auto wmd_file = findWmd(filename);
	return !wmd_file.empty() && mc.importFile(wmd_file) && wess::parseWmd(mc.data(), mc.size(), wmd, err);
}

bool WddArchive::open(MemChunk& mc)
{
	// The sample table is in the WMD
	wmd_path_ = findWmd(filename_);
	MemChunk wmd_mc;
	if (wmd_path_.empty() || !wmd_mc.importFile(wmd_path_))
	{
		global::error = "A WDD needs the WMD with the same name in the same folder to find its samples";
		return false;
	}

	wess::Wmd         wmd;
	vector<wess::Bytes> samples;
	string            err;
	if (!wess::parseWmd(wmd_mc.data(), wmd_mc.size(), wmd, err) || !wess::splitWdd(mc.data(), mc.size(), wmd, samples, err))
	{
		global::error = err;
		return false;
	}
	wmd_data_.assign(wmd_mc.data(), wmd_mc.data() + wmd_mc.size());

	ArchiveModSignalBlocker sig_blocker{ *this };

	auto sample_type = EntryType::fromId("snd_wess");
	for (unsigned a = 0; a < samples.size(); a++)
	{
		addLoadedEntry(fmt::format("SFX{:03d}", a), samples[a]);

		// Remember the codebook for decoding the sample (hex string)
		auto entry = entryAt(a);
		if (sample_type)
			entry->setType(sample_type);
		static const char* digits = "0123456789ABCDEF";
		string             hex;
		for (auto byte : wmd.book(a))
		{
			hex.push_back(digits[byte >> 4]);
			hex.push_back(digits[byte & 15]);
		}
		entry->exProp("wess_book") = hex;
		entry->exProp("wess_rate") = wmd.sampleRate(a);
	}

	sig_blocker.unblock();
	setModified(false);
	return true;
}

bool WddArchive::write(MemChunk& mc, bool update)
{
	wess::Wmd wmd;
	string    err;
	if (!wess::parseWmd(wmd_data_.data(), wmd_data_.size(), wmd, err))
	{
		global::error = err;
		return false;
	}
	if (numEntries() != wmd.numSamples())
	{
		global::error = "The number of samples can't change";
		return false;
	}

	// Samples that were imported can have their own codebook
	for (unsigned a = 0; a < numEntries(); a++)
	{
		wess::Book book;
		if ((a + 1) * wess::wmd_predictor_size > wmd.predictors.size() || !entryBook(entryAt(a), book))
			continue;
		const auto hex = entryAt(a)->exProp<string>("wess_book");
		for (size_t i = 0; i < wess::wmd_predictor_size && i * 2 + 1 < hex.size(); i++)
			wmd.predictors[a * wess::wmd_predictor_size + i] = static_cast<uint8_t>(std::stoi(hex.substr(i * 2, 2), nullptr, 16));
	}

	const auto out = wess::buildWdd(entryData(), wmd);
	wmd_updated_   = wess::buildWmd(wmd);

	mc.clear();
	mc.importMem(out.data(), static_cast<uint32_t>(out.size()));

	if (update)
		markEntriesSaved();
	return true;
}

// -----------------------------------------------------------------------------
// Writes the WDD to [filename], and the WMD with its updated sample table
// next to it
// -----------------------------------------------------------------------------
bool WddArchive::write(string_view filename, bool update)
{
	namespace fs = std::filesystem;

	MemChunk mc;
	if (!write(mc, update) || !mc.exportFile(filename))
		return false;

	const auto target = fs::u8path(string{ filename }).parent_path()
						/ (fs::u8path(string{ filename }).stem() += fs::u8path(wmd_path_).extension());

	MemChunk wmd_mc;
	wmd_mc.importMem(wmd_updated_.data(), static_cast<uint32_t>(wmd_updated_.size()));
	if (!wmd_mc.exportFile(target.u8string()))
	{
		global::error = "Saved the WDD, but couldn't write its WMD";
		return false;
	}

	if (update)
	{
		wmd_path_ = target.u8string();
		wmd_data_ = wmd_updated_;
	}
	log::info("Updated sample table in {}", target.u8string());
	return true;
}

shared_ptr<ArchiveEntry> WddArchive::addEntry(shared_ptr<ArchiveEntry>, unsigned, ArchiveDir*)
{
	refuse("A WDD has a fixed set of samples (instruments refer to them by index)");
	return nullptr;
}
shared_ptr<ArchiveEntry> WddArchive::addNewEntry(string_view, unsigned, ArchiveDir*)
{
	refuse("A WDD has a fixed set of samples (instruments refer to them by index)");
	return nullptr;
}
bool WddArchive::removeEntry(ArchiveEntry*, bool)
{
	return refuse("A WDD has a fixed set of samples (instruments refer to them by index)");
}
bool WddArchive::moveEntry(ArchiveEntry*, unsigned, ArchiveDir*)
{
	return refuse("A WDD has a fixed set of samples (instruments refer to them by index)");
}
bool WddArchive::swapEntries(unsigned, unsigned, ArchiveDir*)
{
	return refuse("A WDD has a fixed set of samples (instruments refer to them by index)");
}
bool WddArchive::swapEntries(ArchiveEntry*, ArchiveEntry*)
{
	return refuse("A WDD has a fixed set of samples (instruments refer to them by index)");
}


// -----------------------------------------------------------------------------
//
// wessaudio Functions
//
// -----------------------------------------------------------------------------
bool wessaudio::isSample(const ArchiveEntry* entry)
{
	return entry && entry->type() && entry->type()->id() == "snd_wess";
}

bool wessaudio::toWav(ArchiveEntry* entry, MemChunk& wav)
{
	wess::Book book;
	if (!entryBook(entry, book))
	{
		global::error = "This sample has no valid ADPCM codebook";
		return false;
	}

	auto rate = entry->exProp<int>("wess_rate");
	if (rate <= 0)
		rate = wess::default_sample_rate;

	const auto pcm = wess::decodeVadpcm(entry->rawData(), entry->size(), book);
	const auto out = wess::makeWav(pcm, rate);
	wav.clear();
	return wav.importMem(out.data(), static_cast<uint32_t>(out.size()));
}

bool wessaudio::fromWav(ArchiveEntry* entry, MemChunk& wav)
{
	wess::Book book;
	if (!entryBook(entry, book))
	{
		global::error = "This sample has no valid ADPCM codebook";
		return false;
	}

	vector<int16_t> pcm;
	int             rate = 0;
	string          err;
	if (!wess::parseWav(wav.data(), wav.size(), pcm, rate, err))
	{
		global::error = err;
		return false;
	}
	if (pcm.empty())
	{
		global::error = "The WAV file has no audio";
		return false;
	}

	// Samples are stored at their own playback rate (same one the WAV export uses)
	auto target_rate = entry->exProp<int>("wess_rate");
	if (target_rate <= 0)
		target_rate = wess::default_sample_rate;
	pcm = wess::resample(pcm, rate, target_rate);

	// Encode with the sample's current codebook, and with one designed for the new audio
	// (like the N64 SDK's tabledesign tool). The one that reproduces the audio best is kept.
	auto adpcm     = wess::encodeVadpcm(pcm, book);
	auto book_data = wess::bookToBytes(book);
	{
		vector<uint8_t> designed;
		wess::Book      new_book;
		if (wess::designBook(pcm, designed) && wess::Book::fromBytes(designed.data(), designed.size(), new_book))
		{
			auto new_adpcm = wess::encodeVadpcm(pcm, new_book);
			if (wess::pcmError(pcm, wess::decodeVadpcm(new_adpcm.data(), new_adpcm.size(), new_book))
				< wess::pcmError(pcm, wess::decodeVadpcm(adpcm.data(), adpcm.size(), book)))
			{
				adpcm     = std::move(new_adpcm);
				book_data = designed;
				log::info("WESS: using a newly designed ADPCM codebook for the imported sample");
			}
		}
	}

	if (adpcm.size() & 1)
		adpcm.push_back(0); // Samples are an even number of bytes
	if (!entry->importMem(adpcm.data(), static_cast<uint32_t>(adpcm.size())))
		return false;

	static const char* digits = "0123456789ABCDEF";
	string             hex;
	for (auto byte : book_data)
	{
		hex.push_back(digits[byte >> 4]);
		hex.push_back(digits[byte & 15]);
	}
	entry->exProp("wess_book") = hex;
	return true;
}

bool wessaudio::importFile(ArchiveEntry* entry, string_view filename)
{
	MemChunk mc;
	if (!mc.importFile(filename))
	{
		global::error = "Couldn't read the file";
		return false;
	}

	if (isSequence(entry) && mc.size() >= 4 && std::memcmp(mc.data(), "MThd", 4) == 0)
		return fromMidi(entry, mc);

	if (mc.size() >= 12 && std::memcmp(mc.data(), "RIFF", 4) == 0 && std::memcmp(mc.data() + 8, "WAVE", 4) == 0)
		return fromWav(entry, mc);

	return entry->importMem(mc.data(), mc.size());
}

bool wessaudio::isSequence(const ArchiveEntry* entry)
{
	return entry && entry->type() && entry->type()->id() == "midi_wess";
}

bool wessaudio::toMidi(ArchiveEntry* entry, MemChunk& midi)
{
	const wess::Bytes seq(entry->rawData(), entry->rawData() + entry->size());
	wess::Bytes       out;
	string            err;
	if (!wess::seqToMidi(seq, out, err))
	{
		global::error = err;
		return false;
	}

	midi.clear();
	return midi.importMem(out.data(), static_cast<uint32_t>(out.size()));
}

bool wessaudio::fromMidi(ArchiveEntry* entry, MemChunk& midi)
{
	wess::Bytes seq;
	string      err;
	if (!wess::midiToSeq(midi.data(), midi.size(), seq, err))
	{
		global::error = err;
		return false;
	}

	return entry->importMem(seq.data(), static_cast<uint32_t>(seq.size()));
}

bool wessaudio::isConvertible(const ArchiveEntry* entry)
{
	return isSample(entry) || isSequence(entry);
}

bool wessaudio::toExportFormat(ArchiveEntry* entry, MemChunk& out)
{
	if (isSample(entry))
		return toWav(entry, out);
	if (isSequence(entry))
		return toMidi(entry, out);
	return false;
}

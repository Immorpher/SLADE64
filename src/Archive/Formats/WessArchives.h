#pragma once

#include "Archive/Archive.h"

namespace slade
{
// -----------------------------------------------------------------------------
// Doom 64 WESS sound files. See WessData.h for the file layouts.
//
// WmdArchive: the sound bank. One entry per section (header, patches,
//             subpatches, wave table, loops, predictors). The set of entries is
//             fixed, counts in the header are updated from the entry sizes when saving.
// WsdArchive: the sequences (music and sound effects), one entry per sequence.
//             Sequences can be added, removed, moved and replaced.
// WddArchive: the sample data, one entry per sample. The sample boundaries come from
//             the WMD with the same name in the same folder, which must exist.
//             Samples can be replaced but not added/removed (instruments refer to them
//             by index). Saving also updates the sample offsets/sizes in that WMD.
// -----------------------------------------------------------------------------
class WessArchive : public TreelessArchive
{
public:
	WessArchive(string_view format) : TreelessArchive(format) {}

	bool loadEntryData(ArchiveEntry* entry) override { return entry && entry->isLoaded(); }

protected:
	// Adds a (loaded, unmodified) entry with [data] to the end of the archive
	void addLoadedEntry(string_view name, const vector<uint8_t>& data);

	// Collects the data of all entries, in order
	vector<vector<uint8_t>> entryData();

	void markEntriesSaved();
};

class WmdArchive : public WessArchive
{
public:
	WmdArchive() : WessArchive("wmd") {}

	bool open(MemChunk& mc) override;
	bool write(MemChunk& mc, bool update = true) override;

	// The set of entries is fixed
	shared_ptr<ArchiveEntry> addEntry(shared_ptr<ArchiveEntry> entry, unsigned position = 0xFFFFFFFF, ArchiveDir* dir = nullptr) override;
	shared_ptr<ArchiveEntry> addNewEntry(string_view name = "", unsigned position = 0xFFFFFFFF, ArchiveDir* dir = nullptr) override;
	bool                     removeEntry(ArchiveEntry* entry, bool set_deleted = true) override;
	bool                     moveEntry(ArchiveEntry* entry, unsigned position = 0xFFFFFFFF, ArchiveDir* dir = nullptr) override;
	bool                     swapEntries(unsigned index1, unsigned index2, ArchiveDir* dir = nullptr) override;
	bool                     swapEntries(ArchiveEntry* entry1, ArchiveEntry* entry2) override;

	static bool isWmdArchive(MemChunk& mc);
	static bool isWmdArchive(const string& filename);

private:
	vector<uint8_t> tail_; // Padding after the last section
};

class WsdArchive : public WessArchive
{
public:
	WsdArchive() : WessArchive("wsd") {}

	bool open(MemChunk& mc) override;
	bool write(MemChunk& mc, bool update = true) override;

	static bool isWsdArchive(MemChunk& mc);
	static bool isWsdArchive(const string& filename);

private:
	vector<uint8_t> header_;
};

class WddArchive : public WessArchive
{
public:
	WddArchive() : WessArchive("wdd") {}

	using Archive::write;
	bool open(MemChunk& mc) override;
	bool write(MemChunk& mc, bool update = true) override;
	bool write(string_view filename, bool update = true) override;

	// The set of samples is fixed
	shared_ptr<ArchiveEntry> addEntry(shared_ptr<ArchiveEntry> entry, unsigned position = 0xFFFFFFFF, ArchiveDir* dir = nullptr) override;
	shared_ptr<ArchiveEntry> addNewEntry(string_view name = "", unsigned position = 0xFFFFFFFF, ArchiveDir* dir = nullptr) override;
	bool                     removeEntry(ArchiveEntry* entry, bool set_deleted = true) override;
	bool                     moveEntry(ArchiveEntry* entry, unsigned position = 0xFFFFFFFF, ArchiveDir* dir = nullptr) override;
	bool                     swapEntries(unsigned index1, unsigned index2, ArchiveDir* dir = nullptr) override;
	bool                     swapEntries(ArchiveEntry* entry1, ArchiveEntry* entry2) override;

	static bool   isWddArchive(const string& filename);
	static string findWmd(const string& wdd_filename);

private:
	string          wmd_path_;      // The WMD the sample table was read from
	vector<uint8_t> wmd_data_;      // Its contents (updated when saving)
	vector<uint8_t> wmd_updated_;   // WMD with the new sample table, written alongside the WDD
};

// -----------------------------------------------------------------------------
// WESS sample entries (the entries of a WddArchive): playback, WAV export and import.
// The entries hold the raw ADPCM, the codebook needed to decode them is kept in
// the entry's extra properties.
// -----------------------------------------------------------------------------
namespace wessaudio
{
	// True if [entry] is a WESS sample
	bool isSample(const ArchiveEntry* entry);

	// Decodes the sample in [entry] to a 16 bit mono WAV in [wav]
	bool toWav(ArchiveEntry* entry, MemChunk& wav);

	// Replaces the data of [entry] with [wav] encoded as ADPCM using the entry's codebook.
	// The audio is resampled to the sample rate Doom 64 plays samples at (11025 Hz).
	bool fromWav(ArchiveEntry* entry, MemChunk& wav);

	// Imports [filename] into [entry]: a WAV is encoded, anything else is used as raw ADPCM
	// (for sequences a MIDI file is converted instead, see below)
	bool importFile(ArchiveEntry* entry, string_view filename);

	// True if [entry] is a WESS sequence (an entry of a WsdArchive)
	bool isSequence(const ArchiveEntry* entry);

	// Converts the sequence in [entry] to a Standard MIDI file in [midi]
	bool toMidi(ArchiveEntry* entry, MemChunk& midi);

	// Replaces the data of [entry] with the sequence made from the MIDI file [midi]
	bool fromMidi(ArchiveEntry* entry, MemChunk& midi);

	// True if [entry] is a sample or a sequence, ie. it is exported/imported as WAV/MIDI
	bool isConvertible(const ArchiveEntry* entry);

	// Converts [entry] to the file it is exported as (WAV for samples, MIDI for sequences)
	bool toExportFormat(ArchiveEntry* entry, MemChunk& out);
} // namespace wessaudio
} // namespace slade

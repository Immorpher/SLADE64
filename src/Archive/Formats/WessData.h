#pragma once

// -----------------------------------------------------------------------------
// Doom 64 WESS sound files (all big-endian), independent of SLADE's archive classes
//
//  WMD ("SN64"): the sound bank. 56 byte header, then (in this order)
//      PATCHES      npatch    x 4 bytes (+4 trailing bytes)      length, offset
//      SUBPATCHES   nsubpatch x 20 bytes (+4 trailing bytes)     instrument zones
//      WAVTABLE     nsfx      x 24 bytes                         start, size, pad, pitch, loop id, pad
//      LOOPS        8 byte header (nsfx, 0, nloops, nsfx) + nloops x 48 bytes
//      PREDICTORS   nsfx      x 264 bytes                        ADPCM codebook per sample
//    The file can have a few padding bytes after that (kept as 'tail').
//
//  WSD ("SSEQ"): the sequences (music + sound effects). 32 byte header, one 16 byte
//      entry per sequence (ntracks, length, offset), then each sequence's tracks.
//      Each track has a 20 byte header, an optional 4 byte loop id (if the track
//      loops), then its event data. Sequences are padded to a multiple of 8 bytes.
//
//  WDD: raw ADPCM sample data with no header. The samples are located by the
//      WAVTABLE of the matching WMD (start, size), and are stored back to back.
// -----------------------------------------------------------------------------

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

namespace slade::wess
{
using Bytes = std::vector<uint8_t>;

namespace detail
{
	inline uint16_t be16(const uint8_t* p)
	{
		return static_cast<uint16_t>((p[0] << 8) | p[1]);
	}
	inline uint32_t be32(const uint8_t* p)
	{
		return (static_cast<uint32_t>(p[0]) << 24) | (static_cast<uint32_t>(p[1]) << 16)
			   | (static_cast<uint32_t>(p[2]) << 8) | p[3];
	}
	inline void put16(uint8_t* p, uint32_t v)
	{
		p[0] = static_cast<uint8_t>(v >> 8);
		p[1] = static_cast<uint8_t>(v);
	}
	inline void put32(uint8_t* p, uint32_t v)
	{
		p[0] = static_cast<uint8_t>(v >> 24);
		p[1] = static_cast<uint8_t>(v >> 16);
		p[2] = static_cast<uint8_t>(v >> 8);
		p[3] = static_cast<uint8_t>(v);
	}
} // namespace detail

// -----------------------------------------------------------------------------
// WMD
// -----------------------------------------------------------------------------
constexpr size_t wmd_header_size    = 56;
constexpr size_t wmd_patch_size     = 4;
constexpr size_t wmd_subpatch_size  = 20;
constexpr size_t wmd_wavtable_size  = 24;
constexpr size_t wmd_loop_size      = 48;
constexpr size_t wmd_loophead_size  = 8;
constexpr size_t wmd_predictor_size = 264;

struct Wmd
{
	Bytes header;
	Bytes patches;     // includes the 4 trailing bytes
	Bytes subpatches;  // includes the 4 trailing bytes
	Bytes wavtable;
	Bytes loops;       // loop header + loop table
	Bytes predictors;
	Bytes tail;        // padding after the predictors

	size_t numSamples() const { return wavtable.size() / wmd_wavtable_size; }

	// The 264 byte ADPCM codebook of sample [i]
	Bytes book(size_t i) const
	{
		return Bytes(predictors.begin() + i * wmd_predictor_size, predictors.begin() + (i + 1) * wmd_predictor_size);
	}

	// Playback rate of sample [i] at its root key. The engine plays at 22050 Hz and shifts every
	// sample by its WAVTABLE "default pitch" (in cents): -1200 (most samples) is half speed = 11025 Hz.
	int sampleRate(size_t i) const
	{
		const int32_t cents = static_cast<int32_t>(detail::be32(&wavtable[i * wmd_wavtable_size + 12]));
		const double  rate  = 22050.0 * std::pow(2.0, cents / 1200.0);
		return (rate >= 2000.0 && rate <= 96000.0) ? static_cast<int>(rate + 0.5) : 11025;
	}

	// Start/size of sample [i] in the WDD
	uint32_t sampleStart(size_t i) const { return detail::be32(&wavtable[i * wmd_wavtable_size]); }
	uint32_t sampleSize(size_t i) const { return detail::be32(&wavtable[i * wmd_wavtable_size + 4]); }
};

// Reads a WMD, returns false (and sets [err]) if it isn't one
inline bool parseWmd(const uint8_t* data, size_t size, Wmd& out, std::string& err)
{
	using namespace detail;

	if (size < wmd_header_size || std::memcmp(data, "SN64", 4) != 0)
	{
		err = "Not a WMD (SN64) file";
		return false;
	}

	const size_t npatch   = be16(data + 36);
	const size_t patchsz  = be16(data + 38);
	const size_t nsub     = be16(data + 40);
	const size_t subsz    = be16(data + 42);
	const size_t nsfx     = be16(data + 44);
	const size_t sfxsz    = be16(data + 46);
	if (patchsz != wmd_patch_size || subsz != wmd_subpatch_size || sfxsz != wmd_wavtable_size)
	{
		err = "Unsupported WMD structure sizes";
		return false;
	}

	size_t p = wmd_header_size;
	auto   take = [&](Bytes& dst, size_t n) {
        if (p + n > size)
            return false;
        dst.assign(data + p, data + p + n);
        p += n;
        return true;
	};

	out = {};
	out.header.assign(data, data + wmd_header_size);
	if (!take(out.patches, npatch * wmd_patch_size + 4) || !take(out.subpatches, nsub * wmd_subpatch_size + 4)
		|| !take(out.wavtable, nsfx * wmd_wavtable_size))
	{
		err = "WMD is truncated";
		return false;
	}

	if (p + wmd_loophead_size > size)
	{
		err = "WMD is truncated";
		return false;
	}
	const size_t nloops = be16(data + p + 4);
	if (!take(out.loops, wmd_loophead_size + nloops * wmd_loop_size) || !take(out.predictors, nsfx * wmd_predictor_size))
	{
		err = "WMD is truncated";
		return false;
	}

	out.tail.assign(data + p, data + size);
	return true;
}

// Checks that the tables of [wmd] fit together (sizes, and that patches, patch maps and the
// wave table only refer to things that exist). A bank that doesn't would crash the game.
inline bool validateWmd(const Wmd& wmd, std::string& err)
{
	using namespace detail;

	if (wmd.header.size() != wmd_header_size)
	{
		err = "The WMD header must be 56 bytes";
		return false;
	}
	if (wmd.patches.size() < 4 || (wmd.patches.size() - 4) % wmd_patch_size)
	{
		err = "PATCHES: the size must be 4 bytes per patch plus 4 trailing bytes";
		return false;
	}
	if (wmd.subpatches.size() < 4 || (wmd.subpatches.size() - 4) % wmd_subpatch_size)
	{
		err = "SUBPATCHES: the size must be 20 bytes per patch map plus 4 trailing bytes";
		return false;
	}
	if (wmd.wavtable.size() % wmd_wavtable_size)
	{
		err = "WAVTABLE: the size must be 24 bytes per sample";
		return false;
	}
	if (wmd.loops.size() < wmd_loophead_size || (wmd.loops.size() - wmd_loophead_size) % wmd_loop_size)
	{
		err = "LOOPS: the size must be an 8 byte header plus 48 bytes per loop";
		return false;
	}

	const size_t npatch = (wmd.patches.size() - 4) / wmd_patch_size;
	const size_t nsub   = (wmd.subpatches.size() - 4) / wmd_subpatch_size;
	const size_t nsfx   = wmd.numSamples();
	const size_t nloops = (wmd.loops.size() - wmd_loophead_size) / wmd_loop_size;

	if (wmd.predictors.size() != nsfx * wmd_predictor_size)
	{
		err = "PREDICTORS: there must be one 264 byte codebook for each row of the WAVTABLE ("
			  + std::to_string(nsfx) + " samples, but " + std::to_string(wmd.predictors.size() / wmd_predictor_size)
			  + " codebooks)";
		return false;
	}

	for (size_t i = 0; i < npatch; i++)
	{
		const uint8_t* row   = &wmd.patches[i * wmd_patch_size];
		const size_t   count = row[0];
		const size_t   first = be16(row + 2);
		if (first + count > nsub)
		{
			err = "PATCHES: patch " + std::to_string(i) + " uses patch maps " + std::to_string(first) + " to "
				  + std::to_string(first + count - 1) + ", but there are only " + std::to_string(nsub);
			return false;
		}
	}

	for (size_t i = 0; i < nsub; i++)
	{
		const size_t sample = be16(&wmd.subpatches[i * wmd_subpatch_size + 10]);
		if (sample >= nsfx)
		{
			err = "SUBPATCHES: patch map " + std::to_string(i) + " uses sample " + std::to_string(sample)
				  + ", but there are only " + std::to_string(nsfx);
			return false;
		}
	}

	for (size_t i = 0; i < nsfx; i++)
	{
		const uint32_t id = be32(&wmd.wavtable[i * wmd_wavtable_size + 16]);
		if (id != 0xFFFFFFFFu && id >= nloops)
		{
			err = "WAVTABLE: sample " + std::to_string(i) + " uses loop " + std::to_string(id) + ", but there are only "
				  + std::to_string(nloops);
			return false;
		}
	}
	return true;
}

// Writes [wmd] to bytes, updating the counts in the header and loop table
inline Bytes buildWmd(const Wmd& wmd)
{
	using namespace detail;

	const Bytes& cheader = wmd.header;
	Bytes        header  = cheader;
	Bytes        loops   = wmd.loops;

	const size_t npatch = wmd.patches.size() >= 4 ? (wmd.patches.size() - 4) / wmd_patch_size : 0;
	const size_t nsub   = wmd.subpatches.size() >= 4 ? (wmd.subpatches.size() - 4) / wmd_subpatch_size : 0;
	const size_t nsfx   = wmd.wavtable.size() / wmd_wavtable_size;
	const size_t nloops = loops.size() >= wmd_loophead_size ? (loops.size() - wmd_loophead_size) / wmd_loop_size : 0;

	put16(&header[36], npatch);
	put16(&header[40], nsub);
	put16(&header[44], nsfx);
	if (loops.size() >= wmd_loophead_size)
	{
		put16(&loops[0], nsfx);
		put16(&loops[4], nloops);
		put16(&loops[6], nsfx);
	}

	const size_t body = wmd.patches.size() + wmd.subpatches.size() + wmd.wavtable.size() + loops.size()
						+ wmd.predictors.size();
	put32(&header[24], static_cast<uint32_t>(body));

	Bytes out;
	out.reserve(wmd_header_size + body + wmd.tail.size());
	const Bytes* parts[] = { &header, &wmd.patches, &wmd.subpatches, &wmd.wavtable, &loops, &wmd.predictors, &wmd.tail };
	for (const Bytes* part : parts)
		out.insert(out.end(), part->begin(), part->end());
	return out;
}

// -----------------------------------------------------------------------------
// WSD
// -----------------------------------------------------------------------------
constexpr size_t wsd_header_size = 32;
constexpr size_t wsd_entry_size  = 16;
constexpr size_t wsd_track_size  = 20;

struct Wsd
{
	Bytes              header;    // 32 bytes (count and entry table size are updated when writing)
	std::vector<Bytes> sequences; // Each sequence's tracks (including any padding)
};

// Number of tracks in a sequence's data (counts track headers until the data runs out)
inline size_t countTracks(const Bytes& seq)
{
	using namespace detail;

	size_t p = 0, n = 0;
	while (p + wsd_track_size <= seq.size())
	{
		const size_t datalen = be32(&seq[p + 16]);
		const size_t nlabels = be16(&seq[p + 14]);
		const size_t next    = p + wsd_track_size + datalen + nlabels * 4;
		if (next > seq.size())
			break;
		p = next;
		n++;
	}
	return n;
}

inline bool parseWsd(const uint8_t* data, size_t size, Wsd& out, std::string& err)
{
	using namespace detail;

	if (size < wsd_header_size || std::memcmp(data, "SSEQ", 4) != 0)
	{
		err = "Not a WSD (SSEQ) file";
		return false;
	}

	const size_t nseq      = be32(data + 12);
	const size_t table_len = be32(data + 24);
	if (table_len != nseq * wsd_entry_size || wsd_header_size + table_len > size)
	{
		err = "Invalid WSD entry table";
		return false;
	}

	out = {};
	out.header.assign(data, data + wsd_header_size);

	const size_t base = wsd_header_size + table_len;
	for (size_t i = 0; i < nseq; i++)
	{
		const uint8_t* e      = data + wsd_header_size + i * wsd_entry_size;
		const size_t   length = be32(e + 4);
		const size_t   offset = be32(e + 8);
		if (base + offset + length > size)
		{
			err = "WSD sequence is outside the file";
			return false;
		}
		out.sequences.emplace_back(data + base + offset, data + base + offset + length);
	}
	return true;
}

// Writes [wsd], sequences are padded to a multiple of 8 bytes and packed back to back
inline Bytes buildWsd(const Wsd& wsd)
{
	using namespace detail;

	Bytes header = wsd.header;
	const size_t n = wsd.sequences.size();
	put32(&header[12], static_cast<uint32_t>(n));
	put32(&header[24], static_cast<uint32_t>(n * wsd_entry_size));

	Bytes table(n * wsd_entry_size, 0);
	Bytes body;
	for (size_t i = 0; i < n; i++)
	{
		Bytes seq = wsd.sequences[i];
		while (seq.size() % 8)
			seq.push_back(0);

		uint8_t* e = &table[i * wsd_entry_size];
		put16(e, static_cast<uint32_t>(countTracks(seq)));
		put32(e + 4, static_cast<uint32_t>(seq.size()));
		put32(e + 8, static_cast<uint32_t>(body.size()));
		body.insert(body.end(), seq.begin(), seq.end());
	}

	Bytes out = header;
	out.insert(out.end(), table.begin(), table.end());
	out.insert(out.end(), body.begin(), body.end());
	return out;
}

// -----------------------------------------------------------------------------
// WDD
// -----------------------------------------------------------------------------

// Splits [wdd] into samples using the WAVTABLE of [wmd]
inline bool splitWdd(const uint8_t* data, size_t size, const Wmd& wmd, std::vector<Bytes>& out, std::string& err)
{
	out.clear();
	for (size_t i = 0; i < wmd.numSamples(); i++)
	{
		const size_t start = wmd.sampleStart(i);
		const size_t len   = wmd.sampleSize(i);
		if (start + len > size)
		{
			err = "WDD is smaller than its WMD says";
			return false;
		}
		out.emplace_back(data + start, data + start + len);
	}
	return true;
}

// Packs [samples] back to back as a WDD and updates their start/size in [wmd]'s WAVTABLE
inline Bytes buildWdd(const std::vector<Bytes>& samples, Wmd& wmd)
{
	using namespace detail;

	// Every sample in the original data starts on an even address and has an even size,
	// so odd length samples (eg. ADPCM made of 9 byte frames) get a padding byte
	Bytes  out;
	for (size_t i = 0; i < samples.size() && i < wmd.numSamples(); i++)
	{
		const size_t padded = samples[i].size() + (samples[i].size() & 1);
		uint8_t*     row    = &wmd.wavtable[i * wmd_wavtable_size];
		put32(row, static_cast<uint32_t>(out.size()));
		put32(row + 4, static_cast<uint32_t>(padded));
		out.insert(out.end(), samples[i].begin(), samples[i].end());
		if (padded != samples[i].size())
			out.push_back(0);
	}
	return out;
}
} // namespace slade::wess

#pragma once

// -----------------------------------------------------------------------------
// Doom 64 WESS sequences (the entries of a WSD) <-> Standard MIDI files
//
// A sequence is a list of tracks, each one has
//   20 byte header: voices_type, reverb, patch (u16), pitch (s16), volume, pan,
//                   substack_count, mutebits, ppq (u16), qpm (u16), label_count (u16),
//                   data_size (u32)
//   label_count x u32: offsets (in the event data) of the label events
//   event data: delta time (variable length, same as MIDI), command byte, parameters.
//               Ends with the TrkEnd (0x22) command, then padding up to a multiple of 4 bytes.
// Anything after the last track is kept as 'trailing' bytes.
//
// MIDI mapping (one MIDI track per sequence track, plus a first track with the tempo):
//   NoteOn/NoteOff        -> note on / note off
//   PatchChg              -> bank select (patch / 128) + program change (patch % 128)
//   PitchMod              -> pitch bend
//   Volume/Pan/Pedal/...  -> controllers 7 / 10 / 64 / 1 / 91 / 93
//   label (NullEvent)     -> marker "loopStart" ("loopStart N" for the label N)
//   TrkJump               -> marker "loopEnd" ("loopEnd N")
//   TrkEnd                -> end of track
//   anything else, and the header fields that MIDI can't hold (voices type, reverb, padding,
//   ...), is stored in sequencer specific meta events ("D64"), so converting to MIDI and back
//   gives back exactly the same bytes.
// -----------------------------------------------------------------------------

#include "WessData.h"
#include <algorithm>
#include <cmath>
#include <cctype>
#include <map>

namespace slade::wess
{
namespace seqdetail
{
	// Total length of each command (including the command byte), 0 = not a track command
	constexpr uint8_t cmd_len[36] = { 0, 0, 0, 0, 0, 0, 0, 3, 2, 3, 2, 2, 2, 2, 2, 2, 2, 3,
									  2, 4, 5, 5, 2, 2, 3, 3, 3, 3, 1, 1, 3, 3, 3, 1, 1, 1 };

	enum Cmd : uint8_t
	{
		PatchChg   = 0x07,
		PitchMod   = 0x09,
		ModuMod    = 0x0B,
		VolumeMod  = 0x0C,
		PanMod     = 0x0D,
		PedalMod   = 0x0E,
		ReverbMod  = 0x0F,
		ChorusMod  = 0x10,
		NoteOn     = 0x11,
		NoteOff    = 0x12,
		SeqTempo   = 0x19,
		TrkJump    = 0x20,
		TrkEnd     = 0x22,
		NullEvent  = 0x23,
	};

	inline void putVlq(Bytes& out, uint32_t v)
	{
		uint8_t tmp[5];
		int     n = 0;
		tmp[n++]  = v & 0x7F;
		while (v >>= 7)
			tmp[n++] = static_cast<uint8_t>((v & 0x7F) | 0x80);
		while (n--)
			out.push_back(tmp[n]);
	}

	inline bool getVlq(const uint8_t* d, size_t size, size_t& p, uint32_t& v)
	{
		v = 0;
		for (int i = 0; i < 5; i++)
		{
			if (p >= size)
				return false;
			const uint8_t c = d[p++];
			v               = (v << 7) | (c & 0x7F);
			if (!(c & 0x80))
				return true;
		}
		return false;
	}

	// Reads the events of [data] and checks them. [end] is the offset after the TrkEnd command.
	inline bool checkEvents(const Bytes& data, size_t& end)
	{
		size_t p = 0;
		while (true)
		{
			uint32_t delta;
			if (!getVlq(data.data(), data.size(), p, delta) || p >= data.size())
				return false;
			const uint8_t op = data[p];
			if (op > NullEvent || cmd_len[op] == 0 || p + cmd_len[op] > data.size())
				return false;
			p += cmd_len[op];
			if (op == TrkEnd)
			{
				end = p;
				return true;
			}
		}
	}

	inline void put16be(Bytes& b, uint32_t v)
	{
		b.push_back(static_cast<uint8_t>(v >> 8));
		b.push_back(static_cast<uint8_t>(v));
	}
	inline void put32be(Bytes& b, uint32_t v)
	{
		put16be(b, v >> 16);
		put16be(b, v & 0xFFFF);
	}
} // namespace seqdetail

struct SeqTrack
{
	uint8_t               voices = 1, reverb = 0;
	uint16_t              patch = 0;
	int16_t               pitch = 0;
	uint8_t               volume = 127, pan = 64, substack = 0, mute = 0;
	uint16_t              ppq = 120, qpm = 120;
	std::vector<uint32_t> labels; // offsets of the label events in [data]
	Bytes                 data;   // events, ending with the TrkEnd command
	Bytes                 pad;    // bytes after the TrkEnd (up to a multiple of 4)
};

// Splits the raw bytes of a WSD sequence into tracks. Whatever follows the last track goes to [trailing].
inline bool parseSeq(const Bytes& seq, std::vector<SeqTrack>& tracks, Bytes& trailing, std::string& err)
{
	using namespace detail;
	tracks.clear();
	trailing.clear();

	size_t p = 0;
	while (p + wsd_track_size <= seq.size())
	{
		const uint8_t* h        = &seq[p];
		const size_t   nlabels  = be16(h + 14);
		const size_t   datasize = be32(h + 16);
		const size_t   q        = p + wsd_track_size + nlabels * 4;
		if (q + datasize > seq.size() || datasize == 0)
			break;

		SeqTrack t;
		t.voices   = h[0];
		t.reverb   = h[1];
		t.patch    = be16(h + 2);
		t.pitch    = static_cast<int16_t>(be16(h + 4));
		t.volume   = h[6];
		t.pan      = h[7];
		t.substack = h[8];
		t.mute     = h[9];
		t.ppq      = be16(h + 10);
		t.qpm      = be16(h + 12);
		for (size_t i = 0; i < nlabels; i++)
			t.labels.push_back(be32(&seq[p + wsd_track_size + i * 4]));

		Bytes data(seq.begin() + q, seq.begin() + q + datasize);
		size_t end = 0;
		if (!seqdetail::checkEvents(data, end))
		{
			if (tracks.empty())
			{
				err = "This sequence has events that can't be read";
				return false;
			}
			break;
		}
		t.pad.assign(data.begin() + end, data.end());
		data.resize(end);
		t.data = std::move(data);

		tracks.push_back(std::move(t));
		p = q + datasize;
	}

	if (tracks.empty())
	{
		err = "This sequence has no tracks";
		return false;
	}
	trailing.assign(seq.begin() + p, seq.end());
	return true;
}

// Puts [tracks] back into the sequence format
inline Bytes buildSeq(const std::vector<SeqTrack>& tracks, const Bytes& trailing)
{
	using namespace seqdetail;
	Bytes out;
	for (const auto& t : tracks)
	{
		// Padding after the events up to a multiple of 4 bytes
		Bytes pad = t.pad;
		pad.resize((4 - t.data.size() % 4) % 4, 0);

		out.push_back(t.voices);
		out.push_back(t.reverb);
		put16be(out, t.patch);
		put16be(out, static_cast<uint16_t>(t.pitch));
		out.push_back(t.volume);
		out.push_back(t.pan);
		out.push_back(t.substack);
		out.push_back(t.mute);
		put16be(out, t.ppq);
		put16be(out, t.qpm);
		put16be(out, static_cast<uint32_t>(t.labels.size()));
		put32be(out, static_cast<uint32_t>(t.data.size() + pad.size()));
		for (auto l : t.labels)
			put32be(out, l);
		out.insert(out.end(), t.data.begin(), t.data.end());
		out.insert(out.end(), pad.begin(), pad.end());
	}
	out.insert(out.end(), trailing.begin(), trailing.end());
	return out;
}

// -----------------------------------------------------------------------------
// Sequence -> MIDI
// -----------------------------------------------------------------------------
namespace seqdetail
{
	inline void metaEvent(Bytes& out, uint32_t delta, uint8_t type, const Bytes& payload)
	{
		putVlq(out, delta);
		out.push_back(0xFF);
		out.push_back(type);
		putVlq(out, static_cast<uint32_t>(payload.size()));
		out.insert(out.end(), payload.begin(), payload.end());
	}

	inline Bytes d64Payload(char kind, const Bytes& content)
	{
		Bytes p = { 'D', '6', '4', static_cast<uint8_t>(kind) };
		p.insert(p.end(), content.begin(), content.end());
		return p;
	}

	inline void marker(Bytes& out, uint32_t delta, const std::string& text)
	{
		metaEvent(out, delta, 0x06, Bytes(text.begin(), text.end()));
	}

	inline void chanEvent(Bytes& out, uint32_t delta, uint8_t status, uint8_t d1, int d2 = -1)
	{
		putVlq(out, delta);
		out.push_back(status);
		out.push_back(d1);
		if (d2 >= 0)
			out.push_back(static_cast<uint8_t>(d2));
	}

	inline void mtrk(Bytes& file, const Bytes& body)
	{
		file.insert(file.end(), { 'M', 'T', 'r', 'k' });
		put32be(file, static_cast<uint32_t>(body.size()));
		file.insert(file.end(), body.begin(), body.end());
	}

	// MIDI track of a sequence track
	inline Bytes trackToMidi(const SeqTrack& t, int index)
	{
		const uint8_t ch = static_cast<uint8_t>((index < 9 ? index : index + 1) & 15);
		Bytes         b;

		// Header fields MIDI has no place for (and the padding bytes)
		Bytes hdr = { t.voices, t.reverb, static_cast<uint8_t>(t.patch >> 8), static_cast<uint8_t>(t.patch),
					  static_cast<uint8_t>(static_cast<uint16_t>(t.pitch) >> 8), static_cast<uint8_t>(t.pitch),
					  t.volume, t.pan, t.substack, t.mute, static_cast<uint8_t>(t.ppq >> 8), static_cast<uint8_t>(t.ppq),
					  static_cast<uint8_t>(t.qpm >> 8), static_cast<uint8_t>(t.qpm) };
		hdr.insert(hdr.end(), t.pad.begin(), t.pad.end());
		metaEvent(b, 0, 0x7F, d64Payload('T', hdr));

		// Initial state
		chanEvent(b, 0, 0xB0 | ch, 0, (t.patch >> 7) & 127);
		chanEvent(b, 0, 0xC0 | ch, t.patch & 127);
		chanEvent(b, 0, 0xB0 | ch, 7, t.volume & 127);
		chanEvent(b, 0, 0xB0 | ch, 10, t.pan & 127);
		const int bend = std::clamp(int(t.pitch) + 8192, 0, 16383);
		chanEvent(b, 0, 0xE0 | ch, bend & 127, bend >> 7);

		auto raw = [&](uint32_t delta, const uint8_t* e, size_t len) {
			metaEvent(b, delta, 0x7F, d64Payload('E', Bytes(e, e + len)));
		};

		size_t p = 0;
		while (p < t.data.size())
		{
			uint32_t delta;
			getVlq(t.data.data(), t.data.size(), p, delta);
			const size_t   oppos = p;
			const uint8_t* e     = &t.data[p];
			const uint8_t  op    = e[0];
			p += cmd_len[op];

			auto cc = [&](uint8_t num) {
				if (e[1] > 127)
					raw(delta, e, cmd_len[op]);
				else
					chanEvent(b, delta, 0xB0 | ch, num, e[1]);
			};

			switch (op)
			{
			case NoteOn:
				if (e[1] > 127 || e[2] == 0 || e[2] > 127)
					raw(delta, e, 3);
				else
					chanEvent(b, delta, 0x90 | ch, e[1], e[2]);
				break;
			case NoteOff:
				if (e[1] > 127)
					raw(delta, e, 2);
				else
					chanEvent(b, delta, 0x80 | ch, e[1], 64);
				break;
			case PitchMod:
			{
				const int v = static_cast<int16_t>(e[1] | (e[2] << 8)) + 8192;
				if (v < 0 || v > 16383)
					raw(delta, e, 3);
				else
					chanEvent(b, delta, 0xE0 | ch, v & 127, v >> 7);
				break;
			}
			case PatchChg:
			{
				const unsigned patch = e[1] | (e[2] << 8);
				if (patch > 16383)
					raw(delta, e, 3);
				else
				{
					chanEvent(b, delta, 0xB0 | ch, 0, patch >> 7);
					chanEvent(b, 0, 0xC0 | ch, patch & 127);
				}
				break;
			}
			case ModuMod:   cc(1); break;
			case VolumeMod: cc(7); break;
			case PanMod:    cc(10); break;
			case PedalMod:  cc(64); break;
			case ReverbMod: cc(91); break;
			case ChorusMod: cc(93); break;
			case NullEvent:
			{
				const auto it = std::find(t.labels.begin(), t.labels.end(), static_cast<uint32_t>(oppos));
				if (it == t.labels.end())
					raw(delta, e, 1);
				else
				{
					const size_t k = it - t.labels.begin();
					marker(b, delta, k ? "loopStart " + std::to_string(k) : "loopStart");
				}
				break;
			}
			case TrkJump:
			{
				const size_t k = e[1] | (e[2] << 8);
				if (k >= t.labels.size())
					raw(delta, e, 3);
				else
					marker(b, delta, k ? "loopEnd " + std::to_string(k) : "loopEnd");
				break;
			}
			case TrkEnd:
				metaEvent(b, delta, 0x2F, {});
				break;
			default:
				raw(delta, e, cmd_len[op]);
				break;
			}
		}
		return b;
	}
} // namespace seqdetail

// Converts the raw bytes of a WSD sequence to a Standard MIDI file (format 1)
inline bool seqToMidi(const Bytes& seq, Bytes& midi, std::string& err)
{
	using namespace seqdetail;

	std::vector<SeqTrack> tracks;
	Bytes                 trailing;
	if (!parseSeq(seq, tracks, trailing, err))
		return false;

	const unsigned ppq = tracks[0].ppq ? tracks[0].ppq : 120;
	const unsigned qpm = tracks[0].qpm ? tracks[0].qpm : 120;

	midi.clear();
	midi.insert(midi.end(), { 'M', 'T', 'h', 'd' });
	put32be(midi, 6);
	put16be(midi, 1);
	put16be(midi, static_cast<uint32_t>(tracks.size() + 1));
	put16be(midi, ppq);

	// Tempo track
	Bytes tempo;
	const uint32_t us = 60000000u / qpm;
	metaEvent(tempo, 0, 0x51, { static_cast<uint8_t>(us >> 16), static_cast<uint8_t>(us >> 8), static_cast<uint8_t>(us) });
	metaEvent(tempo, 0, 0x7F, d64Payload('S', trailing));
	metaEvent(tempo, 0, 0x2F, {});
	mtrk(midi, tempo);

	for (size_t i = 0; i < tracks.size(); i++)
		mtrk(midi, trackToMidi(tracks[i], static_cast<int>(i)));
	return true;
}

// -----------------------------------------------------------------------------
// MIDI -> Sequence
// -----------------------------------------------------------------------------
namespace seqdetail
{
	struct MidiEv
	{
		uint32_t tick = 0;
		bool     meta = false;
		uint8_t  type = 0; // meta type or channel message status (high nibble)
		uint8_t  chan = 0;
		uint8_t  d1 = 0, d2 = 0;
		Bytes    payload;
	};

	struct MidiTrack
	{
		std::vector<MidiEv> events;
		uint32_t            end = 0;
	};

	inline bool parseMidi(const uint8_t* d, size_t size, int& format, unsigned& division, std::vector<MidiTrack>& tracks, std::string& err)
	{
		using namespace detail;
		if (size < 14 || std::memcmp(d, "MThd", 4) != 0)
		{
			err = "Not a MIDI file";
			return false;
		}
		const size_t hlen = be32(d + 4);
		format            = be16(d + 8);
		const size_t ntr  = be16(d + 10);
		division          = be16(d + 12);
		if (division & 0x8000 || division == 0)
		{
			err = "MIDI files with SMPTE time division aren't supported";
			return false;
		}

		size_t p = 8 + hlen;
		for (size_t i = 0; i < ntr && p + 8 <= size; i++)
		{
			const size_t len = be32(d + p + 4);
			if (std::memcmp(d + p, "MTrk", 4) != 0)
			{
				// Skip unknown chunks
				p += 8 + len;
				i--;
				continue;
			}
			const size_t end = std::min(size, p + 8 + len);
			size_t       q   = p + 8;

			MidiTrack tr;
			uint32_t  tick   = 0;
			uint8_t   status = 0;
			while (q < end)
			{
				uint32_t delta;
				if (!getVlq(d, end, q, delta) || q >= end)
					break;
				tick += delta;

				MidiEv ev;
				ev.tick = tick;
				if (d[q] == 0xFF)
				{
					if (q + 2 > end)
						break;
					ev.meta = true;
					ev.type = d[q + 1];
					q += 2;
					uint32_t mlen;
					if (!getVlq(d, end, q, mlen) || q + mlen > end)
						break;
					ev.payload.assign(d + q, d + q + mlen);
					q += mlen;
					if (ev.type == 0x2F)
					{
						tr.end = tick;
						tr.events.push_back(ev);
						break;
					}
				}
				else if (d[q] == 0xF0 || d[q] == 0xF7)
				{
					q++;
					uint32_t slen;
					if (!getVlq(d, end, q, slen))
						break;
					q += slen;
					continue;
				}
				else
				{
					if (d[q] & 0x80)
						status = d[q++];
					else if (!status)
						break;
					const uint8_t hi = status & 0xF0;
					ev.type          = hi;
					ev.chan          = status & 15;
					const int n      = (hi == 0xC0 || hi == 0xD0) ? 1 : 2;
					if (q + n > end)
						break;
					ev.d1 = d[q];
					ev.d2 = n == 2 ? d[q + 1] : 0;
					q += n;
				}
				tr.end = tick;
				tr.events.push_back(std::move(ev));
			}
			tracks.push_back(std::move(tr));
			p = end;
		}
		return !tracks.empty();
	}

	// An event of a Doom track still on the MIDI time line
	struct InEv
	{
		uint32_t tick;
		int      label = -1; // >= 0: label marker (NullEvent)
		Bytes    cmd;        // command bytes otherwise
	};

	inline int markerNumber(const std::string& text, const char* word)
	{
		// "word" or "word N" (case insensitive), -1 if it isn't one
		std::string t;
		for (char c : text)
			t.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(c))));
		std::string w;
		for (const char* c = word; *c; c++)
			w.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(*c))));
		if (t.compare(0, w.size(), w) != 0)
			return -1;
		const std::string rest = t.substr(w.size());
		if (rest.empty())
			return 0;
		if (rest[0] != ' ')
			return -1;
		try
		{
			return std::stoi(rest.substr(1));
		}
		catch (...)
		{
			return -1;
		}
	}
} // namespace seqdetail

// Converts a Standard MIDI file to the raw bytes of a WSD sequence
inline bool midiToSeq(const uint8_t* midi, size_t size, Bytes& seq, std::string& err)
{
	using namespace seqdetail;

	int                    format;
	unsigned               division;
	std::vector<MidiTrack> mt;
	if (!parseMidi(midi, size, format, division, mt, err))
		return false;

	// Tempo (the first one decides the track tempo, later ones become tempo commands)
	unsigned                                 qpm = 120;
	bool                                     got_tempo = false;
	std::vector<std::pair<uint32_t, unsigned>> tempo_changes;
	Bytes                                    trailing;
	for (const auto& tr : mt)
		for (const auto& ev : tr.events)
		{
			if (!ev.meta)
				continue;
			if (ev.type == 0x51 && ev.payload.size() == 3)
			{
				const uint32_t us = (ev.payload[0] << 16) | (ev.payload[1] << 8) | ev.payload[2];
				const unsigned q  = us ? static_cast<unsigned>(std::clamp(std::lround(60000000.0 / us), 1L, 65535L)) : 120;
				if (!got_tempo && ev.tick == 0)
				{
					qpm       = q;
					got_tempo = true;
				}
				else
					tempo_changes.emplace_back(ev.tick, q);
			}
			else if (ev.type == 0x7F && ev.payload.size() >= 4 && std::memcmp(ev.payload.data(), "D64S", 4) == 0)
				trailing.assign(ev.payload.begin() + 4, ev.payload.end());
		}

	std::vector<SeqTrack> out;

	for (const auto& tr : mt)
	{
		// One Doom track per MIDI channel used in this MIDI track
		std::vector<uint8_t> channels;
		for (const auto& ev : tr.events)
			if (!ev.meta && std::find(channels.begin(), channels.end(), ev.chan) == channels.end())
				channels.push_back(ev.chan);

		for (size_t g = 0; g < channels.size(); g++)
		{
			const uint8_t ch = channels[g];
			SeqTrack      t;
			t.ppq = static_cast<uint16_t>(division);
			t.qpm = static_cast<uint16_t>(qpm);

			std::vector<InEv> evs;
			bool              have_prog = false, have_vol = false, have_pan = false, have_bend = false;
			unsigned          bank = 0;
			Bytes             padmeta;

			auto add = [&](uint32_t tick, std::initializer_list<uint8_t> cmd) { evs.push_back({ tick, -1, Bytes(cmd) }); };

			for (const auto& ev : tr.events)
			{
				if (ev.meta)
				{
					if (g != 0)
						continue; // meta events belong to the first channel's track
					if (ev.type == 0x7F && ev.payload.size() >= 4 && std::memcmp(ev.payload.data(), "D64T", 4) == 0 && ev.payload.size() >= 18)
					{
						const uint8_t* m = ev.payload.data() + 4;
						t.voices         = m[0];
						t.reverb         = m[1];
						t.patch          = static_cast<uint16_t>((m[2] << 8) | m[3]);
						t.pitch          = static_cast<int16_t>((m[4] << 8) | m[5]);
						t.volume         = m[6];
						t.pan            = m[7];
						t.substack       = m[8];
						t.mute           = m[9];
						padmeta.assign(ev.payload.begin() + 18, ev.payload.end());
					}
					else if (ev.type == 0x7F && ev.payload.size() >= 5 && std::memcmp(ev.payload.data(), "D64E", 4) == 0)
					{
						Bytes cmd(ev.payload.begin() + 4, ev.payload.end());
						if (cmd[0] <= NullEvent && cmd_len[cmd[0]] == cmd.size() && cmd[0] != TrkEnd)
							evs.push_back({ ev.tick, -1, cmd });
					}
					else if (ev.type == 0x06)
					{
						const std::string text(ev.payload.begin(), ev.payload.end());
						int               n;
						if ((n = markerNumber(text, "loopStart")) >= 0)
							evs.push_back({ ev.tick, n, {} });
						else if ((n = markerNumber(text, "loopEnd")) >= 0)
							add(ev.tick, { TrkJump, static_cast<uint8_t>(n & 255), static_cast<uint8_t>(n >> 8) });
					}
					continue;
				}
				if (ev.chan != ch)
					continue;

				const bool at_start = ev.tick == 0;
				switch (ev.type)
				{
				case 0x90:
					if (ev.d2)
						add(ev.tick, { NoteOn, ev.d1, ev.d2 });
					else
						add(ev.tick, { NoteOff, ev.d1 });
					break;
				case 0x80: add(ev.tick, { NoteOff, ev.d1 }); break;
				case 0xE0:
				{
					const int v = ((ev.d2 << 7) | ev.d1) - 8192;
					if (at_start && !have_bend)
					{
						have_bend = true;
						t.pitch   = static_cast<int16_t>(v);
					}
					else
						add(ev.tick, { PitchMod, static_cast<uint8_t>(v & 255), static_cast<uint8_t>((v >> 8) & 255) });
					break;
				}
				case 0xC0:
				{
					const unsigned patch = (bank << 7) | ev.d1;
					if (at_start && !have_prog)
					{
						have_prog = true;
						t.patch   = static_cast<uint16_t>(patch);
					}
					else
						add(ev.tick, { PatchChg, static_cast<uint8_t>(patch & 255), static_cast<uint8_t>(patch >> 8) });
					break;
				}
				case 0xB0:
					switch (ev.d1)
					{
					case 0: bank = ev.d2; break;
					case 7:
						if (at_start && !have_vol)
						{
							have_vol = true;
							t.volume = ev.d2;
						}
						else
							add(ev.tick, { VolumeMod, ev.d2 });
						break;
					case 10:
						if (at_start && !have_pan)
						{
							have_pan = true;
							t.pan    = ev.d2;
						}
						else
							add(ev.tick, { PanMod, ev.d2 });
						break;
					case 1: add(ev.tick, { ModuMod, ev.d2 }); break;
					case 64: add(ev.tick, { PedalMod, ev.d2 }); break;
					case 91: add(ev.tick, { ReverbMod, ev.d2 }); break;
					case 93: add(ev.tick, { ChorusMod, ev.d2 }); break;
					default: break; // other controllers have no equivalent
					}
					break;
				default: break; // aftertouch etc.
				}
			}

			// Tempo changes go to the first track of the song
			if (out.empty())
				for (const auto& [tick, q] : tempo_changes)
					add(tick, { SeqTempo, static_cast<uint8_t>(q & 255), static_cast<uint8_t>(q >> 8) });

			std::stable_sort(evs.begin(), evs.end(), [](const InEv& a, const InEv& b) { return a.tick < b.tick; });

			// Write the events
			uint32_t last = 0;
			int      maxlabel = -1;
			for (const auto& e : evs)
				maxlabel = std::max(maxlabel, e.label);
			t.labels.assign(maxlabel + 1, 0);
			for (const auto& e : evs)
			{
				putVlq(t.data, e.tick - last);
				last = e.tick;
				if (e.label >= 0)
				{
					t.labels[e.label] = static_cast<uint32_t>(t.data.size());
					t.data.push_back(NullEvent);
				}
				else
					t.data.insert(t.data.end(), e.cmd.begin(), e.cmd.end());
			}
			putVlq(t.data, std::max(tr.end, last) - last);
			t.data.push_back(TrkEnd);

			t.pad = padmeta;
			out.push_back(std::move(t));
		}
	}

	if (out.empty())
	{
		err = "The MIDI file has no notes or channel events";
		return false;
	}

	seq = buildSeq(out, trailing);
	return true;
}
// -----------------------------------------------------------------------------
// Full loop
//
// Rewrites a Standard MIDI file so every track that has channel events loops
// completely: any existing loopStart/loopEnd markers are removed, then a
// "loopStart" marker is put at tick 0 (after the initial setup events, before the
// first note) and a "loopEnd" marker at the very end, right before the end of
// track event. All tracks end on the same tick, so they can't drift apart.
// Events keep their absolute ticks, so nothing gets out of sync.
// -----------------------------------------------------------------------------
namespace seqdetail
{
	struct RawEv
	{
		uint32_t tick = 0;
		Bytes    raw;           // the event without its delta time
		bool     note    = false; // note on/off
		bool     channel = false; // any channel message
		bool     eot     = false; // end of track
		bool     loop    = false; // loopStart/loopEnd marker
	};
} // namespace seqdetail

inline bool addFullLoop(const uint8_t* midi, size_t size, Bytes& out, std::string& err)
{
	using namespace seqdetail;
	using namespace detail;

	if (size < 14 || std::memcmp(midi, "MThd", 4) != 0)
	{
		err = "Not a MIDI file";
		return false;
	}

	const size_t hlen = be32(midi + 4);
	size_t       p    = 8 + hlen;
	if (p > size)
	{
		err = "The MIDI file is truncated";
		return false;
	}

	struct Trk
	{
		std::vector<RawEv> ev;
		uint32_t           end         = 0;
		bool               has_channel = false;
	};
	std::vector<Trk> tracks;

	while (p + 8 <= size)
	{
		const size_t len = be32(midi + p + 4);
		if (std::memcmp(midi + p, "MTrk", 4) != 0)
		{
			p += 8 + len;
			continue;
		}
		const size_t end = std::min(size, p + 8 + len);
		size_t       q   = p + 8;

		Trk      tr;
		uint32_t tick   = 0;
		uint8_t  status = 0;
		while (q < end)
		{
			uint32_t delta;
			if (!getVlq(midi, end, q, delta) || q >= end)
				break;
			tick += delta;

			RawEv ev;
			ev.tick = tick;
			if (midi[q] == 0xFF)
			{
				if (q + 2 > end)
					break;
				const uint8_t type = midi[q + 1];
				size_t        r    = q + 2;
				uint32_t      mlen;
				if (!getVlq(midi, end, r, mlen) || r + mlen > end)
					break;
				ev.raw.assign(midi + q, midi + r + mlen);
				if (type == 0x2F)
					ev.eot = true;
				else if (type == 0x06 || type == 0x01)
				{
					const std::string text(reinterpret_cast<const char*>(midi + r), mlen);
					if (type == 0x06 && (markerNumber(text, "loopStart") >= 0 || markerNumber(text, "loopEnd") >= 0))
						ev.loop = true;
				}
				q = r + mlen;
				status = 0;
			}
			else if (midi[q] == 0xF0 || midi[q] == 0xF7)
			{
				size_t   r = q + 1;
				uint32_t slen;
				if (!getVlq(midi, end, r, slen) || r + slen > end)
					break;
				ev.raw.assign(midi + q, midi + r + slen);
				q      = r + slen;
				status = 0;
			}
			else
			{
				if (midi[q] & 0x80)
					status = midi[q++];
				else if (!status)
					break;
				const uint8_t hi = status & 0xF0;
				const int     n  = (hi == 0xC0 || hi == 0xD0) ? 1 : 2;
				if (q + n > end)
					break;
				ev.raw.push_back(status);
				ev.raw.insert(ev.raw.end(), midi + q, midi + q + n);
				ev.channel = true;
				ev.note    = hi == 0x80 || hi == 0x90;
				q += n;
			}

			tr.end = std::max(tr.end, tick);
			if (ev.channel)
				tr.has_channel = true;
			if (!ev.eot && !ev.loop)
				tr.ev.push_back(std::move(ev));
			else if (ev.eot)
			{
				tr.end = std::max(tr.end, tick);
				break; // nothing follows the end of track
			}
		}
		tracks.push_back(std::move(tr));
		p = end;
	}

	if (tracks.empty())
	{
		err = "The MIDI file has no tracks";
		return false;
	}

	// All tracks that will loop end on the same tick
	uint32_t loop_end = 0;
	bool     any      = false;
	for (auto& t : tracks)
		loop_end = std::max(loop_end, t.end);
	for (auto& t : tracks)
		any = any || t.has_channel;
	if (!any)
	{
		err = "The MIDI file has no notes or channel events";
		return false;
	}

	auto text_event = [](uint32_t tick, const char* text) {
		RawEv e;
		e.tick = tick;
		e.raw  = { 0xFF, 0x06, static_cast<uint8_t>(std::strlen(text)) };
		e.raw.insert(e.raw.end(), text, text + std::strlen(text));
		return e;
	};

	// Write the file back
	out.clear();
	out.insert(out.end(), midi, midi + 8 + hlen);
	// Fix the track count (tracks that aren't MTrk chunks are dropped)
	out[10] = static_cast<uint8_t>(tracks.size() >> 8);
	out[11] = static_cast<uint8_t>(tracks.size());

	for (auto& t : tracks)
	{
		if (t.has_channel)
		{
			// loopStart: tick 0, after the setup events and before the first note
			size_t at = 0;
			while (at < t.ev.size() && t.ev[at].tick == 0 && !t.ev[at].note)
				at++;
			t.ev.insert(t.ev.begin() + at, text_event(0, "loopStart"));
			t.ev.push_back(text_event(loop_end, "loopEnd"));
		}

		Bytes body;
		uint32_t last = 0;
		for (auto& e : t.ev)
		{
			putVlq(body, e.tick - last);
			last = e.tick;
			body.insert(body.end(), e.raw.begin(), e.raw.end());
		}
		const uint32_t eot_tick = t.has_channel ? loop_end : std::max(last, t.end);
		putVlq(body, eot_tick - last);
		body.push_back(0xFF);
		body.push_back(0x2F);
		body.push_back(0x00);

		out.insert(out.end(), { 'M', 'T', 'r', 'k' });
		put32be(out, static_cast<uint32_t>(body.size()));
		out.insert(out.end(), body.begin(), body.end());
	}
	return true;
}
} // namespace slade::wess

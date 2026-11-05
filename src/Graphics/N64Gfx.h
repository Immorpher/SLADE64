#pragma once

// -----------------------------------------------------------------------------
// Doom 64 (Nintendo 64) graphic formats
//
// Header-only detection + decoding of the graphic lumps found in Doom 64 WADs.
// All multi-byte values are big-endian. Palettes are N64 RGBA5551.
//
//  Texture   (T_START..T_END)   8 byte header {id, numpal, wshift, hshift}
//            4bpp, w*h/2 bytes (padded to 8), then numpal*16 palette entries.
//            Odd rows have their 4-byte halves swapped (per 8 bytes).
//  Sprite    16 byte header {tiles, compressed, cmpsize, xoffs, yoffs, width,
//            height, tileheight}
//            compressed >= 0: 4bpp, row stride = width padded to 16, followed
//                             by a 16 entry palette.
//            compressed == -1: 8bpp, row stride = width padded to 8, followed
//                             by a 256 entry palette (only for lumps outside
//                             of S_START..S_END) or, inside the sprite
//                             namespace, no palette at all (external PALxxxx#
//                             lump). Within every 'tileheight' rows, odd rows
//                             have their 4-byte halves swapped (per 8 pixels).
//  Gfx       8 byte header {-1, unknown, width, height}, 8bpp (stride=width,
//            total padded to 8) followed by a 256 entry palette. Without the
//            palette it's the 'FIRE' sky (greyscale).
//  Cloud     8 byte header, 64x64 8bpp, odd rows swapped, 256 entry palette
//            at offset 4104.
//  Palette   (PALxxxx#) 8 byte header + 256 palette entries (external sprite
//            palette)
// -----------------------------------------------------------------------------

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstddef>
#include <cstdlib>
#include <cstring>
#include <string>
#include <utility>
#include <vector>

namespace slade::n64gfx
{
enum class Kind
{
	Unknown,
	Texture,    // 4bpp, 1+ embedded 16 colour palettes
	Sprite4,    // 4bpp, embedded 16 colour palette
	Sprite8,    // 8bpp, embedded 256 colour palette
	Sprite8Ext, // 8bpp, palette is in an external PAL lump
	Gfx,        // 8bpp, embedded 256 colour palette
	GfxNoPal,   // 8bpp, no palette (fire sky), shown greyscale
	Cloud,      // 64x64 8bpp, embedded palette, odd rows swapped
	Palette     // External sprite palette lump
};

inline bool isTexture(Kind k)
{
	return k == Kind::Texture;
}
inline bool isSprite(Kind k)
{
	return k == Kind::Sprite4 || k == Kind::Sprite8 || k == Kind::Sprite8Ext;
}
inline bool isGfx(Kind k)
{
	return k == Kind::Gfx || k == Kind::GfxNoPal || k == Kind::Cloud;
}

struct Image
{
	int                  width = 0, height = 0;
	int                  offset_x = 0, offset_y = 0;
	std::vector<uint8_t> index;   // palette index per pixel
	std::vector<uint8_t> alpha;   // alpha per pixel
	bool                 has_palette = false;
	uint8_t              palette[256][4] = {}; // RGBA
};

namespace detail
{
	inline unsigned be16(const uint8_t* d, size_t o)
	{
		return (unsigned(d[o]) << 8) | d[o + 1];
	}
	inline size_t pad8(size_t x)
	{
		return (x + 7) & ~size_t(7);
	}
	inline size_t pad16(size_t x)
	{
		return (x + 15) & ~size_t(15);
	}
	// Allow up to 3 bytes of trailing padding (wadutil64 pads lumps to 4 bytes)
	inline bool sizeOk(size_t size, size_t expected)
	{
		return size >= expected && size < expected + 4;
	}
	inline void swapHalves(uint8_t* p, size_t n, size_t group)
	{
		for (size_t g = 0; g + group <= n; g += group)
			for (size_t i = 0; i < group / 2; i++)
				std::swap(p[g + i], p[g + group / 2 + i]);
	}
	// Reads [count] big-endian RGBA5551 colours into [out] (5->8 bit by bit replication)
	inline void readPalette(const uint8_t* d, int count, uint8_t (*out)[4])
	{
		for (int i = 0; i < count; i++)
		{
			unsigned v = be16(d, i * 2);
			unsigned r = (v >> 11) & 31, g = (v >> 6) & 31, b = (v >> 1) & 31;
			out[i][0] = uint8_t((r << 3) | (r >> 2));
			out[i][1] = uint8_t((g << 3) | (g >> 2));
			out[i][2] = uint8_t((b << 3) | (b >> 2));
			out[i][3] = (v & 1) ? 255 : 0;
		}
	}
} // namespace detail

// Determines which (if any) Doom 64 graphic format [d] is, by checking that the
// header is sane and that the lump size matches exactly what the header implies
inline Kind detect(const uint8_t* d, size_t size)
{
	using namespace detail;
	if (!d || size < 8)
		return Kind::Unknown;

	// External palette
	if (size == 520 && be16(d, 0) == 0 && be16(d, 2) == 256)
		return Kind::Palette;

	// Cloud (64x64, 8 byte header, 256 colour palette at 4104)
	if (size == 4616 && be16(d, 2) == 0xFFFF && be16(d, 0) != 0xFFFF)
		return Kind::Cloud;

	// Gfx (compressed = -1)
	if (be16(d, 0) == 0xFFFF)
	{
		size_t w = be16(d, 4), h = be16(d, 6);
		if (w == 0 || h == 0 || w > 2048 || h > 2048)
			return Kind::Unknown;
		size_t sz = pad8(w * h);
		if (sizeOk(size, 8 + sz + 512))
			return Kind::Gfx;
		if (sizeOk(size, 8 + sz))
			return Kind::GfxNoPal;
		return Kind::Unknown;
	}

	// Texture
	{
		unsigned numpal = be16(d, 2), ws = be16(d, 4), hs = be16(d, 6);
		if (numpal >= 1 && numpal <= 64 && ws >= 2 && ws <= 10 && hs >= 2 && hs <= 10)
		{
			size_t sz = pad8(((size_t(1) << ws) * (size_t(1) << hs)) >> 1);
			if (sizeOk(size, 8 + sz + numpal * 32))
				return Kind::Texture;
		}
	}

	// Sprite
	if (size >= 16)
	{
		int    comp = int16_t(be16(d, 2));
		size_t w = be16(d, 10), h = be16(d, 12);
		if (w > 0 && h > 0 && w <= 2048 && h <= 2048)
		{
			if (comp == -1)
			{
				size_t sz = pad8(w) * h;
				if (sizeOk(size, 16 + sz + 512))
					return Kind::Sprite8;
				if (sizeOk(size, 16 + sz))
					return Kind::Sprite8Ext;
			}
			else if (comp >= 0)
			{
				size_t sz = pad16(w) * h / 2;
				if (be16(d, 4) == sz && sizeOk(size, 16 + sz + 32))
					return Kind::Sprite4;
			}
		}
	}

	return Kind::Unknown;
}

// Returns the image dimensions for a lump of the given [kind] (width/height = 0 on failure)
inline void dimensions(const uint8_t* d, Kind kind, int& w, int& h)
{
	using namespace detail;
	w = h = 0;
	switch (kind)
	{
	case Kind::Texture:
		w = 1 << be16(d, 4);
		h = 1 << be16(d, 6);
		break;
	case Kind::Sprite4:
	case Kind::Sprite8:
	case Kind::Sprite8Ext:
		w = be16(d, 10);
		h = be16(d, 12);
		break;
	case Kind::Gfx:
	case Kind::GfxNoPal:
		w = be16(d, 4);
		h = be16(d, 6);
		break;
	case Kind::Cloud:
		w = h = 64;
		break;
	case Kind::Palette:
		w = h = 16;
		break;
	default: break;
	}
}

// Decodes a lump of the given [kind] (as returned by detect). [pal_index] selects
// which palette of a multi-palette texture to show. Returns false if invalid.
inline bool decode(const uint8_t* d, size_t size, Kind kind, Image& out, int pal_index = 0)
{
	using namespace detail;
	if (kind == Kind::Unknown || detect(d, size) != kind)
		return false;

	dimensions(d, kind, out.width, out.height);
	const size_t w = out.width, h = out.height;
	out.index.assign(w * h, 0);
	out.alpha.assign(w * h, 255);
	out.has_palette = true;
	out.offset_x = out.offset_y = 0;

	switch (kind)
	{
	case Kind::Texture:
	{
		size_t   numpal = be16(d, 2);
		size_t   rb     = w / 2;
		size_t   pal_o  = 8 + pad8(rb * h);
		unsigned pi     = (pal_index >= 0 && size_t(pal_index) < numpal) ? pal_index : 0;
		readPalette(d + pal_o + pi * 32, 16, out.palette);

		std::vector<uint8_t> row(rb);
		for (size_t y = 0; y < h; y++)
		{
			for (size_t i = 0; i < rb; i++)
				row[i] = d[8 + y * rb + i];
			if ((y & 1) && w >= 16)
				swapHalves(row.data(), rb, 8);
			for (size_t x = 0; x < w; x++)
				out.index[y * w + x] = (x & 1) ? (row[x / 2] & 15) : (row[x / 2] >> 4);
		}
		break;
	}

	case Kind::Sprite4:
	case Kind::Sprite8:
	case Kind::Sprite8Ext:
	{
		const bool   is4  = (kind == Kind::Sprite4);
		const size_t pw   = is4 ? pad16(w) : pad8(w);
		const size_t th   = be16(d, 14);
		const size_t grp  = is4 ? 16 : 8;
		out.offset_x      = int16_t(be16(d, 6));
		out.offset_y      = int16_t(be16(d, 8));
		const size_t data = is4 ? pw * h / 2 : pw * h;

		if (is4)
			readPalette(d + 16 + data, 16, out.palette);
		else if (kind == Kind::Sprite8)
			readPalette(d + 16 + data, 256, out.palette);
		else
			out.has_palette = false; // external palette

		std::vector<uint8_t> row(pw);
		size_t               id  = 0;
		bool                 inv = false;
		for (size_t y = 0; y < h; y++, id++)
		{
			// Row flipping restarts at the start of each tile
			if (id == th)
			{
				id  = 0;
				inv = false;
			}
			if (is4)
			{
				for (size_t i = 0; i < pw / 2; i++)
				{
					uint8_t v  = d[16 + y * (pw / 2) + i];
					row[i * 2]     = v >> 4;
					row[i * 2 + 1] = v & 15;
				}
			}
			else
				for (size_t i = 0; i < pw; i++)
					row[i] = d[16 + y * pw + i];
			if (inv)
				swapHalves(row.data(), pw, grp);
			inv = !inv;
			for (size_t x = 0; x < w; x++)
				out.index[y * w + x] = row[x];
		}

		// External palette: index 0 is the transparent colour
		if (kind == Kind::Sprite8Ext)
			for (size_t i = 0; i < w * h; i++)
				out.alpha[i] = out.index[i] ? 255 : 0;
		break;
	}

	case Kind::Gfx:
		for (size_t i = 0; i < w * h; i++)
			out.index[i] = d[8 + i];
		readPalette(d + 8 + pad8(w * h), 256, out.palette);
		break;

	case Kind::GfxNoPal:
		for (size_t i = 0; i < w * h; i++)
			out.index[i] = d[8 + i];
		for (int i = 0; i < 256; i++)
		{
			out.palette[i][0] = out.palette[i][1] = out.palette[i][2] = uint8_t(i & 0xF0);
			out.palette[i][3]                                          = 255;
		}
		break;

	case Kind::Cloud:
	{
		for (size_t y = 0; y < 64; y++)
		{
			uint8_t* row = &out.index[y * 64];
			for (size_t x = 0; x < 64; x++)
				row[x] = d[8 + y * 64 + x];
			if (y & 1)
				swapHalves(row, 64, 8);
		}
		readPalette(d + 4104, 256, out.palette);
		break;
	}

	case Kind::Palette:
		// Show the 256 colours as a 16x16 grid of swatches
		readPalette(d + 8, 256, out.palette);
		for (int i = 0; i < 256; i++)
			out.index[i] = uint8_t(i);
		break;

	default: return false;
	}

	// Per-pixel alpha comes from the palette's alpha bit (except where already
	// decided above, or for the procedural sky lumps which are always opaque)
	if (out.has_palette && kind != Kind::GfxNoPal && kind != Kind::Cloud && kind != Kind::Palette)
		for (size_t i = 0; i < w * h; i++)
			out.alpha[i] = out.palette[out.index[i]][3];
	if (out.has_palette)
		for (auto& c : out.palette)
			c[3] = 255; // alpha is carried by the mask instead

	return true;
}

// Returns the number of palettes in a texture lump (1 for everything else)
inline int numPalettes(const uint8_t* d, Kind kind)
{
	return kind == Kind::Texture ? int(detail::be16(d, 2)) : 1;
}

// Finds the name of the default external palette (PALxxxx0) for the sprite
// named [sprite_name] given a list of palette names. Returns -1 if none match.
inline int findSpritePalette(const std::string& sprite_name, const std::vector<std::string>& pal_names)
{
	if (sprite_name.size() < 4)
		return -1;
	auto up = [](std::string s)
	{
		for (auto& c : s)
			if (c >= 'a' && c <= 'z')
				c -= 32;
		return s;
	};
	const std::string base = "PAL" + up(sprite_name).substr(0, 4);
	int               first = -1;
	for (size_t i = 0; i < pal_names.size(); i++)
	{
		auto n = up(pal_names[i]);
		if (n.compare(0, base.size(), base) != 0)
			continue;
		if (n.size() > base.size() && n[base.size()] == '0')
			return int(i); // exact "PALxxxx0"
		if (first < 0)
			first = int(i);
	}
	return first;
}

// =============================================================================
// Encoding (writing) - used by the 'Graphic Format Conversion' formats
// =============================================================================

// A palettised image ready to be encoded. Palette entries are raw N64 RGBA5551
// values (bit 0 = opaque), a multiple of 16 of them (one 16 colour palette per row)
struct Indexed
{
	int                   width = 0, height = 0;
	int                   offset_x = 0, offset_y = 0;
	std::vector<uint8_t>  index;   // palette index per pixel
	std::vector<uint16_t> palette; // raw RGBA5551 entries
	unsigned              pal_ref = 1; // External palette sprites: 'cmpsize' header field (see below)
};

namespace detail
{
	inline void put16(std::vector<uint8_t>& o, unsigned v)
	{
		o.push_back(uint8_t((v >> 8) & 0xFF));
		o.push_back(uint8_t(v & 0xFF));
	}
	inline unsigned to5(unsigned c)
	{
		return (c * 31 + 127) / 255;
	}
	inline unsigned expand5(unsigned v)
	{
		return (v << 3) | (v >> 2);
	}
	inline bool shiftOf(int n, unsigned& shift)
	{
		for (shift = 2; shift <= 10; shift++)
			if ((1 << shift) == n)
				return true;
		return false;
	}
	inline void padTo8(std::vector<uint8_t>& o, size_t start)
	{
		while ((o.size() - start) % 8)
			o.push_back(0);
	}
	inline bool indicesBelow(const Indexed& in, unsigned limit)
	{
		if (in.index.size() != size_t(in.width) * size_t(in.height))
			return false;
		for (auto v : in.index)
			if (v >= limit)
				return false;
		return true;
	}
} // namespace detail

// Packs an 8-bit RGB colour into a raw N64 RGBA5551 palette entry
inline uint16_t packColour(const uint8_t* rgb, bool opaque)
{
	using namespace detail;
	return uint16_t((to5(rgb[0]) << 11) | (to5(rgb[1]) << 6) | (to5(rgb[2]) << 1) | (opaque ? 1 : 0));
}

// Encodes a texture (4bpp, power of 2 size, 1-16 palettes of 16 colours)
inline bool encodeTexture(const Indexed& in, std::vector<uint8_t>& out)
{
	using namespace detail;
	unsigned ws, hs;
	if (!shiftOf(in.width, ws) || !shiftOf(in.height, hs))
		return false;
	const size_t numpal = in.palette.size() / 16;
	if (numpal < 1 || numpal > 16 || in.palette.size() % 16 || !indicesBelow(in, 16))
		return false;

	const size_t w = in.width, h = in.height, rb = w / 2;
	out.clear();
	put16(out, 1);
	put16(out, unsigned(numpal));
	put16(out, ws);
	put16(out, hs);

	std::vector<uint8_t> row(rb);
	for (size_t y = 0; y < h; y++)
	{
		for (size_t i = 0; i < rb; i++)
			row[i] = uint8_t((in.index[y * w + i * 2] << 4) | in.index[y * w + i * 2 + 1]);
		if ((y & 1) && w >= 16)
			swapHalves(row.data(), rb, 8);
		out.insert(out.end(), row.begin(), row.end());
	}
	padTo8(out, 8);
	for (auto v : in.palette)
		put16(out, v);
	return true;
}

// Encodes a sprite. [four_bit]: 4bpp with a 16 colour palette, otherwise 8bpp
// with no palette (palette is in an external PAL lump)
inline bool encodeSprite(const Indexed& in, bool four_bit, std::vector<uint8_t>& out)
{
	using namespace detail;
	const size_t w = in.width, h = in.height;
	if (w < 1 || h < 1 || w > 4095 || h > 4095)
		return false;
	if (four_bit && (in.palette.size() != 16 || !indicesBelow(in, 16)))
		return false;
	if (!four_bit && !indicesBelow(in, 256))
		return false;

	const size_t pw     = four_bit ? pad16(w) : pad8(w);
	const size_t stride = four_bit ? pw / 2 : pw;
	if (stride > 2048)
		return false;
	const size_t th        = std::min(h, 2048 / stride); // Rows per N64 tile
	const size_t tiles     = (h + th - 1) / th;
	const size_t data_size = stride * h;
	if (four_bit && data_size > 0xFFFF)
		return false;

	out.clear();
	put16(out, unsigned(tiles));
	put16(out, four_bit ? 1 : 0xFFFF);
	// For 4bpp this is the size of the pixel data. For 8bpp sprites with an external
	// palette it's a reference to the palette lump: (2 * distance in lumps from the
	// palette lump to this one) + 1, which depends on the sprite's position in the WAD
	put16(out, four_bit ? unsigned(data_size) : (in.pal_ref & 0xFFFF));
	put16(out, unsigned(int16_t(in.offset_x)) & 0xFFFF);
	put16(out, unsigned(int16_t(in.offset_y)) & 0xFFFF);
	put16(out, unsigned(w));
	put16(out, unsigned(h));
	put16(out, unsigned(th));

	std::vector<uint8_t> row(pw);
	size_t               id  = 0;
	bool                 inv = false;
	for (size_t y = 0; y < h; y++, id++)
	{
		if (id == th)
		{
			id  = 0;
			inv = false;
		}
		std::fill(row.begin(), row.end(), 0);
		std::copy(in.index.begin() + y * w, in.index.begin() + (y + 1) * w, row.begin());
		if (inv)
			swapHalves(row.data(), pw, four_bit ? 16 : 8);
		inv = !inv;
		if (four_bit)
			for (size_t i = 0; i < pw; i += 2)
				out.push_back(uint8_t((row[i] << 4) | row[i + 1]));
		else
			out.insert(out.end(), row.begin(), row.end());
	}
	if (four_bit)
		for (auto v : in.palette)
			put16(out, v);
	return true;
}

// Encodes an 8bpp graphic with an embedded 256 colour palette
inline bool encodeGfx(const Indexed& in, std::vector<uint8_t>& out)
{
	using namespace detail;
	const size_t w = in.width, h = in.height;
	if (w < 1 || h < 1 || w > 2048 || h > 2048 || in.palette.size() != 256 || !indicesBelow(in, 256))
		return false;

	out.clear();
	put16(out, 0xFFFF);
	put16(out, 0);
	put16(out, unsigned(w));
	put16(out, unsigned(h));
	out.insert(out.end(), in.index.begin(), in.index.end());
	padTo8(out, 8);
	for (auto v : in.palette)
		put16(out, v);
	return true;
}

// -----------------------------------------------------------------------------
// Palette building
//
// Turns an image into palette indices + a raw N64 palette. If the image has any
// transparent pixels, palette index 0 is the transparent colour (raw value 0).
// -----------------------------------------------------------------------------

// Number of 16 colour rows (1-16) of a 256 colour palette (RGBA, 256*4 bytes)
// that are in use: trailing rows that are entirely black are dropped, since an
// indexed PNG's palette size isn't preserved when SLADE loads it
inline int animationRows(const uint8_t (*pal)[4])
{
	int rows = 1;
	for (int r = 0; r < 16; r++)
		for (int i = 0; i < 16; i++)
		{
			const uint8_t* c = pal[r * 16 + i];
			if (c[0] || c[1] || c[2])
				rows = r + 1;
		}
	return rows;
}

namespace detail
{
	inline int colourDistance(const int* a, const int* b)
	{
		const int dr = a[0] - b[0], dg = a[1] - b[1], db = a[2] - b[2];
		return 2 * dr * dr + 4 * dg * dg + 3 * db * db;
	}

	struct HistColour
	{
		int      c[3]; // 8 bit (expanded from 5 bit)
		uint32_t count;
	};

	// Picks [n] representative colours from a histogram: weighted median cut,
	// refined with a few k-means passes
	inline std::vector<std::array<int, 3>> pickColours(std::vector<HistColour>& hist, size_t n)
	{
		struct Box
		{
			size_t begin, end;
		};
		std::vector<Box> boxes{ { 0, hist.size() } };

		auto stats = [&](const Box& b, double mean[3], double& sse, int& axis)
		{
			double   total = 0;
			int      lo[3] = { 255, 255, 255 }, hi[3] = { 0, 0, 0 };
			double   sum[3] = { 0, 0, 0 };
			for (size_t i = b.begin; i < b.end; i++)
				for (int k = 0; k < 3; k++)
				{
					sum[k] += double(hist[i].c[k]) * hist[i].count;
					lo[k] = std::min(lo[k], hist[i].c[k]);
					hi[k] = std::max(hi[k], hist[i].c[k]);
					if (k == 0)
						total += hist[i].count;
				}
			sse = 0;
			for (int k = 0; k < 3; k++)
				mean[k] = sum[k] / total;
			for (size_t i = b.begin; i < b.end; i++)
				for (int k = 0; k < 3; k++)
				{
					const double d = hist[i].c[k] - mean[k];
					sse += d * d * hist[i].count * (k == 0 ? 2 : k == 1 ? 4 : 3);
				}
			axis = 0;
			double best = -1;
			for (int k = 0; k < 3; k++)
			{
				const double range = double(hi[k] - lo[k]) * (k == 0 ? 2 : k == 1 ? 4 : 3);
				if (range > best)
				{
					best = range;
					axis = k;
				}
			}
		};

		while (boxes.size() < n)
		{
			// Find the box with the most error that can still be split
			int    pick     = -1;
			double pick_sse = -1;
			int    pick_axis = 0;
			for (size_t b = 0; b < boxes.size(); b++)
			{
				if (boxes[b].end - boxes[b].begin < 2)
					continue;
				double mean[3], sse;
				int    axis;
				stats(boxes[b], mean, sse, axis);
				if (sse > pick_sse)
				{
					pick_sse  = sse;
					pick      = int(b);
					pick_axis = axis;
				}
			}
			if (pick < 0)
				break;

			// Split at the weighted median along the widest axis
			Box b = boxes[pick];
			std::sort(
				hist.begin() + b.begin,
				hist.begin() + b.end,
				[pick_axis](const HistColour& x, const HistColour& y) { return x.c[pick_axis] < y.c[pick_axis]; });
			uint64_t total = 0;
			for (size_t i = b.begin; i < b.end; i++)
				total += hist[i].count;
			uint64_t acc = 0;
			size_t   mid = b.begin + 1;
			for (size_t i = b.begin; i < b.end - 1; i++)
			{
				acc += hist[i].count;
				mid = i + 1;
				if (acc * 2 >= total)
					break;
			}
			boxes[pick] = { b.begin, mid };
			boxes.push_back({ mid, b.end });
		}

		// Box averages
		std::vector<std::array<int, 3>> centres;
		for (auto& b : boxes)
		{
			double mean[3], sse;
			int    axis;
			stats(b, mean, sse, axis);
			centres.push_back({ int(mean[0] + 0.5), int(mean[1] + 0.5), int(mean[2] + 0.5) });
		}

		// k-means refinement
		for (int pass = 0; pass < 4; pass++)
		{
			std::vector<double>   sum(centres.size() * 3, 0.0);
			std::vector<uint64_t> cnt(centres.size(), 0);
			for (auto& h : hist)
			{
				size_t best = 0;
				int    bd   = 1 << 30;
				for (size_t k = 0; k < centres.size(); k++)
				{
					const int d = colourDistance(h.c, centres[k].data());
					if (d < bd)
					{
						bd   = d;
						best = k;
					}
				}
				for (int k = 0; k < 3; k++)
					sum[best * 3 + k] += double(h.c[k]) * h.count;
				cnt[best] += h.count;
			}
			for (size_t k = 0; k < centres.size(); k++)
				if (cnt[k])
					for (int j = 0; j < 3; j++)
						centres[k][j] = int(sum[k * 3 + j] / cnt[k] + 0.5);
		}
		return centres;
	}
} // namespace detail

// Builds a palettised image from [rgba] (w*h*4) with at most [max_colours]
// (16 or 256) palette entries.
// [src_index]/[src_pal]: the original indexed data (and its 256*4 RGBA palette),
// if the source image was paletted - its palette is kept as is when possible.
// Pixels with alpha < 128 are transparent.
inline bool buildPalettized(
	int            w,
	int            h,
	const uint8_t* rgba,
	const uint8_t* src_index,
	const uint8_t (*src_pal)[4],
	int      max_colours,
	Indexed& out,
	std::string& err)
{
	using namespace detail;
	const size_t npix = size_t(w) * h;
	out.width  = w;
	out.height = h;
	out.index.assign(npix, 0);
	out.palette.assign(max_colours, 0);

	size_t trans_count = 0;
	for (size_t i = 0; i < npix; i++)
		if (rgba[i * 4 + 3] < 128)
			trans_count++;
	const bool any_trans = trans_count > 0;
	if (trans_count == npix)
	{
		// Fully transparent image: nothing to put in the palette
		return true;
	}

	// --- Paletted source ---
	if (src_index && src_pal)
	{
		bool used[256] = {};
		for (size_t i = 0; i < npix; i++)
			if (rgba[i * 4 + 3] >= 128)
				used[src_index[i]] = true;
		int max_used = 0, num_used = 0;
		for (int i = 0; i < 256; i++)
			if (used[i])
			{
				max_used = i;
				num_used++;
			}

		// Keep palette exactly as is
		if (max_used < max_colours && !(any_trans && used[0]))
		{
			for (int i = 0; i < max_colours; i++)
				out.palette[i] = packColour(src_pal[i], true);
			if (any_trans)
				out.palette[0] = 0;
			for (size_t i = 0; i < npix; i++)
				out.index[i] = (rgba[i * 4 + 3] < 128) ? 0 : src_index[i];
			return true;
		}

		// Same colours, but compacted (and index 0 freed up for transparency)
		if (num_used + (any_trans ? 1 : 0) <= max_colours)
		{
			int remap[256] = {};
			int next       = any_trans ? 1 : 0;
			for (int i = 0; i < 256; i++)
				if (used[i])
				{
					remap[i]          = next;
					out.palette[next] = packColour(src_pal[i], true);
					next++;
				}
			if (any_trans)
				out.palette[0] = 0;
			for (size_t i = 0; i < npix; i++)
				out.index[i] = (rgba[i * 4 + 3] < 128) ? 0 : uint8_t(remap[src_index[i]]);
			return true;
		}
		// Too many colours: quantize below
	}

	// --- Full colour (or too many colours): quantize ---
	const int budget = max_colours - (any_trans ? 1 : 0);
	if (budget < 1)
	{
		err = "Not enough palette entries";
		return false;
	}

	std::vector<uint32_t> counts(32768, 0);
	std::vector<uint16_t> first_seen; // Unique 5 bit colours in raster order
	std::vector<uint16_t> key(npix, 0);
	for (size_t i = 0; i < npix; i++)
	{
		if (rgba[i * 4 + 3] < 128)
			continue;
		const uint16_t k = uint16_t((to5(rgba[i * 4]) << 10) | (to5(rgba[i * 4 + 1]) << 5) | to5(rgba[i * 4 + 2]));
		key[i]           = k;
		if (counts[k]++ == 0)
			first_seen.push_back(k);
	}

	auto keyColour = [](unsigned k, int* c)
	{
		c[0] = int(expand5((k >> 10) & 31));
		c[1] = int(expand5((k >> 5) & 31));
		c[2] = int(expand5(k & 31));
	};

	std::vector<std::array<int, 3>> palette;
	if (int(first_seen.size()) <= budget)
	{
		// Few enough colours to keep them all
		for (auto k : first_seen)
		{
			std::array<int, 3> c;
			keyColour(k, c.data());
			palette.push_back(c);
		}
	}
	else
	{
		std::vector<HistColour> hist;
		for (auto k : first_seen)
		{
			HistColour hc;
			keyColour(k, hc.c);
			hc.count = counts[k];
			hist.push_back(hc);
		}
		palette = pickColours(hist, size_t(budget));
		for (auto& c : palette) // Snap to what the N64 can display
			for (int j = 0; j < 3; j++)
				c[j] = int(expand5(to5(unsigned(std::clamp(c[j], 0, 255)))));
	}

	const int first = any_trans ? 1 : 0;
	for (size_t i = 0; i < palette.size(); i++)
	{
		const uint8_t rgb[3] = { uint8_t(palette[i][0]), uint8_t(palette[i][1]), uint8_t(palette[i][2]) };
		out.palette[first + i] = packColour(rgb, true);
	}
	if (any_trans)
		out.palette[0] = 0;

	// Map pixels to the nearest palette entry (cached per 5 bit colour)
	std::vector<int16_t> lut(32768, -1);
	for (size_t i = 0; i < npix; i++)
	{
		if (rgba[i * 4 + 3] < 128)
		{
			out.index[i] = 0;
			continue;
		}
		int16_t& m = lut[key[i]];
		if (m < 0)
		{
			int c[3];
			keyColour(key[i], c);
			int bd = 1 << 30;
			for (size_t k = 0; k < palette.size(); k++)
			{
				const int d = colourDistance(c, palette[k].data());
				if (d < bd)
				{
					bd = d;
					m  = int16_t(k);
				}
			}
		}
		out.index[i] = uint8_t(first + m);
	}
	return true;
}

// Maps an image onto an existing 256 colour palette [target] (RGBA, 256*4 bytes;
// index 0 is the transparent colour). Used for sprites with an external palette.
// If the source is paletted and its palette matches [target] it is kept as is.
inline bool mapToPalette(
	int            w,
	int            h,
	const uint8_t* rgba,
	const uint8_t* src_index,
	const uint8_t (*src_pal)[4],
	const uint8_t (*target)[4],
	Indexed& out)
{
	using namespace detail;
	const size_t npix = size_t(w) * h;
	out.width         = w;
	out.height        = h;
	out.index.assign(npix, 0);
	out.palette.clear();

	// Is the source's palette the same as the target? (within 5 bit precision)
	bool same = (src_index && src_pal);
	if (same)
	{
		bool used[256] = {};
		for (size_t i = 0; i < npix; i++)
			if (rgba[i * 4 + 3] >= 128)
				used[src_index[i]] = true;
		for (int i = 0; i < 256 && same; i++)
			if (used[i] && i > 0)
				for (int k = 0; k < 3; k++)
					if (std::abs(int(src_pal[i][k]) - int(target[i][k])) > 8)
						same = false;
	}

	std::vector<int16_t> lut(32768, -1);
	for (size_t i = 0; i < npix; i++)
	{
		if (rgba[i * 4 + 3] < 128)
		{
			out.index[i] = 0;
			continue;
		}
		if (same && src_index[i] != 0)
		{
			out.index[i] = src_index[i];
			continue;
		}

		const uint8_t* p = &rgba[i * 4];
		const uint16_t k = uint16_t((to5(p[0]) << 10) | (to5(p[1]) << 5) | to5(p[2]));
		int16_t&       m = lut[k];
		if (m < 0)
		{
			const int c[3] = { int(expand5((k >> 10) & 31)), int(expand5((k >> 5) & 31)), int(expand5(k & 31)) };
			int       bd   = 1 << 30;
			for (int e = 1; e < 256; e++) // Index 0 is transparent
			{
				const int t[3] = { target[e][0], target[e][1], target[e][2] };
				const int d    = colourDistance(c, t);
				if (d < bd)
				{
					bd = d;
					m  = int16_t(e);
				}
			}
		}
		out.index[i] = uint8_t(m);
	}
	return true;
}

} // namespace slade::n64gfx

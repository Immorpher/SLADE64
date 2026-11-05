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

#include <cstdint>
#include <cstddef>
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
} // namespace slade::n64gfx

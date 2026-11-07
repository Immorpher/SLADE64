#pragma once

#include "Graphics/N64Gfx.h"

// -----------------------------------------------------------------------------
// SIFDoom64 Class
//
// Reads Doom 64 (N64) textures, sprites, gfx and palette lumps (read only).
// One class instance is registered per category (see SIFormat::initFormats).
// For textures, the 'image index' selects which of the lump's palettes is used.
// -----------------------------------------------------------------------------
class SIFDoom64 : public SIFormat
{
public:
	enum class Category
	{
		Texture,
		Sprite,
		Gfx,
		Palette
	};

	SIFDoom64(string_view id, string_view name, Category category) : SIFormat(id, name, "lmp", 90), category_{ category }
	{
	}

	bool isThisFormat(MemChunk& mc) override { return matches(n64gfx::detect(mc.data(), mc.size())); }

	SImage::Info info(MemChunk& mc, int index) override
	{
		SImage::Info info;
		auto         kind = n64gfx::detect(mc.data(), mc.size());
		if (!matches(kind))
			return info;

		n64gfx::dimensions(mc.data(), kind, info.width, info.height);
		info.colformat  = SImage::Type::PalMask;
		info.format     = id_;
		info.numimages  = n64gfx::numPalettes(mc.data(), kind);
		info.imgindex   = index;
		info.has_palette = (kind != n64gfx::Kind::Sprite8Ext);
		if (n64gfx::isSprite(kind))
		{
			info.offset_x = int16_t(mc.readB16(6));
			info.offset_y = int16_t(mc.readB16(8));
		}
		return info;
	}

	// Writing (eg. after flipping/rotating, or when the entry panel saves an image it just converted).
	// Not offered as a conversion target (see SIFDoom64Writer for that)
	Writable canWrite(SImage& image) override;
	bool     canWriteType(SImage::Type type) override { return false; }

protected:
	bool writeImage(SImage& image, MemChunk& out, Palette* pal, int index) override;

	bool readImage(SImage& image, MemChunk& data, int index) override
	{
		auto kind = n64gfx::detect(data.data(), data.size());
		if (!matches(kind))
			return false;

		// Texture palette selection
		const int numpal = n64gfx::numPalettes(data.data(), kind);
		if (index < 0 || index >= numpal)
			index = 0;

		n64gfx::Image img;
		if (!n64gfx::decode(data.data(), data.size(), kind, img, index))
		{
			global::error = "Invalid Doom 64 graphic";
			return false;
		}

		// Build palette (external-palette sprites don't have one, the palette
		// is selected via the palette chooser instead)
		Palette pal;
		if (img.has_palette)
			for (int i = 0; i < 256; i++)
				pal.setColour(i, ColRGBA(img.palette[i][0], img.palette[i][1], img.palette[i][2], 255));

		image.create(img.width, img.height, SImage::Type::PalMask, img.has_palette ? &pal : nullptr, index, numpal);
		image.setXOffset(img.offset_x);
		image.setYOffset(img.offset_y);

		memcpy(imageData(image), img.index.data(), img.index.size());
		memcpy(imageMask(image), img.alpha.data(), img.alpha.size());

		return true;
	}

private:
	Category category_;

	// The writer format that stores [image] back in the format it was read from
	SIFormat* writerFor(SImage& image) const;

	bool matches(n64gfx::Kind kind) const
	{
		switch (category_)
		{
		case Category::Texture: return n64gfx::isTexture(kind);
		case Category::Sprite: return n64gfx::isSprite(kind);
		case Category::Gfx: return n64gfx::isGfx(kind);
		case Category::Palette: return kind == n64gfx::Kind::Palette;
		}
		return false;
	}
};

// -----------------------------------------------------------------------------
// SIFDoom64Writer Class
//
// The Doom 64 formats offered in the Graphic Format Conversion dialog. They
// aren't used for detecting/reading anything (see SIFDoom64 for that), they just
// convert an image to what the N64 needs and write it:
//
//  Texture16   16 colour texture (T_START..T_END), 1 palette. The image is
//              reduced to 16 colours if it has more
//  Sprite16    16 colour sprite with an embedded palette
//  Graphic256  256 colour graphic with an embedded palette (title screens etc.)
//  TextureAnim 16 colour texture with extra palette rows for palette animation.
//              The source must be an 8-bit indexed image using indices 0-15;
//              each following group of 16 palette entries is the next row
//  Sprite256   256 colour sprite *without* a palette (enemies). Pixels are
//              matched to the target palette (a PALxxxx lump); an indexed image
//              that already uses that palette keeps its indices
//
// If the image has transparency, palette index 0 is the transparent colour.
// -----------------------------------------------------------------------------
class SIFDoom64Writer : public SIFormat
{
public:
	enum class Variant
	{
		Texture16,
		Sprite16,
		Graphic256,
		TextureAnim,
		Sprite256
	};

	SIFDoom64Writer(string_view id, string_view name, Variant variant) : SIFormat(id, name, "lmp", 0), variant_{ variant }
	{
	}

	// Never used to detect or read anything
	bool            isThisFormat(MemChunk& mc) override { return false; }
	SImage::Info    info(MemChunk& mc, int index) override { return {}; }
	bool            typeInName() const override { return false; }

	Writable canWrite(SImage& image) override
	{
		return unwritableReason(image).empty() ? Writable::Convert : Writable::No;
	}

	bool listWhenUnwritable() const override { return true; }

	// Explains why [image] doesn't fit this format (empty if it does), recommending a size where possible
	string unwritableReason(SImage& image) override
	{
		if (!image.isValid())
			return "There is no valid image to convert.";

		const int w = image.width();
		const int h = image.height();

		switch (variant_)
		{
		case Variant::Texture16:
		case Variant::TextureAnim:
		{
			// Textures store their size as powers of 2
			unsigned shift;
			const bool wok = n64gfx::detail::shiftOf(w, shift);
			const bool hok = n64gfx::detail::shiftOf(h, shift);
			if (!wok || !hok)
			{
				return fmt::format(
					"Doom 64 textures need a width and height that are powers of 2 (4, 8, 16, ... 1024). "
					"This image is {}x{}. Resize it to {}x{} (or another power of 2) and try again.",
					w,
					h,
					nearestPow2(w),
					nearestPow2(h));
			}

			// Palette animation needs an indexed source (that's where the palette rows are)
			if (variant_ == Variant::TextureAnim && !usesOnlyFirstRow(image))
				return "Texture animation needs a paletted image whose visible pixels only use palette "
					   "indices 0-15 (the first palette row).";
			break;
		}
		case Variant::Sprite16:
		{
			// Sprites can be any size (rows are padded internally), within limits
			const int maxw = std::min(4095, 0xFFFF * 2 / std::max(h, 1) / 16 * 16);
			if (w > 4095 || h > 4095 || n64gfx::detail::pad16(w) / 2 * h > 0xFFFF)
				return fmt::format(
					"This image is {}x{}, which is too large for a 16 colour sprite (maximum 4095x4095, and "
					"the padded pixel data must fit in 64 KB). Resize it to {}x{} or smaller.",
					w,
					h,
					std::min(w, std::max(maxw, 16)),
					std::min(h, 4095));
			break;
		}
		case Variant::Sprite256:
			if (w > 2048 || h > 4095)
				return fmt::format(
					"This image is {}x{}, which is too large for a 256 colour sprite (maximum 2048x4095). "
					"Resize it to {}x{} or smaller.",
					w,
					h,
					std::min(w, 2048),
					std::min(h, 4095));
			break;
		case Variant::Graphic256:
			if (w > 2048 || h > 2048)
				return fmt::format(
					"This image is {}x{}, which is too large for a 256 colour graphic (maximum 2048x2048). "
					"Resize it to {}x{} or smaller.",
					w,
					h,
					std::min(w, 2048),
					std::min(h, 2048));
			break;
		}

		return {};
	}

	bool canWriteType(SImage::Type type) override { return type == SImage::Type::PalMask; }

	bool convertWritable(SImage& image, ConvertOptions opt) override
	{
		// Do mask conversion
		if (!opt.transparency)
			image.fillAlpha(255);
		else if (opt.mask_source == Mask::Colour)
			image.maskFromColour(opt.mask_colour, opt.pal_current);
		else if (opt.mask_source == Mask::Alpha)
			image.cutoffMask(opt.alpha_threshold);
		else if (opt.mask_source == Mask::Brightness)
			image.maskFromBrightness(opt.pal_current);

		// Build the N64 palette + indices
		n64gfx::Indexed ix;
		if (!prepare(image, opt.pal_current, opt.pal_target, ix))
			return false;

		// Update the image to show what will actually be written
		Palette pal;
		if (variant_ == Variant::Sprite256)
			pal.copyPalette(opt.pal_target);
		else
			for (size_t i = 0; i < ix.palette.size() && i < 256; i++)
			{
				const unsigned v = ix.palette[i];
				pal.setColour(
					i,
					ColRGBA(
						n64gfx::detail::expand5((v >> 11) & 31),
						n64gfx::detail::expand5((v >> 6) & 31),
						n64gfx::detail::expand5((v >> 1) & 31),
						255));
			}

		const auto offset = image.offset();
		image.create(ix.width, ix.height, SImage::Type::PalMask, &pal, 0, 1);
		image.setXOffset(offset.x);
		image.setYOffset(offset.y);

		memcpy(imageData(image), ix.index.data(), ix.index.size());
		auto mask = imageMask(image);
		for (size_t i = 0; i < ix.index.size(); i++)
		{
			if (variant_ == Variant::Sprite256)
				mask[i] = ix.index[i] ? 255 : 0;
			else
				mask[i] = (ix.palette[ix.index[i]] & 1) ? 255 : 0;
		}

		return true;
	}

protected:
	bool readImage(SImage& image, MemChunk& data, int index) override { return false; }

	bool writeImage(SImage& image, MemChunk& out, Palette* pal, int index) override
	{
		if (!image.isValid() || image.type() != SImage::Type::PalMask)
			return false;

		// The image is normally already converted at this point (see convertWritable),
		// in which case this just picks up the palette and indices as they are
		Palette* image_pal = image.hasPalette() ? image.palette() : pal;
		if (!image_pal)
			return false;
		n64gfx::Indexed ix;
		if (!prepare(image, image_pal, variant_ == Variant::Sprite256 ? image_pal : nullptr, ix))
			return false;

		std::vector<uint8_t> bytes;
		bool                 ok = false;
		switch (variant_)
		{
		case Variant::Texture16:
		case Variant::TextureAnim: ok = n64gfx::encodeTexture(ix, bytes); break;
		case Variant::Sprite16: ok = n64gfx::encodeSprite(ix, true, bytes); break;
		case Variant::Sprite256: ok = n64gfx::encodeSprite(ix, false, bytes); break;
		case Variant::Graphic256: ok = n64gfx::encodeGfx(ix, bytes); break;
		}
		if (!ok)
		{
			global::error = "Image can't be written in this Doom 64 format";
			return false;
		}

		out.importMem(bytes.data(), bytes.size());
		return true;
	}

private:
	Variant variant_;

	// Gets the image as RGBA + (if paletted) its indices and 256 colour palette
	struct Source
	{
		MemChunk       rgba;
		bool           indexed = false;
		vector<uint8_t> index;
		uint8_t        palette[256][4] = {};
	};
	bool getSource(SImage& image, Palette* pal_current, Source& src) const
	{
		image.putRGBAData(src.rgba, pal_current);
		if (image.type() != SImage::Type::PalMask)
			return true;

		auto* source_pal = image.hasPalette() ? image.palette() : pal_current;
		if (!source_pal)
			return true;

		const auto size = static_cast<size_t>(image.width()) * image.height();
		src.index.assign(imageData(image), imageData(image) + size);
		for (int i = 0; i < 256; i++)
		{
			const auto col      = source_pal->colour(i);
			src.palette[i][0]   = col.r;
			src.palette[i][1]   = col.g;
			src.palette[i][2]   = col.b;
			src.palette[i][3]   = 255;
		}
		src.indexed = true;
		return true;
	}

	// Nearest power of 2 in the texture range (4..1024)
	static int nearestPow2(int n)
	{
		int best = 4;
		for (int p = 4; p <= 1024; p *= 2)
			if (std::abs(p - n) < std::abs(best - n))
				best = p;
		return best;
	}

	// True if the indexed image only uses palette indices 0-15
	bool usesOnlyFirstRow(SImage& image)
	{
		if (image.type() != SImage::Type::PalMask || !image.hasPalette())
			return false;

		const auto size = static_cast<size_t>(image.width()) * image.height();
		auto       data = imageData(image);
		auto       mask = imageMask(image);
		for (size_t i = 0; i < size; i++)
			if (data[i] > 15 && mask[i] >= 128)
				return false;
		return true;
	}

	// Builds the palette + indices to be written for [image]
	bool prepare(SImage& image, Palette* pal_current, Palette* pal_target, n64gfx::Indexed& ix)
	{
		Source src;
		if (!getSource(image, pal_current, src))
			return false;

		const int          w    = image.width();
		const int          h    = image.height();
		const uint8_t*     rgba = src.rgba.data();
		const uint8_t*     idx  = src.indexed ? src.index.data() : nullptr;
		const uint8_t(*pal)[4]  = src.indexed ? src.palette : nullptr;
		std::string        err;

		switch (variant_)
		{
		case Variant::Texture16:
		case Variant::Sprite16:
			if (!n64gfx::buildPalettized(w, h, rgba, idx, pal, 16, ix, err))
				return false;
			break;

		case Variant::Graphic256:
			if (!n64gfx::buildPalettized(w, h, rgba, idx, pal, 256, ix, err))
				return false;
			break;

		case Variant::TextureAnim:
		{
			if (!idx)
				return false;

			// Keep the indices and as many 16 colour palette rows as the image has
			const size_t npix = static_cast<size_t>(w) * h;
			const int    rows = n64gfx::animationRows(pal);
			bool         trans = false, opaque_zero = false;
			for (size_t i = 0; i < npix; i++)
			{
				if (rgba[i * 4 + 3] < 128)
					trans = true;
				else if (idx[i] == 0)
					opaque_zero = true;
			}
			// Index 0 can only be transparent if no opaque pixel uses it
			if (opaque_zero)
				trans = false;

			ix.width  = w;
			ix.height = h;
			ix.index.assign(npix, 0);
			for (size_t i = 0; i < npix; i++)
			{
				const bool t = rgba[i * 4 + 3] < 128;
				ix.index[i]  = t ? 0 : idx[i];
				if (ix.index[i] > 15)
					return false;
			}
			ix.palette.assign(rows * 16, 0);
			for (int e = 0; e < rows * 16; e++)
				ix.palette[e] = n64gfx::packColour(pal[e], true);
			if (trans)
				for (int r = 0; r < rows; r++)
					ix.palette[r * 16] = 0;
			break;
		}

		case Variant::Sprite256:
		{
			if (!pal_target)
				return false;
			uint8_t target[256][4];
			for (int i = 0; i < 256; i++)
			{
				const auto col = pal_target->colour(i);
				target[i][0]   = col.r;
				target[i][1]   = col.g;
				target[i][2]   = col.b;
				target[i][3]   = 255;
			}
			n64gfx::mapToPalette(w, h, rgba, idx, pal, target, ix);
			break;
		}
		}

		const auto offset = image.offset();
		ix.offset_x       = offset.x;
		ix.offset_y       = offset.y;
		return true;
	}
};


// -----------------------------------------------------------------------------
// SIFDoom64 writing, handled by the matching writer format
// -----------------------------------------------------------------------------
inline SIFormat* SIFDoom64::writerFor(SImage& image) const
{
	if (!image.isValid() || image.type() != SImage::Type::PalMask)
		return nullptr;

	switch (category_)
	{
	case Category::Texture:
		// Textures with palette animation rows can't be written back from a single row
		return image.size() > 1 ? nullptr : getFormat("n64w_tex16");

	case Category::Gfx:
		// FIRE sky has no palette
		return image.hasPalette() ? getFormat("n64w_gfx256") : nullptr;

	case Category::Sprite:
	{
		// No embedded palette = enemy sprite using a PAL lump
		if (!image.hasPalette())
			return getFormat("n64w_spr256");

		// Embedded palette: 16 colour sprites only (256 colour weapon sprites can't be written)
		const auto size = static_cast<size_t>(image.width()) * image.height();
		auto       data = imageData(image);
		auto       mask = imageMask(image);
		for (size_t i = 0; i < size; i++)
			if (data[i] > 15 && mask[i] >= 128)
				return nullptr;
		return getFormat("n64w_spr16");
	}

	default: return nullptr;
	}
}

inline SIFormat::Writable SIFDoom64::canWrite(SImage& image)
{
	auto* writer = writerFor(image);
	if (!writer || writer == unknownFormat() || writer->canWrite(image) == Writable::No)
		return Writable::No;

	return Writable::Yes;
}

inline bool SIFDoom64::writeImage(SImage& image, MemChunk& out, Palette* pal, int index)
{
	// FIRE and CLOUD skies look like plain graphics once loaded but are stored differently,
	// so they can't be written. Check what's being replaced
	const auto old_kind = n64gfx::detect(out.data(), out.size());
	const bool special  = old_kind == n64gfx::Kind::Cloud || old_kind == n64gfx::Kind::GfxNoPal;
	auto*      writer   = special ? nullptr : writerFor(image);
	if (!writer || writer == unknownFormat())
	{
		global::error = "This Doom 64 graphic can't be written back in its original format";
		return false;
	}

	return writer->saveImage(image, out, pal, index);
}

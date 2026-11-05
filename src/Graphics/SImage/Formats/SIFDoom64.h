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

protected:
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

#pragma once

// -----------------------------------------------------------------------------
// Doom 64 sprite palette references (needs archive context, so separate from
// N64Gfx.h).
//
// Enemy sprites (8bpp, no embedded palette) store a reference to their palette
// lump in the 'cmpsize' header field: when odd, the palette lump is the lump
// (cmpsize >> 1) places *before* the sprite in the WAD (PALxxxx0, the engine adds
// the colour variant on top of that). So the value depends on where the sprite
// is in the archive.
// -----------------------------------------------------------------------------

#include "Archive/Archive.h"
#include "Archive/ArchiveDir.h"
#include "Archive/ArchiveEntry.h"
#include "Archive/EntryType/EntryType.h"
#include "N64Gfx.h"

namespace slade::n64gfx
{
// Returns the raw 'cmpsize' palette reference of an external palette sprite
// (0 if [data] isn't one, or doesn't reference a palette lump)
inline unsigned spritePaletteReference(const uint8_t* data, size_t size)
{
	if (detect(data, size) != Kind::Sprite8Ext)
		return 0;
	const unsigned ref = detail::be16(data, 4);
	return (ref & 1) ? ref : 0;
}

// Returns the entry referenced by [ref] for the sprite [entry] (nullptr if invalid)
inline ArchiveEntry* referencedEntry(ArchiveEntry* entry, unsigned ref)
{
	if (!entry || !entry->parentDir() || !(ref & 1))
		return nullptr;
	const int index = entry->index() - int(ref >> 1);
	if (index < 0 || index >= int(entry->parentDir()->numEntries()))
		return nullptr;
	return entry->parentDir()->entryAt(index);
}

// Finds the default palette entry (PALxxxx0) for the sprite named [entry] in the same directory
inline ArchiveEntry* findNamedPalette(ArchiveEntry* entry)
{
	if (!entry || !entry->parentDir())
		return nullptr;

	auto*  dir = entry->parentDir();
	string base;
	for (size_t i = 0; i < 4 && i < entry->upperName().size(); i++)
		base += entry->upperName()[i];
	if (base.size() < 4)
		return nullptr;
	base = "PAL" + base;

	ArchiveEntry* fallback = nullptr;
	for (unsigned i = 0; i < dir->numEntries(); i++)
	{
		auto* e = dir->entryAt(i);
		if (!e->type() || e->type()->formatId() != "img_n64_pal" || e->upperName().compare(0, base.size(), base) != 0)
			continue;
		if (e->upperName().size() > base.size() && e->upperName()[base.size()] == '0')
			return e;
		if (!fallback)
			fallback = e;
	}
	return fallback;
}

// Sets the palette reference of the external palette sprite in [data] (which is
// about to replace the data of [entry]). [old_ref] is the reference of the data
// being replaced, which is kept if there was one (replacing a sprite doesn't move
// it). Otherwise it's set to point to the sprite's PALxxxx0 lump.
// Returns false if no palette could be found (the sprite will have no valid palette)
inline bool fixSpritePaletteReference(MemChunk& data, ArchiveEntry* entry, unsigned old_ref)
{
	if (detect(data.data(), data.size()) != Kind::Sprite8Ext)
		return true; // Nothing to do

	unsigned ref = old_ref;
	if (!(ref & 1))
	{
		auto* pal = findNamedPalette(entry);
		if (!pal)
			return false;
		const int distance = entry->index() - pal->index();
		if (distance < 1 || distance > 0x7FFF)
			return false;
		ref = unsigned(distance) * 2 + 1;
	}

	data[4] = uint8_t((ref >> 8) & 0xFF);
	data[5] = uint8_t(ref & 0xFF);
	return true;
}
} // namespace slade::n64gfx

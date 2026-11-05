
// -----------------------------------------------------------------------------
// SLADE - It's a Doom Editor
// Copyright(C) 2008 - 2022 Simon Judd
//
// Email:       sirjuddington@gmail.com
// Web:         http://slade.mancubus.net
// Filename:    PaletteChooser.cpp
// Description: PaletteChooser class. A wxComboBox that contains a list of
//              palettes available (from the PaletteManager)
//
// This program is free software; you can redistribute it and/or modify it
// under the terms of the GNU General Public License as published by the Free
// Software Foundation; either version 2 of the License, or (at your option)
// any later version.
//
// This program is distributed in the hope that it will be useful, but WITHOUT
// ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
// FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License for
// more details.
//
// You should have received a copy of the GNU General Public License along with
// this program; if not, write to the Free Software Foundation, Inc.,
// 51 Franklin Street, Fifth Floor, Boston, MA  02110 - 1301, USA.
// -----------------------------------------------------------------------------


// -----------------------------------------------------------------------------
//
// Includes
//
// -----------------------------------------------------------------------------
#include "Main.h"
#include "PaletteChooser.h"
#include "App.h"
#include "Archive/Archive.h"
#include "Archive/ArchiveEntry.h"
#include "Archive/EntryType/EntryType.h"
#include "Graphics/N64Gfx.h"
#include "Graphics/N64GfxEntry.h"
#include "General/Misc.h"
#include "Graphics/Palette/PaletteManager.h"

using namespace slade;


// -----------------------------------------------------------------------------
//
// PaletteChooser Class Functions
//
// -----------------------------------------------------------------------------


// -----------------------------------------------------------------------------
// PaletteChooser class constructor
// -----------------------------------------------------------------------------
PaletteChooser::PaletteChooser(wxWindow* parent, int id) : wxChoice(parent, id)
{
	// Init variables
	pal_global_.copyPalette(app::paletteManager()->globalPalette());

	// Add first 'existing' item
	Append("Existing/Global");

	// Add palette names from palette manager
	for (int a = 0; a < app::paletteManager()->numPalettes(); a++)
		Append(app::paletteManager()->palName(a));

	// Add greyscale palette
	Append("Greyscale");

	// Select first item
	wxChoice::SetSelection(0);

	// Bind events
	Bind(wxEVT_CHOICE, &PaletteChooser::onPaletteChanged, this);
}

// -----------------------------------------------------------------------------
// Called when the current image palette chooser is changed
// -----------------------------------------------------------------------------
void PaletteChooser::onPaletteChanged(wxCommandEvent& e)
{
	signals_.palette_changed();
}

// -----------------------------------------------------------------------------
// Sets the chooser's 'global' palette to the palette contained in [archive], or
// if it doesn't exist, the PaletteManager's global palette
// -----------------------------------------------------------------------------
void PaletteChooser::setGlobalFromArchive(Archive* archive, int lump)
{
	if (!archive)
		pal_global_.copyPalette(app::paletteManager()->globalPalette());

	else if (!misc::loadPaletteFromArchive(&pal_global_, archive, lump))
		setGlobalFromArchive(archive->parentArchive(), lump);
}

// -----------------------------------------------------------------------------
// Returns the selected palette (from the PaletteManager)
// -----------------------------------------------------------------------------
Palette* PaletteChooser::selectedPalette(ArchiveEntry* entry)
{
	const int sel = GetSelection();
	const int num_archive_pals = static_cast<int>(archive_pals_.size());
	if (sel > 0 && sel <= num_archive_pals)
		return &archive_pals_[sel - 1].palette;
	else if (sel > num_archive_pals)
		return app::paletteManager()->palette(sel - 1 - num_archive_pals);
	else if (entry)
	{
		misc::loadPaletteFromArchive(&pal_global_, entry->parent(), misc::detectPaletteHack(entry));
		matchSpritePalette(entry);
	}
	return &pal_global_;
}

// -----------------------------------------------------------------------------
// Returns true if the 'Archive/Global Palette' entry is selected
// -----------------------------------------------------------------------------
bool PaletteChooser::globalSelected() const
{
	return (GetSelection() == 0);
}

// -----------------------------------------------------------------------------
// Selects the palette matching [name], or the default palette if no match was
// found
// -----------------------------------------------------------------------------
void PaletteChooser::selectPalette(wxString name)
{
	// Go through palettes list
	for (unsigned a = 0; a < GetCount(); a++)
	{
		if (S_CMPNOCASE(GetString(a), name))
		{
			SetSelection(a);
			return;
		}
	}

	// No match found, set to default
	SetSelection(0);
}

// -----------------------------------------------------------------------------
// Appends another palette choice to the list.
// This is needed for the "Add Custom Palette" action, since the MainWindow's
// palette chooser is only initialized at startup and the new custom palette,
// without this function, requires exiting and restarting the app to appear in
// the list.
// -----------------------------------------------------------------------------
void PaletteChooser::addPalette(wxString name)
{
	// We want it to be just before the "Greyscale" choice
	if (GetCount() > 2)
		Insert(name, GetCount() - 1);
}

// -----------------------------------------------------------------------------
// Lists the Doom 64 sprite palette entries (PAL*) in [archive] after the
// 'Existing/Global' item, so enemy sprites (which have no embedded palette) can
// be viewed in any of them. If [entry] is a Doom 64 sprite using an external
// palette and 'Existing/Global' is selected, its matching palette (PALxxxx0,
// where xxxx is the first 4 letters of the sprite name) becomes the global one.
// -----------------------------------------------------------------------------
void PaletteChooser::updateArchivePalettes(Archive* archive, ArchiveEntry* entry)
{
	// Read all N64 palette entries from the archive
	vector<ArchivePalette> pals;
	if (archive)
	{
		Archive::SearchOptions opt;
		opt.match_type     = EntryType::fromId("gfx_n64_palette");
		opt.search_subdirs = true;
		if (opt.match_type)
		{
			for (auto* pal_entry : archive->findAll(opt))
			{
				n64gfx::Image img;
				if (!n64gfx::decode(pal_entry->rawData(true), pal_entry->size(), n64gfx::Kind::Palette, img))
					continue;

				pals.emplace_back();
				pals.back().name = pal_entry->name();
				for (int i = 0; i < 256; i++)
					pals.back().palette.setColour(
						i, ColRGBA(img.palette[i][0], img.palette[i][1], img.palette[i][2], 255));
			}
		}
	}

	// Rebuild the list items only if the set of palettes changed
	bool changed = pals.size() != archive_pals_.size();
	for (unsigned a = 0; !changed && a < pals.size(); a++)
		changed = pals[a].name != archive_pals_[a].name;

	if (changed)
	{
		const wxString selected = GetStringSelection();

		for (size_t a = 0; a < archive_pals_.size(); a++)
			Delete(1);
		for (unsigned a = 0; a < pals.size(); a++)
			Insert(wxString::FromUTF8("Doom 64: " + pals[a].name), 1 + a);

		archive_pals_ = std::move(pals);

		// Restore previous selection if possible
		if (selected.empty() || !SetStringSelection(selected))
			SetSelection(0);
	}
	else
		archive_pals_ = std::move(pals); // Same names, palette contents may have been edited

	matchSpritePalette(entry);
}

// -----------------------------------------------------------------------------
// If 'Existing/Global' is selected and [entry] is a Doom 64 sprite (or an image
// that's going to become one, ie. not a Doom 64 texture/graphic/palette), sets
// the global palette to the Doom 64 palette it uses: the lump its header
// references, or else PALxxxx0 where xxxx is the first 4 letters of its name.
// -----------------------------------------------------------------------------
void PaletteChooser::matchSpritePalette(ArchiveEntry* entry)
{
	if (!entry || !entry->type() || archive_pals_.empty() || !globalSelected())
		return;

	const auto& format = entry->type()->formatId();
	if (format == "img_n64_tex" || format == "img_n64_gfx" || format == "img_n64_pal")
		return;

	// Palette lump referenced by the sprite itself
	string ref_name;
	if (format == "img_n64_sprite")
	{
		const auto ref = n64gfx::spritePaletteReference(entry->rawData(true), entry->size());
		if (auto* pal_entry = n64gfx::referencedEntry(entry, ref))
			ref_name = pal_entry->name();
	}

	for (auto& pal : archive_pals_)
		if (!ref_name.empty() && pal.name == ref_name)
		{
			pal_global_.copyPalette(&pal.palette);
			return;
		}

	// Otherwise, go by name
	vector<string> names;
	for (auto& pal : archive_pals_)
		names.push_back(pal.name);

	const int match = n64gfx::findSpritePalette(entry->name(), names);
	if (match >= 0)
		pal_global_.copyPalette(&archive_pals_[match].palette);
}

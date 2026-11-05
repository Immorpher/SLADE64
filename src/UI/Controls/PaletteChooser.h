#pragma once

#include "Graphics/Palette/Palette.h"

namespace slade
{
class Archive;
class ArchiveEntry;

class PaletteChooser : public wxChoice
{
public:
	PaletteChooser(wxWindow* parent, int id);
	~PaletteChooser() = default;

	void     setGlobalFromArchive(Archive* archive, int lump = 0);
	Palette* selectedPalette(ArchiveEntry* entry = nullptr);
	bool     globalSelected() const;
	void     selectPalette(wxString name);
	void     onPaletteChanged(wxCommandEvent& e);
	void     addPalette(wxString name);

	// Doom 64: lists the sprite palette (PAL*) entries found in [archive] right
	// after 'Existing/Global'. If [entry] is a Doom 64 sprite that uses an external
	// palette and 'Existing/Global' is selected, the matching palette is used
	void updateArchivePalettes(Archive* archive, ArchiveEntry* entry);

	// Signals
	struct Signals
	{
		sigslot::signal<> palette_changed;
	};
	Signals& signals() { return signals_; }

private:
	struct ArchivePalette
	{
		string  name;
		Palette palette;
	};

	Palette                 pal_global_;
	vector<ArchivePalette>  archive_pals_; // Items 1..n of the chooser
	Signals signals_;
};
} // namespace slade

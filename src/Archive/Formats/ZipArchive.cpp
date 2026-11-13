
// -----------------------------------------------------------------------------
// SLADE - It's a Doom Editor
// Copyright(C) 2008 - 2022 Simon Judd
//
// Email:       sirjuddington@gmail.com
// Web:         http://slade.mancubus.net
// Filename:    ZipArchive.cpp
// Description: ZipArchive, archive class to handle zip format archives
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
#include "ZipArchive.h"
#include "App.h"
#include "General/Misc.h"
#include "General/UI.h"
#include "UI/WxUtils.h"
#include "Utility/FileUtils.h"
#include "Utility/StringUtils.h"
#include "WadArchive.h"
#include <fstream>

using namespace slade;


// -----------------------------------------------------------------------------
//
// Variables
//
// -----------------------------------------------------------------------------
CVAR(Bool, zip_allow_duplicate_names, false, CVar::Save)


// -----------------------------------------------------------------------------
//
// External Variables
//
// -----------------------------------------------------------------------------
EXTERN_CVAR(Bool, archive_load_data)
EXTERN_CVAR(Int, max_entry_size_mb)


// -----------------------------------------------------------------------------
//
// ZipArchive Class Functions
//
// -----------------------------------------------------------------------------

// -----------------------------------------------------------------------------
// ZipArchive class constructor
// -----------------------------------------------------------------------------
ZipArchive::ZipArchive() : Archive("zip")
{
	if (zip_allow_duplicate_names)
		rootDir()->allowDuplicateNames(true);
}

// -----------------------------------------------------------------------------
// ZipArchive class destructor
// -----------------------------------------------------------------------------
ZipArchive::~ZipArchive()
{
	if (fileutil::fileExists(temp_file_))
		fileutil::removeFile(temp_file_);
}

// -----------------------------------------------------------------------------
// Reads zip data from a file
// Returns true if successful, false otherwise
// -----------------------------------------------------------------------------
bool ZipArchive::open(string_view filename)
{
	// Check the file exists
	if (!fileutil::fileExists(filename))
	{
		global::error = "File does not exist";
		return false;
	}

	// Copy the zip to a temp file (for use when saving)
	generateTempFileName(filename);
	fileutil::copyFile(filename, temp_file_);

	// Open the file
	wxFFileInputStream in(wxutil::strFromView(filename));
	if (!in.IsOk())
	{
		global::error = "Unable to open file";
		return false;
	}

	// Create zip stream
	wxZipInputStream zip(in);
	if (!zip.IsOk())
	{
		global::error = "Invalid zip file";
		return false;
	}

	// Stop announcements (don't want to be announcing modification due to entries being added etc)
	const ArchiveModSignalBlocker sig_blocker{ *this };

	// Go through all zip entries
	int  entry_index = 0;
	auto zip_entry   = zip.GetNextEntry();
	ui::setSplashProgressMessage("Reading zip data");
	while (zip_entry)
	{
		ui::setSplashProgress(-1.0f);
		if (zip_entry->GetMethod() != wxZIP_METHOD_DEFLATE && zip_entry->GetMethod() != wxZIP_METHOD_STORE)
		{
			global::error = "Unsupported zip compression method";
			return false;
		}

		if (!zip_entry->IsDir())
		{
			// Get the entry name as a Path (so we can break it up)
			strutil::Path fn(wxutil::strToView(zip_entry->GetName(wxPATH_UNIX)));

			// Create entry
			auto new_entry = std::make_shared<ArchiveEntry>(
				misc::fileNameToLumpName(fn.fileName()), zip_entry->GetSize());

			// Setup entry info
			new_entry->setLoaded(false);
			new_entry->exProp("ZipIndex") = entry_index;

			// Add entry and directory to directory tree
			auto ndir = createDir(fn.path(true));
			ndir->addEntry(new_entry, true);

			if (const auto ze_size = zip_entry->GetSize(); ze_size < max_entry_size_mb * 1024 * 1024)
			{
				if (ze_size > 0)
				{
					vector<uint8_t> data(ze_size);
					zip.Read(data.data(), ze_size); // Note: this is where exceedingly large files cause an exception.
					new_entry->importMem(data.data(), static_cast<uint32_t>(ze_size));
				}
				new_entry->setLoaded(true);

				// Determine its type
				EntryType::detectEntryType(*new_entry);

				// Unload data if needed
				if (!archive_load_data)
					new_entry->unloadData();
			}
			else
			{
				global::error = fmt::format("Entry too large: {} is {} mb", fn.fullPath(), ze_size / (1 << 20));
				return false;
			}
		}
		else
		{
			// Zip entry is a directory, add it to the directory tree
			strutil::Path fn(wxutil::strToView(zip_entry->GetName(wxPATH_UNIX)));
			createDir(fn.path(true));
		}

		// Go to next entry in the zip file
		delete zip_entry;
		zip_entry = zip.GetNextEntry();
		entry_index++;
	}
	ui::updateSplash();

	// Set all entries/directories to unmodified
	vector<ArchiveEntry*> entry_list;
	putEntryTreeAsList(entry_list);
	for (auto& entry : entry_list)
		entry->setState(ArchiveEntry::State::Unmodified);

	// Enable announcements
	sig_blocker.unblock();

	// Setup variables
	filename_      = filename;
	file_modified_ = fileutil::fileModifiedTime(filename);
	setModified(false);
	on_disk_ = true;

	ui::setSplashProgressMessage("");

	return true;
}

// -----------------------------------------------------------------------------
// Reads zip format data from a MemChunk
// Returns true if successful, false otherwise
// -----------------------------------------------------------------------------
bool ZipArchive::open(MemChunk& mc)
{
	// Write the MemChunk to a temp file
	const auto tempfile = app::path("slade-temp-open.zip", app::Dir::Temp);
	mc.exportFile(tempfile);

	// Load the file
	const bool success = open(tempfile);

	// Clean up
	fileutil::removeFile(tempfile);

	return success;
}

// -----------------------------------------------------------------------------
// Writing zips is not supported - this class only exists to read the program's
// own resource archive (slade64.pk3)
// -----------------------------------------------------------------------------
bool ZipArchive::write(MemChunk& mc, bool update)
{
	global::error = "Writing zip archives is not supported";
	return false;
}

bool ZipArchive::write(string_view filename, bool update)
{
	global::error = "Writing zip archives is not supported";
	return false;
}

// -----------------------------------------------------------------------------
// Loads an entry's data from the saved copy of the archive if any.
// Returns false if the entry is invalid, doesn't belong to the archive or
// doesn't exist in the saved copy, true otherwise.
// -----------------------------------------------------------------------------
bool ZipArchive::loadEntryData(ArchiveEntry* entry)
{
	// Check that the entry belongs to this archive
	if (entry->parent() != this)
	{
		log::error("ZipArchive::loadEntryData: Entry {} attempting to load data from wrong parent!", entry->name());
		return false;
	}

	// Do nothing if the entry's size is zero,
	// or if it has already been loaded
	if (entry->size() == 0 || entry->isLoaded())
	{
		entry->setLoaded();
		return true;
	}

	// Check that the entry has a zip index
	int zip_index;
	if (entry->exProps().contains("ZipIndex"))
		zip_index = entry->exProp<int>("ZipIndex");
	else
	{
		log::error("ZipArchive::loadEntryData: Entry {} has no zip entry index!", entry->name());
		return false;
	}

	// Open the file
	wxFFileInputStream in(filename_);
	if (!in.IsOk())
	{
		log::error("ZipArchive::loadEntryData: Unable to open zip file \"{}\"!", filename_);
		return false;
	}

	// Create zip stream
	wxZipInputStream zip(in);
	if (!zip.IsOk())
	{
		log::error("ZipArchive::loadEntryData: Invalid zip file \"{}\"!", filename_);
		return false;
	}

	// Lock entry state
	entry->lockState();

	// Skip to correct entry in zip
	auto zentry = zip.GetNextEntry();
	for (long a = 0; a < zip_index; a++)
	{
		delete zentry;
		zentry = zip.GetNextEntry();
	}

	// Abort if entry doesn't exist in zip (some kind of error)
	if (!zentry)
	{
		log::error("Error: ZipEntry for entry \"{}\" does not exist in zip", entry->name());
		return false;
	}

	// Read the data
	vector<uint8_t> data(zentry->GetSize());
	zip.Read(data.data(), zentry->GetSize());
	entry->importMem(data.data(), static_cast<uint32_t>(zentry->GetSize()));

	// Set the entry to loaded
	entry->setLoaded();
	entry->unlockState();

	// Clean up
	delete zentry;

	return true;
}

// -----------------------------------------------------------------------------
// Adds [entry] to the end of the namespace matching [add_namespace].
// If [copy] is true a copy of the entry is added.
// Returns the added entry or NULL if the entry is invalid
//
// In a zip archive, a namespace is simply a first-level directory, ie
// <root>/<namespace>
// -----------------------------------------------------------------------------
shared_ptr<ArchiveEntry> ZipArchive::addEntry(shared_ptr<ArchiveEntry> entry, string_view add_namespace)
{
	// Check namespace
	if (add_namespace.empty() || add_namespace == "global")
		return Archive::addEntry(entry, 0xFFFFFFFF, nullptr);

	// Get/Create namespace dir
	const auto dir = createDir(strutil::lower(add_namespace));

	// Add the entry to the dir
	return Archive::addEntry(entry, 0xFFFFFFFF, dir.get());
}

// -----------------------------------------------------------------------------
// Returns the mapdesc_t information about the map at [entry], if [entry] is
// actually a valid map (ie. a wad archive in the maps folder)
// -----------------------------------------------------------------------------
Archive::MapDesc ZipArchive::mapDesc(ArchiveEntry* maphead)
{
	MapDesc map;

	// Check entry
	if (!checkEntry(maphead))
		return map;

	// Check entry type
	if (maphead->type()->formatId() != "archive_wad")
		return map;

	// Check entry directory
	if (maphead->parentDir()->parent() != rootDir() || maphead->parentDir()->name() != "maps")
		return map;

	// Setup map info
	map.archive = true;
	map.head    = maphead->getShared();
	map.end     = maphead->getShared();
	map.name    = maphead->upperNameNoExt();

	return map;
}

// -----------------------------------------------------------------------------
// Detects all the maps in the archive and returns a vector of information about
// them.
// -----------------------------------------------------------------------------
vector<Archive::MapDesc> ZipArchive::detectMaps()
{
	vector<MapDesc> ret;

	// Get the maps directory
	const auto mapdir = dirAtPath("maps");
	if (!mapdir)
		return ret;

	// Go through entries in map dir
	for (unsigned a = 0; a < mapdir->numEntries(); a++)
	{
		auto entry = mapdir->sharedEntryAt(a);

		// Maps can only be wad archives
		if (entry->type()->formatId() != "archive_wad")
			continue;

		// Detect map format (probably kinda slow but whatever, no better way to do it really)
		auto       format = MapFormat::Unknown;
		WadArchive tempwad;
		tempwad.open(entry->data());
		auto emaps = tempwad.detectMaps();
		if (!emaps.empty())
			format = emaps[0].format;

		// Add map description
		MapDesc md;
		md.head    = entry;
		md.end     = entry;
		md.archive = true;
		md.name    = entry->upperNameNoExt();
		md.format  = format;
		ret.push_back(md);
	}

	return ret;
}

// -----------------------------------------------------------------------------
// Returns the first entry matching the search criteria in [options], or null if
// no matching entry was found
// -----------------------------------------------------------------------------
ArchiveEntry* ZipArchive::findFirst(SearchOptions& options)
{
	// Init search variables
	auto dir = rootDir().get();

	// Check for search directory (overrides namespace)
	if (options.dir)
	{
		dir = options.dir;
	}
	// Check for namespace
	else if (!options.match_namespace.empty())
	{
		dir = dirAtPath(options.match_namespace);

		// If the requested namespace doesn't exist, return nothing
		if (!dir)
			return nullptr;
		else
			options.search_subdirs = true; // Namespace search always includes namespace subdirs
	}

	// Do default search
	auto opt            = options;
	opt.dir             = dir;
	opt.match_namespace = "";
	return Archive::findFirst(opt);
}

// -----------------------------------------------------------------------------
// Returns the last entry matching the search criteria in [options], or null if
// no matching entry was found
// -----------------------------------------------------------------------------
ArchiveEntry* ZipArchive::findLast(SearchOptions& options)
{
	// Init search variables
	auto dir = rootDir().get();

	// Check for search directory (overrides namespace)
	if (options.dir)
	{
		dir = options.dir;
	}
	// Check for namespace
	else if (!options.match_namespace.empty())
	{
		dir = dirAtPath(options.match_namespace);

		// If the requested namespace doesn't exist, return nothing
		if (!dir)
			return nullptr;
		else
			options.search_subdirs = true; // Namespace search always includes namespace subdirs
	}

	// Do default search
	auto opt            = options;
	opt.dir             = dir;
	opt.match_namespace = "";
	return Archive::findLast(opt);
}

// -----------------------------------------------------------------------------
// Returns all entries matching the search criteria in [options]
// -----------------------------------------------------------------------------
vector<ArchiveEntry*> ZipArchive::findAll(SearchOptions& options)
{
	// Init search variables
	auto                  dir = rootDir().get();
	vector<ArchiveEntry*> ret;

	// Check for search directory (overrides namespace)
	if (options.dir)
	{
		dir = options.dir;
	}
	// Check for namespace
	else if (!options.match_namespace.empty())
	{
		dir = dirAtPath(options.match_namespace);

		// If the requested namespace doesn't exist, return nothing
		if (!dir)
			return ret;
		else
			options.search_subdirs = true; // Namespace search always includes namespace subdirs
	}

	// Do default search
	auto opt            = options;
	opt.dir             = dir;
	opt.match_namespace = "";
	return Archive::findAll(opt);
}

// -----------------------------------------------------------------------------
// Generates the temp file path to use, from [filename].
// The temp file will be in the configured temp folder
// -----------------------------------------------------------------------------
void ZipArchive::generateTempFileName(string_view filename)
{
	const strutil::Path tfn(filename);
	temp_file_ = app::path(tfn.fileName(), app::Dir::Temp);
	if (wxFileExists(temp_file_))
	{
		// Make sure we don't overwrite an existing temp file
		// (in case there are multiple zips open with the same name)
		int n = 1;
		while (true)
		{
			temp_file_ = app::path(fmt::format("{}.{}", tfn.fileName(), n), app::Dir::Temp);
			if (!wxFileExists(temp_file_))
				break;

			n++;
		}
	}
}

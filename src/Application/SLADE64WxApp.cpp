
// -----------------------------------------------------------------------------
// SLADE - It's a Doom Editor
// Copyright(C) 2008 - 2022 Simon Judd
//
// Email:       sirjuddington@gmail.com
// Web:         http://slade.mancubus.net
// Filename:    SLADE64WxApp.cpp
// Description: SLADE64WxApp class functions.
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
#include "SLADE64WxApp.h"
#include "App.h"
#include "Archive/ArchiveManager.h"
#include "General/Console.h"
#include "MainEditor/MainEditor.h"
#include "MainEditor/UI/ArchiveManagerPanel.h"
#include "MainEditor/UI/MainWindow.h"
#include "OpenGL/OpenGL.h"
#include "UI/WxUtils.h"
#include "Utility/Parser.h"
#include "Utility/StringUtils.h"
#include <wx/statbmp.h>
#include <wx/filefn.h>
#undef BOOL
#ifdef UPDATEREVISION
#include "gitinfo.h"
#endif

using namespace slade;


// -----------------------------------------------------------------------------
//
// Variables
//
// -----------------------------------------------------------------------------
namespace slade::global
{
string error;

#ifdef GIT_DESCRIPTION
string sc_rev = GIT_DESCRIPTION;
#else
string sc_rev;
#endif

#ifdef DEBUG
bool debug = true;
#else
bool   debug = false;
#endif

int win_version_major = 0;
int win_version_minor = 0;
} // namespace slade::global

string current_action;
CVAR(String, dir_last, "", CVar::Flag::Save)


// -----------------------------------------------------------------------------
//
// Classes
//
// -----------------------------------------------------------------------------


// -----------------------------------------------------------------------------
// SLADE64Log class
//
// Extension of the wxLog class to send all wxWidgets log messages
// to the SLADE log implementation
// -----------------------------------------------------------------------------
class SLADE64Log : public wxLog
{
protected:
	// wx2.9.x is no longer supported.
#if (wxMAJOR_VERSION < 3)
#error This will not compile with wxWidgets older than 3.0.0 !
#endif
	void DoLogText(const wxString& msg) override
	{
		if (msg.Lower().Contains("error"))
			log::error(msg.Right(msg.size() - 10));
		else if (msg.Lower().Contains("warning"))
			log::warning(msg.Right(msg.size() - 10));
		else
			log::info(msg.Right(msg.size() - 10));
	}

public:
	SLADE64Log()           = default;
	~SLADE64Log() override = default;
};


// -----------------------------------------------------------------------------
// SLADE64StackTrace class
//
// Extension of the wxStackWalker class that formats stack trace
// information to a multi-line string, that can be retrieved via
// getTraceString(). wxStackWalker is currently unimplemented on some
// platforms, so unfortunately it has to be disabled there
// -----------------------------------------------------------------------------
#if wxUSE_STACKWALKER
class SLADE64StackTrace : public wxStackWalker
{
public:
	SLADE64StackTrace() : stack_trace_("Stack Trace:\n") {}
	~SLADE64StackTrace() override = default;

	wxString traceString() const { return stack_trace_; }
	wxString topLevel() const { return top_level_; }

	void OnStackFrame(const wxStackFrame& frame) override
	{
		wxString location = "[unknown location] ";
		if (frame.HasSourceLocation())
			location = wxString::Format("(%s:%ld) ", frame.GetFileName(), frame.GetLine());

		wxUIntPtr address   = wxPtrToUInt(frame.GetAddress());
		wxString  func_name = frame.GetName();
		if (func_name.IsEmpty())
			func_name = wxString::Format("[unknown:%lu]", address);

		wxString line = wxString::Format("%s%s", location, func_name);
		stack_trace_.Append(wxString::Format("%ld: %s\n", frame.GetLevel(), line));

		if (frame.GetLevel() == 0)
			top_level_ = line;
	}

private:
	wxString stack_trace_;
	wxString top_level_;
};


// -----------------------------------------------------------------------------
// SLADE64CrashDialog class
//
// A simple dialog that displays a crash message and a scrollable,
// multi-line textbox with a stack trace
// -----------------------------------------------------------------------------
class SLADE64CrashDialog : public wxDialog
{
public:
	SLADE64CrashDialog() : wxDialog(wxGetApp().GetTopWindow(), -1, "SLADE64 Application Crash")
	{
		// Setup sizer
		auto sizer = new wxBoxSizer(wxVERTICAL);
		SetSizer(sizer);

		auto hbox = new wxBoxSizer(wxHORIZONTAL);
		sizer->Add(hbox, 0, wxEXPAND);

		// Add dead doomguy picture
		app::archiveManager()
			.programResourceArchive()
			->entryAtPath("images/STFDEAD0.png")
			->exportFile(app::path("STFDEAD0.png", app::Dir::Temp));
		wxImage img;
		img.LoadFile(app::path("STFDEAD0.png", app::Dir::Temp));
		img.Rescale(img.GetWidth(), img.GetHeight(), wxIMAGE_QUALITY_NEAREST);
		auto picture = new wxStaticBitmap(this, -1, wxBitmap(img));
		hbox->Add(picture, 0, wxALIGN_CENTER_VERTICAL | wxLEFT | wxTOP | wxBOTTOM, 10);

		// Add general crash message
		string message =
			"SLADE64 has crashed unexpectedly. To help fix the problem that caused this crash, "
			"please click 'Create GitHub Issue' below and copy+paste the stack trace into the "
			"issue details, along with a description of what you were doing at the time of the "
			"crash.";
		auto label = new wxStaticText(this, -1, message);
		hbox->Add(label, 0, wxALIGN_CENTER_VERTICAL | wxALL, 10);
		label->Wrap(480 - 20 - picture->GetSize().x);

		// Add stack trace text area
		text_stack_ = new wxTextCtrl(
			this, -1, wxEmptyString, wxDefaultPosition, wxDefaultSize, wxTE_MULTILINE | wxTE_READONLY | wxHSCROLL);
		// text_stack_->SetValue(trace_);
		text_stack_->SetFont(wxFont(8, wxFONTFAMILY_MODERN, wxFONTSTYLE_NORMAL, wxFONTWEIGHT_NORMAL));
		sizer->Add(new wxStaticText(this, -1, "Crash Information:"), 0, wxLEFT | wxRIGHT, 10);
		sizer->AddSpacer(2);
		sizer->Add(text_stack_, 1, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, 10);

		// Add 'Copy Stack Trace' button
		hbox = new wxBoxSizer(wxHORIZONTAL);
		sizer->Add(hbox, 0, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, 6);
		btn_copy_trace_ = new wxButton(this, -1, "Copy Stack Trace");
		hbox->AddStretchSpacer();
		hbox->Add(btn_copy_trace_, 0, wxLEFT | wxRIGHT | wxBOTTOM, 4);
		btn_copy_trace_->Bind(wxEVT_COMMAND_BUTTON_CLICKED, &SLADE64CrashDialog::onBtnCopyTrace, this);

		// Add 'Exit SLADE' button
		btn_exit_ = new wxButton(this, -1, "Exit SLADE64");
		hbox->Add(btn_exit_, 0, wxLEFT | wxRIGHT | wxBOTTOM, 4);
		btn_exit_->Bind(wxEVT_COMMAND_BUTTON_CLICKED, &SLADE64CrashDialog::onBtnExit, this);

		// Setup layout
		wxDialog::Layout();
		SetInitialSize(wxSize(500, 600));
		CenterOnParent();
		Show(false);
	}

	~SLADE64CrashDialog() override = default;

	void loadStackTrace(const SLADE64StackTrace& st)
	{
		top_level_ = st.topLevel();

		// SLADE info
		if (global::sc_rev.empty())
			trace_ = wxString::Format("Version: %s", app::version().toString());
		else
			trace_ = wxString::Format("Version: %s (%s)", app::version().toString(), global::sc_rev);
		if (app::platform() == app::Platform::Windows)
			trace_ += wxString::Format(" (%s)\n", app::isWin64Build() ? "x64" : "x86");
		else
			trace_ += "\n";
		if (current_action.empty())
			trace_ += "No current action\n";
		else
			trace_ += wxString::Format("Current action: %s", current_action);
		trace_ += "\n";

		// System info
		gl::Info gl_info = gl::sysInfo();
		trace_ += "Operating System: " + wxGetOsDescription() + "\n";
		trace_ += "Graphics Vendor: " + gl_info.vendor + "\n";
		trace_ += "Graphics Hardware: " + gl_info.renderer + "\n";
		trace_ += "OpenGL Version: " + gl_info.version + "\n";

		// Stack trace
		trace_ += "\n";
		trace_ += st.traceString();

		// Last 10 log lines
		trace_ += "\nLast Log Messages:\n";
		auto& log = log::history();
		for (auto a = log.size() - 10; a < log.size(); a++)
			trace_ += log[a].message + "\n";

		// Set stack trace text
		text_stack_->SetValue(trace_);

		// Dump stack trace to a file (just in case)
		wxFile file(app::path("slade64_crash.log", app::Dir::User), wxFile::write);
		file.Write(trace_);
		file.Close();

		// Also dump stack trace to console
		std::cerr << trace_;
	}

	void onBtnCopyTrace(wxCommandEvent& e)
	{
		if (wxTheClipboard->Open())
		{
			wxTheClipboard->SetData(new wxTextDataObject(trace_));
			wxTheClipboard->Flush();
			wxTheClipboard->Close();
			wxMessageBox("Stack trace successfully copied to clipboard");
		}
		else
			wxMessageBox(
				"Unable to access the system clipboard, please select+copy the text above manually",
				wxMessageBoxCaptionStr,
				wxICON_EXCLAMATION);
	}

	void onBtnExit(wxCommandEvent& e) { EndModal(wxID_OK); }

private:
	wxTextCtrl* text_stack_;
	wxButton*   btn_copy_trace_;
	wxButton*   btn_exit_;
	wxString    trace_;
	wxString    top_level_;
};
#endif // wxUSE_STACKWALKER


// -----------------------------------------------------------------------------
// MainAppFileListener and related Classes
//
// wxWidgets IPC classes used to send filenames of archives to open
// from one SLADE instance to another in the case where a second
// instance is opened
// -----------------------------------------------------------------------------
class MainAppFLConnection : public wxConnection
{
public:
	MainAppFLConnection()           = default;
	~MainAppFLConnection() override = default;

	bool OnAdvise(const wxString& topic, const wxString& item, const void* data, size_t size, wxIPCFormat format)
		override
	{
		return true;
	}

	bool OnPoke(const wxString& topic, const wxString& item, const void* data, size_t size, wxIPCFormat format) override
	{
		app::archiveManager().openArchive(item.ToStdString());
		return true;
	}
};

class MainAppFileListener : public wxServer
{
public:
	MainAppFileListener()           = default;
	~MainAppFileListener() override = default;

	wxConnectionBase* OnAcceptConnection(const wxString& topic) override { return new MainAppFLConnection(); }
};

class MainAppFLClient : public wxClient
{
public:
	MainAppFLClient()           = default;
	~MainAppFLClient() override = default;

	wxConnectionBase* OnMakeConnection() override { return new MainAppFLConnection(); }
};


// -----------------------------------------------------------------------------
//
// SLADE64WxApp Class Functions
//
// -----------------------------------------------------------------------------
IMPLEMENT_APP(SLADE64WxApp)


// -----------------------------------------------------------------------------
// Checks if another instance of SLADE is already running, and if so, sends the
// args to the file listener of the existing SLADE process.
// Returns false if another instance was found and the new SLADE was started
// with arguments
// -----------------------------------------------------------------------------
bool SLADE64WxApp::singleInstanceCheck()
{
	auto data_dir = wxStandardPaths::Get().GetUserDataDir();
	if (!wxDirExists(data_dir))
		wxMkdir(data_dir);

	single_instance_checker_ = new wxSingleInstanceChecker;
	single_instance_checker_->Create(wxString::Format("SLADE64-%s", app::version().toString()), data_dir);

	if (argc == 1)
		return true;

	if (single_instance_checker_->IsAnotherRunning())
	{
		delete single_instance_checker_;

		// Connect to the file listener of the existing SLADE process
		auto client     = std::make_unique<MainAppFLClient>();
		auto connection = client->MakeConnection(wxGetHostName(), "SLADE64_MAFL", "files");

		if (connection)
		{
			// Send args as archives to open
			for (int a = 1; a < argc; a++)
				connection->Poke(argv[a], argv[a]);

			connection->Disconnect();
		}

		return false;
	}

	return true;
}

// -----------------------------------------------------------------------------
// Application initialization, run when program is started
// -----------------------------------------------------------------------------
bool SLADE64WxApp::OnInit()
{
	// Check if an instance of SLADE is already running
	if (!singleInstanceCheck())
	{
		printf("Found active instance. Quitting.\n");
		return false;
	}

	// Start up file listener
	file_listener_ = new MainAppFileListener();
	file_listener_->Create("SLADE64_MAFL");

	// Setup system options
	wxSystemOptions::SetOption("mac.listctrl.always_use_generic", 1);

	// Set application name (for wx directory stuff)
#ifdef __WINDOWS__
	wxApp::SetAppName("SLADE64");
#else
    wxApp::SetAppName("slade64");
#endif

	// Handle exceptions using wxDebug stuff, but only in release mode
#ifdef NDEBUG
	wxHandleFatalExceptions(true);
#endif

	// Load image handlers
	wxInitAllImageHandlers();

#ifdef __APPLE__
	// Should be constant, wxWidgets Cocoa backend scales everything under the hood
	const double ui_scale = 1.0;
#else  // !__APPLE__
    // Calculate scaling factor (from system ppi)
    wxMemoryDC dc;
    double     ui_scale = (double)(dc.GetPPI().x) / 96.0;
    if (ui_scale < 1.)
        ui_scale = 1.;
#endif // __APPLE__

	// Get Windows version
#ifdef __WXMSW__
	wxGetOsVersion(&global::win_version_major, &global::win_version_minor);
	log::info("Windows Version: {}.{}", global::win_version_major, global::win_version_minor);
#endif

	// Reroute wx log messages
	wxLog::SetActiveTarget(new SLADE64Log());

	// Get command line arguments
	vector<string> args;
	for (int a = 1; a < argc; a++)
		args.push_back(argv[a].ToStdString());

	// Init application
	try
	{
		if (!app::init(args, ui_scale))
			return false;
	}
	catch (const std::exception& ex)
	{
		log::error("Exception during SLADE64 initialization: {}", ex.what());
		throw;
	}

	// Init crash dialog
	// Do it now rather than after a crash happens, since it may fail depending on the type of crash
#if wxUSE_STACKWALKER
#ifndef _DEBUG
	crash_dialog_ = new SLADE64CrashDialog();
#endif //_DEBUG
#endif // wxUSE_STACKWALKER

	// Bind events
	Bind(wxEVT_MENU, &SLADE64WxApp::onMenu, this);
	Bind(wxEVT_ACTIVATE_APP, &SLADE64WxApp::onActivate, this);

	return true;
}

// -----------------------------------------------------------------------------
// Application shutdown, run when program is closed
// -----------------------------------------------------------------------------
int SLADE64WxApp::OnExit()
{
	delete single_instance_checker_;
	delete file_listener_;

	return 0;
}

// -----------------------------------------------------------------------------
// Handler for when a fatal exception occurs - show the stack trace/crash dialog
// if it's configured to be used
// -----------------------------------------------------------------------------
void SLADE64WxApp::OnFatalException()
{
#if wxUSE_STACKWALKER
#ifndef _DEBUG
	SLADE64StackTrace st;
	st.WalkFromException();
	crash_dialog_->loadStackTrace(st);
	crash_dialog_->ShowModal();
#endif //_DEBUG
#endif // wxUSE_STACKWALKER
}

#ifdef __APPLE__
void SLADE64WxApp::MacOpenFile(const wxString& fileName)
{
	theMainWindow->archiveManagerPanel()->openFile(fileName);
}
#endif // __APPLE__

// -----------------------------------------------------------------------------
//
// SLADE64WxApp Class Events
//
// -----------------------------------------------------------------------------


// -----------------------------------------------------------------------------
// Called when a menu item is selected in the application
// -----------------------------------------------------------------------------
void SLADE64WxApp::onMenu(wxCommandEvent& e)
{
	bool handled = false;

	// Find applicable action
	auto s_action = SAction::fromWxId(e.GetId());
	auto action   = s_action->id();

	// Handle action if valid
	if (action != "invalid")
	{
		current_action = action;
		SActionHandler::setWxIdOffset(e.GetId() - s_action->wxId());
		handled = SActionHandler::doAction(action);

		// Check if triggering object is a menu item
		if (s_action && s_action->type() == SAction::Type::Check)
		{
			if (e.GetEventObject() && e.GetEventObject()->IsKindOf(wxCLASSINFO(wxMenuItem)))
			{
				auto item = (wxMenuItem*)e.GetEventObject();
				item->Check(s_action->isChecked());
			}
		}

		current_action = "";
	}

	// If not handled, let something else handle it
	if (!handled)
		e.Skip();
}

// -----------------------------------------------------------------------------
// Called when the app gains focus
// -----------------------------------------------------------------------------
void SLADE64WxApp::onActivate(wxActivateEvent& e)
{
	if (!e.GetActive() || app::isExiting())
	{
		e.Skip();
		return;
	}

	// Check open directory archives for changes on the file system
	if (theMainWindow && theMainWindow->archiveManagerPanel())
		theMainWindow->archiveManagerPanel()->checkDirArchives();

	e.Skip();
}


// -----------------------------------------------------------------------------
//
// Console Commands
//
// -----------------------------------------------------------------------------

CONSOLE_COMMAND(crash, 0, false)
{
	if (wxMessageBox(
			"Yes, this command does actually exist and *will* crash the program. Do you really want it to crash?",
			"...Really?",
			wxYES_NO | wxCENTRE)
		== wxYES)
	{
		uint8_t* test = nullptr;
		*test         = 5;
	}
}

CONSOLE_COMMAND(quit, 0, true)
{
	bool save_config = true;
	for (auto& arg : args)
	{
		if (strutil::equalCI(arg, "nosave"))
			save_config = false;
	}

	app::exit(save_config);
}


// -----------------------------------------------------------------------------
// SLADE - It's a Doom Editor
// Copyright(C) 2008 - 2022 Simon Judd
//
// Email:       sirjuddington@gmail.com
// Web:         http://slade.mancubus.net
// Filename:    KeyBind.cpp
// Description: Input key binding system
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
#include "KeyBind.h"
#include "Utility/StringUtils.h"
#include "Utility/Tokenizer.h"

using namespace slade;


// -----------------------------------------------------------------------------
//
// Variables
//
// -----------------------------------------------------------------------------
namespace
{
vector<KeyBind>         keybinds;
vector<KeyBind>         keybinds_sorted;
KeyBind                 kb_none("-none-");
vector<KeyBindHandler*> kb_handlers;
} // namespace


// -----------------------------------------------------------------------------
//
// Keypress Struct Functions
//
// -----------------------------------------------------------------------------


// -----------------------------------------------------------------------------
// Returns a string representation of the keypress
// -----------------------------------------------------------------------------
string Keypress::asString() const
{
	if (key.empty())
		return "";

	string ret = "";
	if (ctrl)
		ret += "Ctrl+";
	if (alt)
		ret += "Alt+";
	if (shift)
		ret += "Shift+";

	string keyname = key;
	std::replace(keyname.begin(), keyname.end(), '_', ' ');
	strutil::capitalizeIP(keyname);
	ret += keyname;

	return ret;
}


// -----------------------------------------------------------------------------
//
// KeyBind Class Functions
//
// -----------------------------------------------------------------------------


// -----------------------------------------------------------------------------
// Adds a key combination to the keybind
// -----------------------------------------------------------------------------
void KeyBind::addKey(string_view key, bool alt, bool ctrl, bool shift)
{
	keys_.emplace_back(key, alt, ctrl, shift);
}

// -----------------------------------------------------------------------------
// Returns a string representation of all the keys bound to this keybind
// -----------------------------------------------------------------------------
string KeyBind::keysAsString()
{
	string ret;

	for (unsigned a = 0; a < keys_.size(); a++)
	{
		if (keys_[a].ctrl)
			ret += "Ctrl+";
		if (keys_[a].alt)
			ret += "Alt+";
		if (keys_[a].shift)
			ret += "Shift+";

		auto keyname = keys_[a].key;
		std::replace(keyname.begin(), keyname.end(), '_', ' ');
		strutil::capitalizeIP(keyname);
		ret += keyname;

		if (a < keys_.size() - 1)
			ret += ", ";
	}

	if (ret.empty())
		return "None";
	else
		return ret;
}


// -----------------------------------------------------------------------------
//
// KeyBind Class Static Functions
//
// -----------------------------------------------------------------------------


// -----------------------------------------------------------------------------
// Returns the keybind [name]
// -----------------------------------------------------------------------------
KeyBind& KeyBind::bind(string_view name)
{
	for (auto& keybind : keybinds)
	{
		if (keybind.name_ == name)
			return keybind;
	}

	return kb_none;
}

// -----------------------------------------------------------------------------
// Returns a list of all keybind names bound to [key]
// -----------------------------------------------------------------------------
vector<string> KeyBind::bindsForKey(Keypress key)
{
	vector<string> matches;

	// Go through all keybinds
	for (auto& kb : keybinds)
	{
		// Go through all keys bound to this keybind
		for (unsigned a = 0; a < kb.keys_.size(); a++)
		{
			auto& kp = kb.keys_[a];

			// Check for match with keypress
			if (kp.shift == key.shift && kp.alt == key.alt && kp.ctrl == key.ctrl && kp.key == key.key)
				matches.push_back(kb.name_);
		}
	}

	return matches;
}

// -----------------------------------------------------------------------------
// Returns true if keybind [name] is currently pressed
// -----------------------------------------------------------------------------
bool KeyBind::isPressed(string_view name)
{
	return bind(name).pressed_;
}

// -----------------------------------------------------------------------------
// Adds a new keybind
// -----------------------------------------------------------------------------
bool KeyBind::addBind(
	string_view     name,
	const Keypress& key,
	string_view     desc,
	string_view     group,
	bool            ignore_shift,
	int             priority)
{
	// Find keybind
	KeyBind* bind = nullptr;
	for (auto& keybind : keybinds)
	{
		if (keybind.name_ == name)
		{
			bind = &keybind;
			break;
		}
	}

	// Add keybind if it doesn't exist
	if (!bind)
	{
		keybinds.emplace_back(name);
		bind                = &keybinds.back();
		bind->ignore_shift_ = ignore_shift;
	}

	// Set keybind description/group
	if (!desc.empty())
	{
		bind->description_ = desc;
		bind->group_       = group;
	}

	// Check if the key is already bound to it
	for (unsigned a = 0; a < bind->keys_.size(); a++)
	{
		if (bind->keys_[a].alt == key.alt && bind->keys_[a].ctrl == key.ctrl && bind->keys_[a].shift == key.shift
			&& bind->keys_[a].key == key.key)
		{
			// It is, remove the bind
			bind->keys_.erase(bind->keys_.begin() + a);
			return false;
		}
	}

	// Set priority
	if (priority >= 0)
		bind->priority_ = priority;

	// Add the keybind
	bind->addKey(key.key, key.alt, key.ctrl, key.shift);

	return true;
}

// -----------------------------------------------------------------------------
// Returns a string representation of [key]
// -----------------------------------------------------------------------------
string KeyBind::keyName(int key)
{
	// Return string representation of key id
	switch (key)
	{
	case WXK_BACK: return "backspace";
	case WXK_TAB: return "tab";
	case WXK_RETURN: return "return";
	case WXK_ESCAPE: return "escape";
	case WXK_SPACE: return "space";
	case WXK_DELETE: return "delete";
	case WXK_CLEAR: return "clear";
	case WXK_SHIFT: return "shift";
	case WXK_ALT: return "alt";
	case WXK_PAUSE: return "pause";
	case WXK_END: return "end";
	case WXK_HOME: return "home";
	case WXK_LEFT: return "left";
	case WXK_UP: return "up";
	case WXK_RIGHT: return "right";
	case WXK_DOWN: return "down";
	case WXK_INSERT: return "insert";
	case WXK_NUMPAD0: return "num_0";
	case WXK_NUMPAD1: return "num_1";
	case WXK_NUMPAD2: return "num_2";
	case WXK_NUMPAD3: return "num_3";
	case WXK_NUMPAD4: return "num_4";
	case WXK_NUMPAD5: return "num_5";
	case WXK_NUMPAD6: return "num_6";
	case WXK_NUMPAD7: return "num_7";
	case WXK_NUMPAD8: return "num_8";
	case WXK_NUMPAD9: return "num_9";
	case WXK_ADD: return "plus";
	case WXK_SUBTRACT: return "minus";
	case WXK_F1: return "f1";
	case WXK_F2: return "f2";
	case WXK_F3: return "f3";
	case WXK_F4: return "f4";
	case WXK_F5: return "f5";
	case WXK_F6: return "f6";
	case WXK_F7: return "f7";
	case WXK_F8: return "f8";
	case WXK_F9: return "f9";
	case WXK_F10: return "f10";
	case WXK_F11: return "f11";
	case WXK_F12: return "f12";
	case WXK_F13: return "f13";
	case WXK_F14: return "f14";
	case WXK_F15: return "f15";
	case WXK_F16: return "f16";
	case WXK_F17: return "f17";
	case WXK_F18: return "f18";
	case WXK_F19: return "f19";
	case WXK_F20: return "f20";
	case WXK_F21: return "f21";
	case WXK_F22: return "f22";
	case WXK_F23: return "f23";
	case WXK_F24: return "f24";
	case WXK_NUMLOCK: return "numlock";
	case WXK_PAGEUP: return "pageup";
	case WXK_PAGEDOWN: return "pagedown";
	case WXK_NUMPAD_SPACE: return "num_space";
	case WXK_NUMPAD_TAB: return "num_tab";
	case WXK_NUMPAD_ENTER: return "num_enter";
	case WXK_NUMPAD_F1: return "num_f1";
	case WXK_NUMPAD_F2: return "num_f2";
	case WXK_NUMPAD_F3: return "num_f3";
	case WXK_NUMPAD_F4: return "num_f4";
	case WXK_NUMPAD_HOME: return "num_home";
	case WXK_NUMPAD_LEFT: return "num_left";
	case WXK_NUMPAD_UP: return "num_up";
	case WXK_NUMPAD_RIGHT: return "num_right";
	case WXK_NUMPAD_DOWN: return "num_down";
	case WXK_NUMPAD_PAGEUP: return "num_pageup";
	case WXK_NUMPAD_PAGEDOWN: return "num_pagedown";
	case WXK_NUMPAD_END: return "num_end";
	case WXK_NUMPAD_BEGIN: return "num_begin";
	case WXK_NUMPAD_INSERT: return "num_insert";
	case WXK_NUMPAD_DELETE: return "num_delete";
	case WXK_NUMPAD_EQUAL: return "num_equal";
	case WXK_NUMPAD_MULTIPLY: return "num_multiply";
	case WXK_NUMPAD_ADD: return "num_plus";
	case WXK_NUMPAD_SEPARATOR: return "num_separator";
	case WXK_NUMPAD_SUBTRACT: return "num_minus";
	case WXK_NUMPAD_DECIMAL: return "num_decimal";
	case WXK_NUMPAD_DIVIDE: return "num_divide";
	case WXK_WINDOWS_LEFT: return "win_left";
	case WXK_WINDOWS_RIGHT: return "win_right";
	case WXK_WINDOWS_MENU: return "win_menu";
	case WXK_PRINT: return "printscrn";
#ifdef __APPLE__
	case WXK_COMMAND: return "command";
#else
	case WXK_CONTROL: return "control";
#endif
	case '\\': return "backslash";
	default: break;
	};

	// Check for ascii character
	if (key > 32 && key < 128)
		return { (char)key };

	// Unknown character, just return "key##"
	return fmt::format("key{}", key);
}

// -----------------------------------------------------------------------------
// Returns a string representation of mouse [button]
// -----------------------------------------------------------------------------
string KeyBind::mbName(int button)
{
	switch (button)
	{
	case wxMOUSE_BTN_LEFT: return "mouse1";
	case wxMOUSE_BTN_RIGHT: return "mouse2";
	case wxMOUSE_BTN_MIDDLE: return "mouse3";
	case wxMOUSE_BTN_AUX1: return "mouse4";
	case wxMOUSE_BTN_AUX2: return "mouse5";
	default: return fmt::format("mouse{}", button);
	};
}

// -----------------------------------------------------------------------------
// 'Presses' all keybinds bound to [key]
// -----------------------------------------------------------------------------
bool KeyBind::keyPressed(Keypress key)
{
	// Ignore raw modifier keys
	if (key.key == "control" || key.key == "shift" || key.key == "alt" || key.key == "command")
		return false;

	// Go through all keybinds
	// (use sorted list for priority system)
	bool pressed = false;
	for (auto& kb : keybinds_sorted)
	{
		// Go through all keys bound to this keybind
		for (unsigned a = 0; a < kb.keys_.size(); a++)
		{
			auto& kp = kb.keys_[a];

			// Check for match with keypress
			if ((kp.shift == key.shift || kb.ignore_shift_) && kp.alt == key.alt && kp.ctrl == key.ctrl
				&& kp.key == key.key)
			{
				// Set bind state
				bind(kb.name_).pressed_ = true;

				// Send key pressed event to keybind handlers
				for (auto& kb_handler : kb_handlers)
					kb_handler->onKeyBindPress(kb.name_);

				pressed = true;
				break;
			}
		}
	}

	return pressed;
}

// -----------------------------------------------------------------------------
// 'Releases' all keybinds bound to [key]
// -----------------------------------------------------------------------------
bool KeyBind::keyReleased(string_view key)
{
	// Ignore raw modifier keys
	if (key == "control" || key == "shift" || key == "alt" || key == "command")
		return false;

	// Go through all keybinds
	bool released = false;
	for (auto& kb : keybinds)
	{
		// Go through all keys bound to this keybind
		for (unsigned a = 0; a < kb.keys_.size(); a++)
		{
			// Check for match with keypress, and that it is currently pressed
			if (kb.keys_[a].key == key && kb.pressed_)
			{
				// Set bind state
				kb.pressed_ = false;

				// Send key released event to keybind handlers
				for (auto& kb_handler : kb_handlers)
					kb_handler->onKeyBindRelease(kb.name_);

				released = true;
				break;
			}
		}
	}

	return released;
}

// -----------------------------------------------------------------------------
// 'Presses' the keybind [name]
// -----------------------------------------------------------------------------
void KeyBind::pressBind(string_view name)
{
	for (auto& keybind : keybinds)
	{
		if (keybind.name_ == name)
		{
			// Send key pressed event to keybind handlers
			for (auto& kb_handler : kb_handlers)
				kb_handler->onKeyBindPress(name);

			return;
		}
	}
}

// -----------------------------------------------------------------------------
// Returns [keycode] and [modifiers] as a Keypress struct
// -----------------------------------------------------------------------------
Keypress KeyBind::asKeyPress(int keycode, int modifiers)
{
	return Keypress(
		keyName(keycode),
		((modifiers & wxMOD_ALT) != 0),
		((modifiers & wxMOD_CMD) != 0),
		((modifiers & wxMOD_SHIFT) != 0));
}

// -----------------------------------------------------------------------------
// Adds all keybinds to [list]
// -----------------------------------------------------------------------------
void KeyBind::allKeyBinds(vector<KeyBind*>& list)
{
	for (auto& keybind : keybinds)
		list.push_back(&keybind);
}

// -----------------------------------------------------------------------------
// 'Releases' all keybinds
// -----------------------------------------------------------------------------
void KeyBind::releaseAll()
{
	for (auto& keybind : keybinds)
		keybind.pressed_ = false;
}

// -----------------------------------------------------------------------------
// Initialises all default keybinds
// -----------------------------------------------------------------------------
void KeyBind::initBinds()
{
	// General
	string group = "General";
	addBind("copy", Keypress("C", KPM_CTRL), "Copy", group);
	addBind("cut", Keypress("X", KPM_CTRL), "Cut", group);
	addBind("paste", Keypress("V", KPM_CTRL), "Paste", group);
	addBind("select_all", Keypress("A", KPM_CTRL), "Select All", group);

	// Entry List (el*)
	group = "Entry List";
	addBind("el_new", Keypress("N", KPM_CTRL), "New Entry", group);
	addBind("el_delete", Keypress("delete"), "Delete Entry", group);
	addBind("el_move_up", Keypress("U", KPM_CTRL), "Move Entry up", group);
	addBind("el_move_down", Keypress("D", KPM_CTRL), "Move Entry down", group);
	addBind("el_rename", Keypress("R", KPM_CTRL), "Rename Entry", group);
	addBind("el_rename", Keypress("f2"));
	addBind("el_import", Keypress("I", KPM_CTRL), "Import to Entry", group);
	addBind("el_import_files", Keypress("I", KPM_CTRL | KPM_SHIFT), "Import Files", group);
	addBind("el_import_dir", Keypress("I", KPM_CTRL | KPM_ALT), "Import Directory", group);
	addBind("el_export", Keypress("E", KPM_CTRL), "Export Entry", group);
	addBind("el_up_dir", Keypress("backspace"), "Up one directory", group);
	addBind("el_bookmark", Keypress("B", KPM_CTRL), "Toggle Bookmark", group);

	// Text editor (ted*)
	group = "Text Editor";
	addBind("ted_autocomplete", Keypress("space", KPM_CTRL), "Open Autocompletion list", group);
	addBind("ted_calltip", Keypress("space", KPM_CTRL | KPM_SHIFT), "Open CallTip", group);
	addBind("ted_findreplace", Keypress("F", KPM_CTRL), "Find/Replace", group);
	addBind("ted_findnext", Keypress("f3"), "Find next", group);
	addBind("ted_findprev", Keypress("f3", KPM_SHIFT), "Find previous", group);
	addBind("ted_replacenext", Keypress("R", KPM_ALT), "Replace next", group);
	addBind("ted_replaceall", Keypress("R", KPM_ALT | KPM_SHIFT), "Replace all", group);
	addBind("ted_jumptoline", Keypress("G", KPM_CTRL), "Jump to Line", group);
	addBind("ted_fold_foldall", Keypress("[", KPM_CTRL | KPM_SHIFT), "Fold All", group);
	addBind("ted_fold_unfoldall", Keypress("]", KPM_CTRL | KPM_SHIFT), "Fold All", group);
	addBind("ted_line_comment", Keypress("/", KPM_CTRL), "Line Comment", group);
	addBind("ted_block_comment", Keypress("/", KPM_CTRL | KPM_SHIFT), "Block Comment", group);
	addBind("ted_cycle_comments", Keypress("/", KPM_ALT), "Cycle Comments", group);

	// Texture editor (txed*)
	group = "Texture Editor";
	addBind("txed_patch_left", Keypress("left", KPM_CTRL), "Move Patch left", group);
	addBind("txed_patch_left8", Keypress("left"), "Move Patch left 8", group);
	addBind("txed_patch_up", Keypress("up", KPM_CTRL), "Move Patch up", group);
	addBind("txed_patch_up8", Keypress("up"), "Move Patch up 8", group);
	addBind("txed_patch_right", Keypress("right", KPM_CTRL), "Move Patch right", group);
	addBind("txed_patch_right8", Keypress("right"), "Move Patch right 8", group);
	addBind("txed_patch_down", Keypress("down", KPM_CTRL), "Move Patch down", group);
	addBind("txed_patch_down8", Keypress("down"), "Move Patch down 8", group);
	addBind("txed_patch_add", Keypress("insert"), "Add Patch", group);
	addBind("txed_patch_delete", Keypress("delete"), "Delete Patch", group);
	addBind("txed_patch_replace", Keypress("f2"), "Replace Patch", group);
	addBind("txed_patch_replace", Keypress("R", KPM_CTRL));
	addBind("txed_patch_duplicate", Keypress("D", KPM_CTRL), "Duplicate Patch", group);
	addBind("txed_patch_forward", Keypress("]"), "Bring Patch forward", group);
	addBind("txed_patch_back", Keypress("["), "Send Patch back", group);
	addBind("txed_tex_up", Keypress("up", KPM_CTRL), "Move Texture up", group);
	addBind("txed_tex_up", Keypress("U", KPM_CTRL));
	addBind("txed_tex_down", Keypress("down", KPM_CTRL), "Move Texture down", group);
	addBind("txed_tex_down", Keypress("D", KPM_CTRL));
	addBind("txed_tex_new", Keypress("N", KPM_CTRL), "New Texture", group);
	addBind("txed_tex_new_patch", Keypress("N", KPM_CTRL | KPM_SHIFT), "New Texture from Patch", group);
	addBind("txed_tex_new_file", Keypress("N", KPM_CTRL | KPM_ALT), "New Texture from File", group);
	addBind("txed_tex_delete", Keypress("delete"), "Delete Texture", group);

	// Set above keys as defaults
	for (auto& keybind : keybinds)
	{
		for (unsigned k = 0; k < keybind.keys_.size(); k++)
			keybind.defaults_.push_back(keybind.keys_[k]);
	}

	// Create sorted list
	keybinds_sorted = keybinds;
	std::sort(keybinds_sorted.begin(), keybinds_sorted.end());
}

// -----------------------------------------------------------------------------
// Writes all keybind definitions as a string
// -----------------------------------------------------------------------------
string KeyBind::writeBinds()
{
	// Init string
	string ret = "";

	// Go through all keybinds
	for (auto& kb : keybinds)
	{
		// Add keybind line
		ret += "\t";
		ret += kb.name_;

		// 'unbound' indicates no binds
		if (kb.keys_.empty())
			ret += " unbound";

		// Go through all bound keys
		for (unsigned a = 0; a < kb.keys_.size(); a++)
		{
			auto& kp = kb.keys_[a];
			ret += " \"";

			// Add modifiers (if any)
			if (kp.alt)
				ret += "a";
			if (kp.ctrl)
				ret += "c";
			if (kp.shift)
				ret += "s";
			if (kp.alt || kp.ctrl || kp.shift)
				ret += "|";

			// Add key
			ret += kp.key;
			ret += "\"";

			// Add comma if there are any more keys
			if (a < kb.keys_.size() - 1)
				ret += ",";
		}

		ret += "\n";
	}

	return ret;
}

// -----------------------------------------------------------------------------
// Reads keybind defeinitions from tokenizer [tz]
// -----------------------------------------------------------------------------
bool KeyBind::readBinds(Tokenizer& tz)
{
	// Parse until ending }
	while (!tz.checkOrEnd("}"))
	{
		// Ignore binds that no longer exist (eg. from removed features)
		auto name  = tz.current().text;
		bool known = false;
		for (const auto& kb : keybinds)
			if (kb.name_ == name)
			{
				known = true;
				break;
			}

		// Clear any current binds for the key
		if (known)
			bind(name).keys_.clear();

		// Read keys
		while (true)
		{
			auto keystr = tz.next().text;

			// Finish if no keys are bound
			if (keystr == "unbound")
				break;

			// Parse key string
			string key, mods;
			if (keystr.find('|') != string::npos)
			{
				mods = strutil::beforeFirst(keystr, '|');
				key  = strutil::afterFirst(keystr, '|');
			}
			else
				key = keystr;

			// Add the key
			if (known)
				addBind(
					name,
					Keypress(
						key,
						mods.find('a') != string::npos,
						mods.find('c') != string::npos,
						mods.find('s') != string::npos));

			// Check for more keys
			if (!tz.advIfNext(","))
				break;
		}

		// Next keybind
		tz.adv();
	}

	// Create sorted list
	keybinds_sorted = keybinds;
	std::sort(keybinds_sorted.begin(), keybinds_sorted.end());

	return true;
}

// -----------------------------------------------------------------------------
// Updates the sorted keybinds list
// -----------------------------------------------------------------------------
void KeyBind::updateSortedBindsList()
{
	// Create sorted list
	keybinds_sorted.clear();
	keybinds_sorted = keybinds;
	std::sort(keybinds_sorted.begin(), keybinds_sorted.end());
}


// -----------------------------------------------------------------------------
//
// KeyBindHandler Class Functions
//
// -----------------------------------------------------------------------------


// -----------------------------------------------------------------------------
// KeyBindHandler class constructor
// -----------------------------------------------------------------------------
KeyBindHandler::KeyBindHandler()
{
	kb_handlers.push_back(this);
}

// -----------------------------------------------------------------------------
// KeyBindHandler class destructor
// -----------------------------------------------------------------------------
KeyBindHandler::~KeyBindHandler()
{
	for (unsigned a = 0; a < kb_handlers.size(); a++)
	{
		if (kb_handlers[a] == this)
		{
			kb_handlers.erase(kb_handlers.begin() + a);
			a--;
		}
	}
}

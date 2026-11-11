#pragma once

class wxSingleInstanceChecker;
class MainAppFileListener;
class SLADE64CrashDialog;

class SLADE64WxApp : public wxApp
{
public:
	SLADE64WxApp()           = default;
	~SLADE64WxApp() override = default;

	bool OnInit() override;
	int  OnExit() override;
	void OnFatalException() override;

#ifdef __APPLE__
	virtual void MacOpenFile(const wxString& fileName) override;
#endif // __APPLE__

	bool singleInstanceCheck();

	void onMenu(wxCommandEvent& e);
	void onActivate(wxActivateEvent& event);

private:
	wxSingleInstanceChecker* single_instance_checker_ = nullptr;
	MainAppFileListener*     file_listener_           = nullptr;
	SLADE64CrashDialog*        crash_dialog_            = nullptr;
};

DECLARE_APP(SLADE64WxApp)

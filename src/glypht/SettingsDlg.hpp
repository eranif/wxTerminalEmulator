#pragma once

#include "app_persistence.h"
#include "terminal_theme.h"
#include "wxTerminalUI.hpp"

#include <functional>

class SettingsDlg : public SettingsBaseDlg {
public:
  /// Called with the theme the dialog shows and the key of the tab outline
  /// colour, each time the user picks another theme, changes a colour or picks
  /// another outline colour. Used to preview them while the dialog is open.
  using PreviewCallback =
      std::function<void(const wxTerminalTheme &, const wxString &)>;

  SettingsDlg(wxWindow *parent, const AppConfig &config,
              PreviewCallback onPreview = {});
  ~SettingsDlg() override;

  const wxTerminalTheme &GetTheme() const { return m_theme; }
  wxString GetThemeName() const { return m_choiceNames->GetStringSelection(); }
  wxString GetNewTabTitle() const { return m_textCtrlTitle->GetValue(); }
  bool GetShowCloseButton() const { return m_checkBoxCloseButton->GetValue(); }
  bool GetBlockCursor() const { return m_checkBoxBlockCursor->GetValue(); }
  /// The key of the tab outline colour (see AppConfig::GetTabOutlineColours()).
  wxString GetTabOutlineColour() const;
  wxFont GetFont() const { return m_fontPickerFont->GetSelectedFont(); }
  /// The shell executable chosen in the dialog, or empty for the default shell.
  wxString GetShellExecutable() const {
    wxString path = m_filePickerShellExe->GetPath();
    return path.Trim(true).Trim(false);
  }
  wxString GetShellArgs() const {
    wxString args = m_textCtrlShellArgs->GetValue();
    return args.Trim(true).Trim(false);
  }

  // True when "Optimise Drawings" is checked: the inverse of safe drawing.
  bool GetOptimisedPaint() const {
    return m_checkBoxOptimisedPaint->IsChecked();
  }

protected:
  void OnBackgroundColour(wxColourPickerEvent &event) override;
  void OnBlack(wxColourPickerEvent &event) override;
  void OnBlue(wxColourPickerEvent &event) override;
  void OnCyan(wxColourPickerEvent &event) override;
  void OnGreen(wxColourPickerEvent &event) override;
  void OnMagenta(wxColourPickerEvent &event) override;
  void OnOutlineColourChanged(wxCommandEvent &event) override;
  void OnThemeChanged(wxCommandEvent &event) override;
  void OnRed(wxColourPickerEvent &event) override;
  void OnShowBrightColours(wxCommandEvent &event) override;
  void OnTextColour(wxColourPickerEvent &event) override;
  void OnWhite(wxColourPickerEvent &event) override;
  void OnYellow(wxColourPickerEvent &event) override;

  void UpdateColours();
  // Send the current theme, with the font and cursor shape of the dialog, to
  // the preview callback.
  void Preview();

private:
  wxTerminalTheme m_theme;
  const AppConfig &m_config;
  bool m_normalColours{true};
  PreviewCallback m_onPreview;
};

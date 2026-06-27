#pragma once

#include "app_persistence.h"
#include "terminal_theme.h"
#include "wxTerminalUI.hpp"

class SettingsDlg : public SettingsBaseDlg {
public:
  SettingsDlg(wxWindow *parent, const AppConfig &config);
  ~SettingsDlg() override;

  const wxTerminalTheme &GetTheme() const { return m_theme; }
  wxString GetThemeName() const { return m_choiceNames->GetStringSelection(); }
  wxString GetNewTabTitle() const { return m_textCtrlTitle->GetValue(); }
  bool GetShowCloseButton() const { return m_checkBoxCloseButton->GetValue(); }
  bool GetBlockCursor() const { return m_checkBoxBlockCursor->GetValue(); }

protected:
  void OnBackgroundColour(wxColourPickerEvent &event) override;
  void OnBlack(wxColourPickerEvent &event) override;
  void OnBlue(wxColourPickerEvent &event) override;
  void OnCyan(wxColourPickerEvent &event) override;
  void OnGreen(wxColourPickerEvent &event) override;
  void OnMagenta(wxColourPickerEvent &event) override;
  void OnThemeChanged(wxCommandEvent &event) override;
  void OnRed(wxColourPickerEvent &event) override;
  void OnShowBrightColours(wxCommandEvent &event) override;
  void OnTextColour(wxColourPickerEvent &event) override;
  void OnWhite(wxColourPickerEvent &event) override;
  void OnYellow(wxColourPickerEvent &event) override;

  void UpdateColours();

private:
  wxTerminalTheme m_theme;
  const AppConfig &m_config;
  bool m_normalColours{true};
};

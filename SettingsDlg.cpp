#include "SettingsDlg.hpp"

SettingsDlg::SettingsDlg(wxWindow *parent, const AppConfig &config)
    : SettingsBaseDlg(parent), m_config{config},
      m_theme{config.GetThemeByName(config.GetThemeName())} {
  for (const wxString &name : m_config.GetThemeNames()) {
    m_choiceNames->Append(name);
  }
  m_choiceNames->SetStringSelection(m_config.GetThemeName());

  m_colourPickerBg->SetColour(m_theme.bg);
  m_colourPickerFg->SetColour(m_theme.fg);
  m_colourPickerBlack->SetColour(m_theme.black);
  m_colourPickerRed->SetColour(m_theme.red);
  m_colourPickerGreen->SetColour(m_theme.green);
  m_colourPickerYellow->SetColour(m_theme.yellow);
  m_colourPickerBlue->SetColour(m_theme.blue);
  m_colourPickerMagent->SetColour(m_theme.magenta);
  m_colourPickerCyan->SetColour(m_theme.cyan);
  m_colourPickerWhite->SetColour(m_theme.white);
}

SettingsDlg::~SettingsDlg() {}

void SettingsDlg::OnBackgroundColour(wxColourPickerEvent &event) {
  m_theme.bg = event.GetColour();
}
void SettingsDlg::OnTextColour(wxColourPickerEvent &event) {
  m_theme.fg = event.GetColour();
}

void SettingsDlg::OnBlack(wxColourPickerEvent &event) {
  if (m_normalColours) {
    m_theme.black = event.GetColour();
  } else {
    m_theme.brightBlack = event.GetColour();
  }
}

void SettingsDlg::OnBlue(wxColourPickerEvent &event) {
  if (m_normalColours) {
    m_theme.blue = event.GetColour();
  } else {
    m_theme.brightBlue = event.GetColour();
  }
}

void SettingsDlg::OnCyan(wxColourPickerEvent &event) {
  if (m_normalColours) {
    m_theme.cyan = event.GetColour();
  } else {
    m_theme.brightCyan = event.GetColour();
  }
}

void SettingsDlg::OnGreen(wxColourPickerEvent &event) {
  if (m_normalColours) {
    m_theme.green = event.GetColour();
  } else {
    m_theme.brightGreen = event.GetColour();
  }
}

void SettingsDlg::OnMagenta(wxColourPickerEvent &event) {
  if (m_normalColours) {
    m_theme.magenta = event.GetColour();
  } else {
    m_theme.brightMagenta = event.GetColour();
  }
}

void SettingsDlg::UpdateColours() {
  m_theme = m_config.GetThemeByName(m_choiceNames->GetStringSelection());
  m_colourPickerBg->SetColour(m_theme.bg);
  m_colourPickerFg->SetColour(m_theme.fg);
  m_colourPickerBlack->SetColour(m_normalColours ? m_theme.black
                                                 : m_theme.brightBlack);
  m_colourPickerRed->SetColour(m_normalColours ? m_theme.red
                                               : m_theme.brightRed);
  m_colourPickerGreen->SetColour(m_normalColours ? m_theme.green
                                                 : m_theme.brightGreen);
  m_colourPickerYellow->SetColour(m_normalColours ? m_theme.yellow
                                                  : m_theme.brightYellow);
  m_colourPickerBlue->SetColour(m_normalColours ? m_theme.blue
                                                : m_theme.brightBlue);
  m_colourPickerMagent->SetColour(m_normalColours ? m_theme.magenta
                                                  : m_theme.brightMagenta);
  m_colourPickerCyan->SetColour(m_normalColours ? m_theme.cyan
                                                : m_theme.brightCyan);
  m_colourPickerWhite->SetColour(m_normalColours ? m_theme.white
                                                 : m_theme.brightWhite);
}
void SettingsDlg::OnThemeChanged(wxCommandEvent &event) {
  wxUnusedVar(event);
  UpdateColours();
}

void SettingsDlg::OnRed(wxColourPickerEvent &event) {
  if (m_normalColours) {
    m_theme.red = event.GetColour();
  } else {
    m_theme.brightRed = event.GetColour();
  }
}

void SettingsDlg::OnWhite(wxColourPickerEvent &event) {
  if (m_normalColours) {
    m_theme.white = event.GetColour();
  } else {
    m_theme.brightWhite = event.GetColour();
  }
}

void SettingsDlg::OnYellow(wxColourPickerEvent &event) {
  if (m_normalColours) {
    m_theme.yellow = event.GetColour();
  } else {
    m_theme.brightYellow = event.GetColour();
  }
}
void SettingsDlg::OnShowBrightColours(wxCommandEvent &event) {
  m_normalColours = !event.IsChecked();
  UpdateColours();
}

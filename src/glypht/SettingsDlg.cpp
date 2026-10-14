#include "SettingsDlg.hpp"

SettingsDlg::SettingsDlg(wxWindow *parent, const AppConfig &config,
                         PreviewCallback onPreview)
    : SettingsBaseDlg(parent), m_config{config},
      m_theme{config.GetThemeByName(config.GetThemeName())},
      m_onPreview{std::move(onPreview)} {
  for (const wxString &name : m_config.GetThemeNames()) {
    m_choiceNames->Append(name);
  }
  m_choiceNames->SetStringSelection(m_config.GetThemeName());

  m_textCtrlTitle->SetValue(m_config.GetNewTabTitle());
  m_checkBoxCloseButton->SetValue(m_config.GetShowCloseButton());
  m_checkBoxBlockCursor->SetValue(m_config.GetBlockCursor());
  m_filePickerShellExe->SetPath(m_config.GetShellExecutable());
  m_textCtrlShellArgs->SetValue(m_config.GetShellArgs());
  if (m_config.GetFont().IsOk()) {
    m_fontPickerFont->SetSelectedFont(m_config.GetFont());
  }

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
  Preview();
}
void SettingsDlg::OnTextColour(wxColourPickerEvent &event) {
  m_theme.fg = event.GetColour();
  Preview();
}

void SettingsDlg::OnBlack(wxColourPickerEvent &event) {
  if (m_normalColours) {
    m_theme.black = event.GetColour();
  } else {
    m_theme.brightBlack = event.GetColour();
  }
  Preview();
}

void SettingsDlg::OnBlue(wxColourPickerEvent &event) {
  if (m_normalColours) {
    m_theme.blue = event.GetColour();
  } else {
    m_theme.brightBlue = event.GetColour();
  }
  Preview();
}

void SettingsDlg::OnCyan(wxColourPickerEvent &event) {
  if (m_normalColours) {
    m_theme.cyan = event.GetColour();
  } else {
    m_theme.brightCyan = event.GetColour();
  }
  Preview();
}

void SettingsDlg::OnGreen(wxColourPickerEvent &event) {
  if (m_normalColours) {
    m_theme.green = event.GetColour();
  } else {
    m_theme.brightGreen = event.GetColour();
  }
  Preview();
}

void SettingsDlg::OnMagenta(wxColourPickerEvent &event) {
  if (m_normalColours) {
    m_theme.magenta = event.GetColour();
  } else {
    m_theme.brightMagenta = event.GetColour();
  }
  Preview();
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
  Preview();
}

void SettingsDlg::Preview() {
  if (!m_onPreview) {
    return;
  }
  wxTerminalTheme theme = m_theme;
  theme.isBlockCursor = GetBlockCursor();
  const wxFont font = GetFont();
  if (font.IsOk()) {
    theme.font = font;
  }
  m_onPreview(theme);
}

void SettingsDlg::OnRed(wxColourPickerEvent &event) {
  if (m_normalColours) {
    m_theme.red = event.GetColour();
  } else {
    m_theme.brightRed = event.GetColour();
  }
  Preview();
}

void SettingsDlg::OnWhite(wxColourPickerEvent &event) {
  if (m_normalColours) {
    m_theme.white = event.GetColour();
  } else {
    m_theme.brightWhite = event.GetColour();
  }
  Preview();
}

void SettingsDlg::OnYellow(wxColourPickerEvent &event) {
  if (m_normalColours) {
    m_theme.yellow = event.GetColour();
  } else {
    m_theme.brightYellow = event.GetColour();
  }
  Preview();
}
void SettingsDlg::OnShowBrightColours(wxCommandEvent &event) {
  m_normalColours = !event.IsChecked();
  UpdateColours();
}

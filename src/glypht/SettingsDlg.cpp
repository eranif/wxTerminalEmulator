#include "SettingsDlg.hpp"
#include "terminal_view.h"
#include <algorithm>

SettingsDlg::SettingsDlg(wxWindow *parent, const AppConfig &config,
                         PreviewCallback onPreview)
    : SettingsBaseDlg(parent), m_config{config},
      m_theme{config.GetThemeByName(config.GetThemeName())},
      m_onPreview{std::move(onPreview)} {}

SettingsDlg::~SettingsDlg() {}

wxString SettingsDlg::GetTabOutlineColour() const {
  const auto &colours = AppConfig::GetTabOutlineColours();
  const int index = m_choiceOutlineColour->GetSelection();
  if (index < 0 || static_cast<size_t>(index) >= colours.size()) {
    return m_config.GetTabOutlineColour();
  }
  return colours[index].key;
}

TabPosition SettingsDlg::GetTabPosition() const {
  const int index =
      m_choiceTabPosition ? m_choiceTabPosition->GetSelection() : wxNOT_FOUND;
  if (index < 0 || static_cast<size_t>(index) >= m_tabPositions.size()) {
    return m_config.GetTabPosition();
  }
  return m_tabPositions[index];
}

TabStyle SettingsDlg::GetTabStyle() const {
  // The order of the items is set in OnDialogInit(): Browser, then Pills.
  return m_choiceTabStyle->GetSelection() == 1 ? TabStyle::Pills
                                               : TabStyle::Browser;
}

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
  m_onPreview(theme, GetTabOutlineColour());
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

void SettingsDlg::OnOutlineColourChanged(wxCommandEvent &event) {
  wxUnusedVar(event);
  Preview();
}

void SettingsDlg::OnDialogInit(wxInitDialogEvent &event) {
  wxUnusedVar(event);
  // The lists are filled once. A second init would add the items again, and
  // the tab position would then map to the wrong item.
  if (!m_choiceNames->IsEmpty()) {
    return;
  }
  // Fill the lists without repainting each item.
  wxWindowUpdateLocker locker{this};
  for (const wxString &name : m_config.GetThemeNames()) {
    m_choiceNames->Append(name);
  }
  m_choiceNames->SetStringSelection(m_config.GetThemeName());

  // "Optimise Drawings" is the inverse of the stored "safedrawing" setting.
  // The OpenGL renderer always draws cell by cell and ignores the setting, so
  // the checkbox is off and disabled there.
  m_checkBoxOptimisedPaint->SetToolTip(
      _("Draw runs of cells with the same style together. Faster, but less "
        "accurate for wide glyphs (CJK, emoji). Not used by the OpenGL "
        "renderer."));
  if (wxTerminalViewCtrl::IsOpenGLEnabled()) {
    m_checkBoxOptimisedPaint->SetValue(false);
    m_checkBoxOptimisedPaint->Disable();
  } else {
    m_checkBoxOptimisedPaint->SetValue(!m_config.IsSafeDrawingEnabled());
  }
  m_textCtrlTitle->SetValue(m_config.GetNewTabTitle());
  m_checkBoxCloseButton->SetValue(m_config.GetShowCloseButton());
  m_checkBoxBlockCursor->SetValue(m_config.GetBlockCursor());
  m_filePickerShellExe->SetPath(m_config.GetShellExecutable());
  m_textCtrlShellArgs->SetValue(m_config.GetShellArgs());
  // Without a saved font, show the default font of the theme, which the tabs
  // use.
  m_fontPickerFont->SetSelectedFont(
      m_config.GetFont().IsOk() ? m_config.GetFont() : m_theme.font);

  // An unknown key in config.ini shows the default, magenta.
  int outline = wxNOT_FOUND;
  int magenta = wxNOT_FOUND;
  for (const auto &entry : AppConfig::GetTabOutlineColours()) {
    const int index = m_choiceOutlineColour->Append(entry.label);
    if (m_config.GetTabOutlineColour() == entry.key) {
      outline = index;
    }
    if (wxString(entry.key) == "magenta") {
      magenta = index;
    }
  }
  m_choiceOutlineColour->SetSelection(outline != wxNOT_FOUND ? outline
                                                             : magenta);

  m_choiceTabPosition->Append(_("Top"));
  m_tabPositions.push_back(TabPosition::Top);
  m_choiceTabPosition->Append(_("Bottom"));
  m_tabPositions.push_back(TabPosition::Bottom);
  m_choiceTabPosition->Append(_("Left"));
  m_tabPositions.push_back(TabPosition::Left);
  m_choiceTabPosition->Append(_("Right"));
  m_tabPositions.push_back(TabPosition::Right);
  const auto it = std::find(m_tabPositions.begin(), m_tabPositions.end(),
                            m_config.GetTabPosition());
  m_choiceTabPosition->SetSelection(
      it != m_tabPositions.end() ? static_cast<int>(it - m_tabPositions.begin())
                                 : 0);

  // The items are Browser, then Pills (see GetTabStyle()).
  m_choiceTabStyle->SetSelection(m_config.GetTabStyle() == TabStyle::Pills ? 1
                                                                           : 0);

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

  CenterOnParent(); // Always centre.
}

void SettingsDlg::OnTabStyleUI(wxUpdateUIEvent &event) { event.Enable(true); }

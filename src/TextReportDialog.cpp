#include "TextReportDialog.h"

#include <wx/filedlg.h>
#include <wx/filename.h>
#include <wx/sizer.h>
#include <wx/statline.h>

namespace se
{

namespace
{
enum
{
    ID_REPORT_SAVE = wxID_HIGHEST + 500
};
}

TextReportDialog::TextReportDialog(wxWindow*       parent,
                                   const wxString& title,
                                   const wxString& text,
                                   bool            allowSave,
                                   const wxString& defaultFileName)
    : wxDialog(parent, wxID_ANY, title, wxDefaultPosition, wxSize(760, 620),
               wxDEFAULT_DIALOG_STYLE | wxRESIZE_BORDER)
    , m_defaultFileName(defaultFileName)
{
    wxBoxSizer* sizer = new wxBoxSizer(wxVERTICAL);

    m_text = new wxTextCtrl(this,
                            wxID_ANY,
                            text,
                            wxDefaultPosition,
                            wxDefaultSize,
                            wxTE_MULTILINE | wxTE_READONLY | wxTE_DONTWRAP | wxTE_RICH2);

    m_text->SetFont(wxFont(wxFontInfo(9).Family(wxFONTFAMILY_TELETYPE)));
    m_text->SetBackgroundColour(wxColour(252, 252, 250));

    sizer->Add(m_text, 1, wxEXPAND | wxALL, 8);

    wxSizer* buttons = CreateButtonSizer(wxOK | wxCANCEL);
    sizer->Add(buttons, 0, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, 8);

    if (allowSave)
    {
        wxButton* saveButton = new wxButton(this, ID_REPORT_SAVE, L"保存为文件...");
        sizer->Add(saveButton, 0, wxLEFT | wxRIGHT | wxBOTTOM, 8);

        Bind(wxEVT_BUTTON, &TextReportDialog::OnSave, this, ID_REPORT_SAVE);
    }

    SetSizer(sizer);
    Centre();
}

void TextReportDialog::OnSave(wxCommandEvent&)
{
    wxString name = m_defaultFileName;

    if (name.IsEmpty())
        name = L"netlist.net";

    wxFileDialog dialog(this,
                        L"保存为文件",
                        wxEmptyString,
                        name,
                        L"网络表文件 (*.net)|*.net|文本文件 (*.txt)|*.txt|所有文件 (*.*)|*.*",
                        wxFD_SAVE | wxFD_OVERWRITE_PROMPT);

    if (dialog.ShowModal() != wxID_OK)
        return;

    if (m_text->SaveFile(dialog.GetPath()))
    {
        wxMessageBox(wxString::Format(L"已保存到：\n%s", dialog.GetPath()),
                     L"保存成功", wxOK | wxICON_INFORMATION, this);
    }
    else
    {
        wxMessageBox(L"写入文件失败。", L"保存失败", wxOK | wxICON_ERROR, this);
    }
}

} // namespace se

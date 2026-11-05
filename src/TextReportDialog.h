// ============================================================
//  TextReportDialog.h  —— 通用“文本报告”对话框
//
//  用来显示网表、ERC 报告这类纯文本结果，
//  可选带一个“保存为文件...”按钮（任务 5 的网表导出入口）。
// ============================================================

#pragma once

#include <wx/wx.h>

namespace se
{

class TextReportDialog : public wxDialog
{
public:
    TextReportDialog(wxWindow*       parent,
                     const wxString& title,
                     const wxString& text,
                     bool            allowSave       = true,
                     const wxString& defaultFileName = wxEmptyString);

private:
    void OnSave(wxCommandEvent& event);

    wxTextCtrl* m_text      = nullptr;
    wxString    m_defaultFileName;
};

} // namespace se

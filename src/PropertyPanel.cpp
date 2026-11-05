#include "PropertyPanel.h"

#include <wx/propgrid/advprops.h>
#include <wx/statline.h>

#include "Ids.h"

namespace se
{

PropertyPanel::PropertyPanel(wxWindow* parent)
    : wxPanel(parent, wxID_ANY)
{
    SetBackgroundColour(wxColour(248, 249, 251));

    // ---- 标题 ----
    m_header = new wxStaticText(this, wxID_ANY, L"属性表");
    wxFont titleFont = m_header->GetFont();
    titleFont.SetPointSize(titleFont.GetPointSize() + 1);
    titleFont.SetWeight(wxFONTWEIGHT_BOLD);
    m_header->SetFont(titleFont);
    m_header->SetForegroundColour(wxColour(40, 60, 100));

    wxStaticLine* line = new wxStaticLine(this);

    // ---- 属性网格 ----
    m_grid = new wxPropertyGrid(this,
                                ID_PROPERTY_GRID,
                                wxDefaultPosition,
                                wxDefaultSize,
                                wxPG_DEFAULT_STYLE | wxBORDER_NONE);

    m_grid->SetBackgroundColour(wxColour(255, 255, 255));
    m_grid->SetSplitterPosition(96);

    // ---- 布局 ----
    wxBoxSizer* sizer = new wxBoxSizer(wxVERTICAL);
    sizer->Add(m_header, 0, wxLEFT | wxRIGHT | wxTOP | wxBOTTOM, 8);
    sizer->Add(line,    0, wxEXPAND);
    sizer->Add(m_grid,  1, wxEXPAND);

    SetSizer(sizer);

    SetMinSize(wxSize(180, 200));

    ShowPlaceholder(L"未选中任何对象");
}

// ------------------------------------------------------------
void PropertyPanel::Begin(const wxString& title)
{
    m_header->SetLabel(title);
    m_grid->Clear();
    m_seq = 0;
}

// ------------------------------------------------------------
void PropertyPanel::AddCategory(const wxString& name)
{
    // 第二个参数是内部名称，必须唯一，用自动编号保证
    m_grid->Append(new wxPropertyCategory(name, wxString::Format(L"@cat%d", ++m_seq)));
}

// ------------------------------------------------------------
void PropertyPanel::AddRow(const wxString& label, const wxString& value)
{
    wxPGProperty* property = m_grid->Append(
        new wxStringProperty(label, wxString::Format(L"@row%d", ++m_seq), value));

    // 当前阶段属性表只做展示，不允许编辑
    property->ChangeFlag(wxPG_PROP_READONLY, true);
}

// ------------------------------------------------------------
void PropertyPanel::AddIntRow(const wxString& label, int value)
{
    wxPGProperty* property = m_grid->Append(
        new wxIntProperty(label, wxString::Format(L"@row%d", ++m_seq), value));

    property->ChangeFlag(wxPG_PROP_READONLY, true);
}

// ------------------------------------------------------------
void PropertyPanel::End()
{
    m_grid->Refresh();
}

// ------------------------------------------------------------
void PropertyPanel::ShowPlaceholder(const wxString& message)
{
    Begin(L"属性表");
    AddRow(L"选中对象", message);
    End();
}

} // namespace se

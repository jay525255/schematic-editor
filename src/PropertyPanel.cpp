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

    // wxPropertyGrid 在尺寸变化时会把分栏位置按比例重算，光设一次不够，
    // 每次改变大小都重新按 58% 摆一次（宽度没变就跳过，避免自激）。
    //  注意：不要用 SetColumnProportion()——它要求网格带
    //  wxPG_SPLITTER_AUTO_CENTER 样式，否则 Debug 版会弹断言，
    //  SetSplitterPosition() 才是安全的写法。
    m_grid->Bind(wxEVT_SIZE, [this](wxSizeEvent& event)
    {
        event.Skip();

        const int width = m_grid->GetClientSize().x;

        if (width > 200 && width != m_lastSplitWidth)
        {
            m_lastSplitWidth = width;
            m_grid->SetSplitterPosition((width * 58) / 100);
        }
    });

    // 用户在网格里改了值 → 交给 OnEdit 回调处理
    m_grid->Bind(wxEVT_PG_CHANGED, &PropertyPanel::OnGridChanged, this);

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
    m_filling = true;
    m_header->SetLabel(title);
    m_grid->Clear();
    m_keyOf.clear();
    m_seq = 0;
    m_filling = false;
}

// ------------------------------------------------------------
void PropertyPanel::AddCategory(const wxString& name)
{
    // 第二个参数是内部名称，必须唯一，用自动编号保证
    m_grid->Append(new wxPropertyCategory(name, wxString::Format(L"@cat%d", ++m_seq)));
}

// ------------------------------------------------------------
wxPGProperty* PropertyPanel::NewRow(const wxString& label,
                                    const wxString& key,
                                    wxPGProperty* property,
                                    bool editable)
{
    if (!editable)
        property->ChangeFlag(wxPG_PROP_READONLY, true);

    m_grid->Append(property);

    if (editable && !key.IsEmpty())
    {
        m_keyOf[property->GetName()] = key;

        // 可编辑的行给一点暖色底，一眼能看出“这一行能改”；
        // 只读行保持纯白，两种行不用点开也分得清。
        m_grid->SetPropertyBackgroundColour(property->GetName(), wxColour(255, 250, 232));
    }

    return property;
}

// ------------------------------------------------------------
void PropertyPanel::AddRow(const wxString& label, const wxString& value)
{
    NewRow(label,
           wxEmptyString,
           new wxStringProperty(label, wxString::Format(L"@row%d", ++m_seq), value),
           false);
}

// ------------------------------------------------------------
void PropertyPanel::AddIntRow(const wxString& label, int value)
{
    NewRow(label,
           wxEmptyString,
           new wxIntProperty(label, wxString::Format(L"@row%d", ++m_seq), value),
           false);
}

// ------------------------------------------------------------
void PropertyPanel::AddEditRow(const wxString& label, const wxString& value,
                               const wxString& key)
{
    NewRow(label,
           key,
           new wxStringProperty(label, wxString::Format(L"@row%d", ++m_seq), value),
           true);
}

// ------------------------------------------------------------
void PropertyPanel::AddEditIntRow(const wxString& label, int value, const wxString& key)
{
    NewRow(label,
           key,
           new wxIntProperty(label, wxString::Format(L"@row%d", ++m_seq), value),
           true);
}

// ------------------------------------------------------------
void PropertyPanel::AddEditChoiceRow(const wxString& label,
                                     const wxArrayString& choices,
                                     const wxString& value,
                                     const wxString& key)
{
    wxPGProperty* property = NewRow(
        label,
        key,
        new wxEnumProperty(label, wxString::Format(L"@row%d", ++m_seq), choices),
        true);

    // 当前值不在候选里时（用户手改过的封装等），临时塞进去一个，
    // 否则 wxPropertyGrid 会把它显示成第一项，看起来像"被改了"
    if (choices.Index(value) == wxNOT_FOUND && !value.IsEmpty())
        property->SetValueFromString(value);

    property->SetValueFromString(value);
}

// ------------------------------------------------------------
void PropertyPanel::OnGridChanged(wxPropertyGridEvent& event)
{
    if (m_filling)
        return;

    wxPGProperty* property = event.GetProperty();

    if (!property)
        return;

    const auto it = m_keyOf.find(property->GetName());

    if (it == m_keyOf.end())
        return;   // 不是可编辑行，忽略

    if (OnEdit)
        OnEdit(it->second, property->GetValue());
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

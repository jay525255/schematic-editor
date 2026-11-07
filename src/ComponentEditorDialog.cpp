#include "ComponentEditorDialog.h"

#include <wx/spinctrl.h>
#include <wx/statline.h>

#include <algorithm>

namespace se
{

namespace
{

const wxString kUserCategoryName = L"用户自定义";

} // namespace

// ============================================================
ComponentEditorDialog::ComponentEditorDialog(wxWindow* parent)
    : wxDialog(parent, wxID_ANY, L"元件库管理（用户自定义元件）",
               wxDefaultPosition, wxDefaultSize,
               wxDEFAULT_DIALOG_STYLE | wxRESIZE_BORDER)
{
    // 复制一份用户库：所有修改先在副本上做，点“保存”才落盘
    m_categories = ComponentCatalog::UserCategories();

    // 找到（或新建）正在编辑的分类
    for (size_t c = 0; c < m_categories.size(); ++c)
    {
        if (m_categories[c].name == kUserCategoryName)
        {
            m_cat = static_cast<int>(c);
            break;
        }
    }

    if (m_cat < 0)
    {
        CategoryDef created;
        created.name = kUserCategoryName;

        m_categories.push_back(created);
        m_cat = static_cast<int>(m_categories.size()) - 1;
    }

    BuildUi();

    RebuildList(m_categories[m_cat].items.empty() ? -1 : 0);

    SetMinSize(wxSize(720, 430));
    Fit();

    CentreOnParent();
}

// ------------------------------------------------------------
void ComponentEditorDialog::BuildUi()
{
    wxBoxSizer* outer = new wxBoxSizer(wxVERTICAL);

    // ---- 说明 ----
    wxStaticText* where = new wxStaticText(
        this, wxID_ANY,
        wxString::Format(L"元件库文件：%s", ComponentCatalog::GetLibraryPath()));

    where->SetForegroundColour(wxColour(110, 120, 140));

    m_hint = new wxStaticText(
        this, wxID_ANY,
        L"提示：名称与内置元件相同 = 覆盖内置元件的字段（例如填“电阻”把默认值改成 1k）；"
        L"新增的名称会出现在元件库树末尾，带 ★ 标记。");

    m_hint->SetForegroundColour(wxColour(140, 100, 30));
    m_hint->Wrap(660);

    outer->Add(where,  0, wxLEFT | wxRIGHT | wxTOP, 12);
    outer->Add(m_hint, 0, wxLEFT | wxRIGHT | wxTOP, 12);
    outer->Add(new wxStaticLine(this), 0, wxEXPAND | wxALL, 12);

    // ---- 左右两栏 ----
    wxBoxSizer* body = new wxBoxSizer(wxHORIZONTAL);

    // 左：元件列表 + 新建/删除
    wxBoxSizer* left = new wxBoxSizer(wxVERTICAL);

    left->Add(new wxStaticText(this, wxID_ANY, L"自定义元件"), 0, wxBOTTOM, 4);

    m_list = new wxListBox(this, wxID_ANY, wxDefaultPosition, wxSize(210, 240));
    left->Add(m_list, 1, wxEXPAND);

    wxBoxSizer* listButtons = new wxBoxSizer(wxHORIZONTAL);

    wxButton* addButton = new wxButton(this, wxID_ANY, L"新建");
    wxButton* delButton = new wxButton(this, wxID_ANY, L"删除");

    listButtons->Add(addButton, 0, wxRIGHT, 6);
    listButtons->Add(delButton, 0);
    left->Add(listButtons, 0, wxTOP, 8);

    body->Add(left, 0, wxRIGHT, 16);

    // 右：字段
    wxFlexGridSizer* grid = new wxFlexGridSizer(2, 8, 10);
    grid->AddGrowableCol(1, 1);

    auto label = [this](const wxString& text)
    {
        return new wxStaticText(this, wxID_ANY, text);
    };

    m_name = new wxTextCtrl(this, wxID_ANY);
    grid->Add(label(L"名称"), 0, wxALIGN_CENTER_VERTICAL);
    grid->Add(m_name, 1, wxEXPAND);

    m_refPrefix = new wxTextCtrl(this, wxID_ANY);
    grid->Add(label(L"位号前缀"), 0, wxALIGN_CENTER_VERTICAL);
    grid->Add(m_refPrefix, 1, wxEXPAND);

    m_desc = new wxTextCtrl(this, wxID_ANY);
    grid->Add(label(L"说明"), 0, wxALIGN_CENTER_VERTICAL);
    grid->Add(m_desc, 1, wxEXPAND);

    m_inputs = new wxSpinCtrl(this, wxID_ANY, wxEmptyString, wxDefaultPosition,
                              wxDefaultSize, wxSP_ARROW_KEYS, 0, 32, 0);
    grid->Add(label(L"输入引脚数"), 0, wxALIGN_CENTER_VERTICAL);
    grid->Add(m_inputs, 1, wxEXPAND);

    m_outputs = new wxSpinCtrl(this, wxID_ANY, wxEmptyString, wxDefaultPosition,
                               wxDefaultSize, wxSP_ARROW_KEYS, 0, 32, 1);
    grid->Add(label(L"输出引脚数"), 0, wxALIGN_CENTER_VERTICAL);
    grid->Add(m_outputs, 1, wxEXPAND);

    wxArrayString kinds;
    kinds.Add(L"逻辑器件（左入右出）");
    kinds.Add(L"无源器件（左右两端）");
    kinds.Add(L"电源/地（底部单脚）");
    kinds.Add(L"端口/标签（单脚）");

    m_kind = new wxChoice(this, wxID_ANY, wxDefaultPosition, wxDefaultSize, kinds);
    m_kind->SetSelection(0);
    grid->Add(label(L"电气形态"), 0, wxALIGN_CENTER_VERTICAL);
    grid->Add(m_kind, 1, wxEXPAND);

    wxBoxSizer* sizeRow = new wxBoxSizer(wxHORIZONTAL);

    m_width = new wxSpinCtrl(this, wxID_ANY, wxEmptyString, wxDefaultPosition,
                             wxSize(90, -1), wxSP_ARROW_KEYS, 24, 400, 60);
    m_height = new wxSpinCtrl(this, wxID_ANY, wxEmptyString, wxDefaultPosition,
                              wxSize(90, -1), wxSP_ARROW_KEYS, 20, 400, 40);

    sizeRow->Add(m_width,  0, wxRIGHT, 6);
    sizeRow->Add(new wxStaticText(this, wxID_ANY, L"×"), 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 6);
    sizeRow->Add(m_height, 0);

    grid->Add(label(L"元件框宽 × 高"), 0, wxALIGN_CENTER_VERTICAL);
    grid->Add(sizeRow, 1, wxEXPAND);

    m_footprint = new wxTextCtrl(this, wxID_ANY);
    grid->Add(label(L"默认封装"), 0, wxALIGN_CENTER_VERTICAL);
    grid->Add(m_footprint, 1, wxEXPAND);

    m_value = new wxTextCtrl(this, wxID_ANY);
    grid->Add(label(L"元件值/型号"), 0, wxALIGN_CENTER_VERTICAL);
    grid->Add(m_value, 1, wxEXPAND);

    body->Add(grid, 1, wxEXPAND);

    outer->Add(body, 1, wxEXPAND | wxLEFT | wxRIGHT, 12);

    // ---- 底部按钮 ----
    wxBoxSizer* buttons = new wxBoxSizer(wxHORIZONTAL);

    wxButton* saveButton = new wxButton(this, wxID_ANY, L"保存并应用");
    wxButton* cancelButton = new wxButton(this, wxID_CANCEL, L"取消");

    buttons->AddStretchSpacer();
    buttons->Add(saveButton, 0, wxRIGHT, 8);
    buttons->Add(cancelButton, 0);

    outer->Add(buttons, 0, wxEXPAND | wxALL, 12);

    SetSizer(outer);

    // ---- 事件 ----
    m_list->Bind(wxEVT_LISTBOX, &ComponentEditorDialog::OnSelect, this);
    addButton->Bind(wxEVT_BUTTON, &ComponentEditorDialog::OnNew,    this);
    delButton->Bind(wxEVT_BUTTON, &ComponentEditorDialog::OnDelete, this);
    saveButton->Bind(wxEVT_BUTTON, &ComponentEditorDialog::OnSave,  this);

    // 字段一改就顺手写回模型，避免“切走了才发现没保存”
    for (wxTextCtrl* ctrl : { m_name, m_refPrefix, m_desc, m_footprint, m_value })
        ctrl->Bind(wxEVT_TEXT, [this](wxCommandEvent& e) { FlushControls(); e.Skip(); });

    for (wxSpinCtrl* ctrl : { m_inputs, m_outputs, m_width, m_height })
        ctrl->Bind(wxEVT_SPINCTRL, [this](wxCommandEvent& e) { FlushControls(); e.Skip(); });

    m_kind->Bind(wxEVT_CHOICE, [this](wxCommandEvent& e) { FlushControls(); e.Skip(); });
}

// ------------------------------------------------------------
void ComponentEditorDialog::RebuildList(int selectIndex)
{
    if (m_cat < 0 || m_cat >= static_cast<int>(m_categories.size()))
        return;

    m_list->Clear();

    const std::vector<ComponentDef>& items = m_categories[m_cat].items;

    for (const ComponentDef& item : items)
        m_list->Append(item.name);

    if (!items.empty())
    {
        const int index = std::min(std::max(0, selectIndex),
                                   static_cast<int>(items.size()) - 1);

        m_list->SetSelection(index);
        m_item = index;
    }
    else
    {
        m_item = -1;
    }

    LoadToControls();
}

// ------------------------------------------------------------
void ComponentEditorDialog::LoadToControls()
{
    m_loading = true;

    if (m_cat < 0 || m_item < 0 ||
        m_cat >= static_cast<int>(m_categories.size()) ||
        m_item >= static_cast<int>(m_categories[m_cat].items.size()))
    {
        for (wxTextCtrl* ctrl : { m_name, m_refPrefix, m_desc, m_footprint, m_value })
            ctrl->ChangeValue(wxEmptyString);

        m_inputs->SetValue(0);
        m_outputs->SetValue(1);
        m_width->SetValue(60);
        m_height->SetValue(40);
        m_kind->SetSelection(0);

        m_name->Enable(false);
        m_refPrefix->Enable(false);
        m_desc->Enable(false);
        m_footprint->Enable(false);
        m_value->Enable(false);
        m_inputs->Enable(false);
        m_outputs->Enable(false);
        m_width->Enable(false);
        m_height->Enable(false);
        m_kind->Enable(false);

        m_loading = false;
        return;
    }

    const ComponentDef& def = m_categories[m_cat].items[m_item];

    m_name->ChangeValue(def.name);
    m_refPrefix->ChangeValue(def.refPrefix);
    m_desc->ChangeValue(def.description);
    m_footprint->ChangeValue(def.footprint);
    m_value->ChangeValue(def.value);
    m_inputs->SetValue(def.inputs);
    m_outputs->SetValue(def.outputs);
    m_width->SetValue(def.w);
    m_height->SetValue(def.h);

    switch (def.kind)
    {
        case ComponentKind::Passive: m_kind->SetSelection(1); break;
        case ComponentKind::Power:   m_kind->SetSelection(2); break;
        case ComponentKind::Port:    m_kind->SetSelection(3); break;
        case ComponentKind::Logic:
        default:                     m_kind->SetSelection(0); break;
    }

    for (wxWindow* ctrl : { static_cast<wxWindow*>(m_name),
                            static_cast<wxWindow*>(m_refPrefix),
                            static_cast<wxWindow*>(m_desc),
                            static_cast<wxWindow*>(m_footprint),
                            static_cast<wxWindow*>(m_value),
                            static_cast<wxWindow*>(m_inputs),
                            static_cast<wxWindow*>(m_outputs),
                            static_cast<wxWindow*>(m_width),
                            static_cast<wxWindow*>(m_height),
                            static_cast<wxWindow*>(m_kind) })
        ctrl->Enable(true);

    m_loading = false;
}

// ------------------------------------------------------------
void ComponentEditorDialog::FlushControls()
{
    if (m_loading || m_cat < 0 || m_item < 0)
        return;

    if (m_cat >= static_cast<int>(m_categories.size()) ||
        m_item >= static_cast<int>(m_categories[m_cat].items.size()))
        return;

    ComponentDef& def = m_categories[m_cat].items[m_item];

    def.name        = m_name->GetValue().Trim();
    def.refPrefix   = m_refPrefix->GetValue().Trim();
    def.description = m_desc->GetValue();
    def.inputs      = m_inputs->GetValue();
    def.outputs     = m_outputs->GetValue();
    def.w           = m_width->GetValue();
    def.h           = m_height->GetValue();
    def.footprint   = m_footprint->GetValue().Trim();
    def.value       = m_value->GetValue().Trim();
    def.userDefined = true;

    switch (m_kind->GetSelection())
    {
        case 1:  def.kind = ComponentKind::Passive; break;
        case 2:  def.kind = ComponentKind::Power;   break;
        case 3:  def.kind = ComponentKind::Port;    break;
        default: def.kind = ComponentKind::Logic;   break;
    }
}

// ------------------------------------------------------------
void ComponentEditorDialog::OnSelect(wxCommandEvent& event)
{
    FlushControls();   // 先把上一个元件的编辑保住

    const int index = m_list->GetSelection();

    if (index >= 0 && m_cat >= 0 &&
        index < static_cast<int>(m_categories[m_cat].items.size()))
    {
        m_item = index;
        LoadToControls();
    }

    event.Skip();
}

// ------------------------------------------------------------
void ComponentEditorDialog::OnNew(wxCommandEvent&)
{
    FlushControls();

    if (m_cat < 0)
        return;

    ComponentDef def;
    def.name        = wxString::Format(L"新元件%d",
                                       static_cast<int>(m_categories[m_cat].items.size()) + 1);
    def.refPrefix   = L"U";
    def.description = L"用户自定义元件";
    def.inputs      = 2;
    def.outputs     = 1;
    def.kind        = ComponentKind::Logic;
    def.w           = 60;
    def.h           = 40;
    def.userDefined = true;

    m_categories[m_cat].items.push_back(def);

    RebuildList(static_cast<int>(m_categories[m_cat].items.size()) - 1);

    m_name->SetFocus();
    m_name->SelectAll();
}

// ------------------------------------------------------------
void ComponentEditorDialog::OnDelete(wxCommandEvent&)
{
    if (m_cat < 0 || m_item < 0)
        return;

    if (m_item >= static_cast<int>(m_categories[m_cat].items.size()))
        return;

    const wxString name = m_categories[m_cat].items[m_item].name;

    if (wxMessageBox(wxString::Format(L"确定删除自定义元件「%s」？", name),
                     L"删除元件", wxYES_NO | wxICON_QUESTION, this) != wxYES)
        return;

    m_categories[m_cat].items.erase(m_categories[m_cat].items.begin() + m_item);

    RebuildList(m_item);
}

// ------------------------------------------------------------
void ComponentEditorDialog::OnSave(wxCommandEvent&)
{
    FlushControls();

    // 名称是查找用的键，必须非空
    for (const CategoryDef& category : m_categories)
    {
        for (const ComponentDef& def : category.items)
        {
            wxString validationError;
            if (!ComponentCatalog::ValidateDefinition(def, &validationError)) {
                wxMessageBox(def.name + L"：" + validationError, L"元件定义无效", wxOK | wxICON_WARNING, this);
                return;
            }

            if (def.name.IsEmpty())
            {
                wxMessageBox(L"有元件的名称为空，请先补上名称。",
                             L"元件库管理", wxOK | wxICON_WARNING, this);
                return;
            }
        }
    }

    wxString error;

    if (!ComponentCatalog::SaveUserLibrary(m_categories, &error))
    {
        wxMessageBox(error, L"保存失败", wxOK | wxICON_ERROR, this);
        return;
    }

    wxString reloadError;

    if (!ComponentCatalog::Reload(&reloadError))
    {
        wxMessageBox(reloadError, L"元件库重新加载失败", wxOK | wxICON_ERROR, this);
        return;
    }

    EndModal(wxID_OK);
}

} // namespace se

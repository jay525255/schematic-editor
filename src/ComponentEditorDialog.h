// ============================================================
//  ComponentEditorDialog.h  —— 元件库管理对话框（任务 3 · 用户自定义元件）
//
//  做的事：
//    * 列出当前用户自定义元件，可以新建 / 删除 / 改字段；
//    * 「保存」把整份用户库写回 components.xml，然后由 MainFrame 触发票
//      元件库重新加载（元件库面板与已放置元件的定义都会跟着更新）；
//    * 名称与内置元件相同时 = 覆盖内置元件的字段（比如把电阻默认值改成 1k）。
// ============================================================

#pragma once

#include <wx/wx.h>
#include <wx/listbox.h>

#include <vector>

#include "ComponentCatalog.h"

namespace se
{

class ComponentEditorDialog : public wxDialog
{
public:
    explicit ComponentEditorDialog(wxWindow* parent);

private:
    void BuildUi();
    void RebuildList(int selectIndex);
    void LoadToControls();
    void FlushControls();

    void OnSelect(wxCommandEvent& event);
    void OnNew(wxCommandEvent& event);
    void OnDelete(wxCommandEvent& event);
    void OnSave(wxCommandEvent& event);

    // 用户库的完整副本（含非“用户自定义”分类，保存时原样写回）
    std::vector<CategoryDef> m_categories;
    int  m_cat      = -1;      // 正在编辑的分类下标
    int  m_item     = -1;      // 正在编辑的元件下标
    bool m_loading  = false;   // 回填控件期间不要反写模型

    wxListBox*    m_list      = nullptr;
    wxTextCtrl*   m_name      = nullptr;
    wxTextCtrl*   m_refPrefix = nullptr;
    wxTextCtrl*   m_desc      = nullptr;
    wxTextCtrl*   m_footprint = nullptr;
    wxTextCtrl*   m_value     = nullptr;
    wxSpinCtrl*   m_inputs    = nullptr;
    wxSpinCtrl*   m_outputs   = nullptr;
    wxSpinCtrl*   m_width     = nullptr;
    wxSpinCtrl*   m_height    = nullptr;
    wxChoice*     m_kind      = nullptr;
    wxStaticText* m_hint      = nullptr;
};

} // namespace se

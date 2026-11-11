// ============================================================
//  LibraryPanel.h  —— 元件库面板（左侧分区）
//
//  用 wxTreeCtrl 展示「分类 → 元件」两级树形结构，
//  选中元件后通过回调通知 MainFrame 更新绘图区待放置元件与属性表。
//
//  任务 3 补完后具备：
//    * 顶部搜索框（按名称 / 说明 / 型号过滤，实时筛选）
//    * 每个元件的图形符号缩略图（树里是随形态变色的符号图标，
//      底部是选中元件的大号符号预览）
//    * 用户自定义元件（数据来自 components.xml，见 ComponentCatalog）
// ============================================================

#pragma once

#include <wx/wx.h>
#include <wx/srchctrl.h>
#include <wx/treectrl.h>

#include <functional>
#include <map>

#include "ComponentCatalog.h"

namespace se
{

class SymbolPreview;   // 实现在 LibraryPanel.cpp

class LibraryPanel : public wxPanel
{
public:
    explicit LibraryPanel(wxWindow* parent);

    // 选中元件变化时的回调（传 nullptr 表示取消选中）
    void SetOnSelect(std::function<void(const ComponentDef*)> callback)
    {
        m_onSelect = std::move(callback);
    }

    // 当前选中的元件，未选中返回 nullptr
    const ComponentDef* GetSelected() const { return m_selected; }

    void ClearSelection();

    // 元件库数据变了（重新加载 XML / 管理对话框保存后）→ 重建树与预览
    void Reload();

    // 当前树里显示了多少个元件（过滤后）
    int VisibleComponentCount() const { return m_visible; }

private:
    void BuildTree();
    void ApplyFilter();
    void OnSelectionChanged(wxTreeEvent& event);
    void OnItemActivated(wxTreeEvent& event);
    void OnSearchChanged(wxCommandEvent& event);
    void OnSearchCancel(wxCommandEvent& event);
    void UpdatePreview();

    // 图标：按“形态 + 是否自定义”缓存
    int IconFor(const ComponentDef& def);

    wxSearchCtrl*     m_search   = nullptr;
    wxTreeCtrl*       m_tree     = nullptr;
    SymbolPreview*    m_preview  = nullptr;
    wxImageList*      m_images   = nullptr;

    std::map<wxString, int> m_iconCache;
    int                     m_visible = 0;
    bool                    m_rebuilding = false;

    const ComponentDef* m_selected = nullptr;

    std::function<void(const ComponentDef*)> m_onSelect;
};

} // namespace se

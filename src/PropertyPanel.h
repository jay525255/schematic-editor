// ============================================================
//  PropertyPanel.h  —— 属性表面板（右侧分区）
//
//  用 wxPropertyGrid 显示当前选中对象（画布上的元件实例 /
//  元件库条目）的属性，并固定附带一段画布视图信息。
//
//  面板本身只负责“显示 + 把用户的修改报告出去”，要显示什么、
//  改完怎么落到模型上，都由 MainFrame / SchematicCanvas 决定，
//  这样面板不需要认识元件、网络这些概念。
// ============================================================

#pragma once

#include <wx/wx.h>
#include <wx/propgrid/propgrid.h>
#include <functional>
#include <map>

namespace se
{

class PropertyPanel : public wxPanel
{
public:
    // 用户改完某一行的回调：key 是“语义键”（如 comp.refdes），
    // value 是 wxPropertyGrid 给出的新值。面板不认识模型，改动一律交出去。
    using EditHandler = std::function<void(const wxString& key, const wxVariant& value)>;

    explicit PropertyPanel(wxWindow* parent);

    // 开始一次填充：设置标题并清空内容
    void Begin(const wxString& title);

    // 分组标题（属性表里的可折叠分类行）
    void AddCategory(const wxString& name);

    // 只读属性行
    void AddRow(const wxString& label, const wxString& value);
    void AddIntRow(const wxString& label, int value);

    // ---- 可编辑属性行 ----
    // key 由调用方定义，回调里原样送回；面板只负责“改了就报告”。
    void AddEditRow(const wxString& label, const wxString& value, const wxString& key);
    void AddEditIntRow(const wxString& label, int value, const wxString& key);
    void AddEditChoiceRow(const wxString& label,
                          const wxArrayString& choices,
                          const wxString& value,
                          const wxString& key);

    // 全部填充结束后刷新显示
    void End();

    // 常用组合：只显示一行占位提示
    void ShowPlaceholder(const wxString& message);

    // 用户编辑回调（由宿主设置）
    EditHandler OnEdit;

private:
    void OnGridChanged(wxPropertyGridEvent& event);

    // 登记一行并返回属性对象；editable=false 时设为只读
    wxPGProperty* NewRow(const wxString& label,
                         const wxString& key,
                         wxPGProperty* property,
                         bool editable);

    // 内部名 → 语义键
    std::map<wxString, wxString> m_keyOf;

    wxStaticText*   m_header = nullptr;
    wxPropertyGrid* m_grid   = nullptr;

    // wxPropertyGrid 要求每个条目的“内部名称”唯一。
    // 界面上显示的是 label，内部名称则自动编号，避免
    // 分类名与属性名重名导致断言失败。
    int m_seq = 0;

    // 重建属性表时会短暂地触发 wxEVT_PG_CHANGED，
    // 用它挡住，避免“刷新一下界面就当成用户改了值”。
    bool m_filling = false;

    // 上一次调分栏比例时网格的宽度（像素）。
    // 宽度没变就不再重设，避免 wxEVT_SIZE → SetSplitterPosition → 再布局的死循环。
    int m_lastSplitWidth = -1;
};

} // namespace se
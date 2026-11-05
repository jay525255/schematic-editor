// ============================================================
//  PropertyPanel.h  —— 属性表面板（右侧分区）
//
//  用 wxPropertyGrid 显示当前选中对象（画布上的元件实例 /
//  元件库条目）的属性，并固定附带一段画布视图信息。
//
//  面板本身只负责“显示”，要显示什么由 MainFrame 决定，
//  这样将来接入真实元件参数编辑时不必改动本类。
// ============================================================

#pragma once

#include <wx/wx.h>
#include <wx/propgrid/propgrid.h>

namespace se
{

class PropertyPanel : public wxPanel
{
public:
    explicit PropertyPanel(wxWindow* parent);

    // 开始一次填充：设置标题并清空内容
    void Begin(const wxString& title);

    // 分组标题（属性表里的可折叠分类行）
    void AddCategory(const wxString& name);

    // 只读属性行
    void AddRow(const wxString& label, const wxString& value);
    void AddIntRow(const wxString& label, int value);

    // 全部填充结束后刷新显示
    void End();

    // 常用组合：只显示一行占位提示
    void ShowPlaceholder(const wxString& message);

private:
    wxStaticText*   m_header = nullptr;
    wxPropertyGrid* m_grid   = nullptr;

    // wxPropertyGrid 要求每个条目的“内部名称”唯一。
    // 界面上显示的是 label，内部名称则自动编号，避免
    // 分类名与属性名重名导致断言失败。
    int m_seq = 0;
};

} // namespace se

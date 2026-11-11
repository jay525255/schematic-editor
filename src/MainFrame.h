// ============================================================
//  MainFrame.h  —— 主窗口
//
//  界面分区（对标 Logisim 主界面）：
//
//    ┌──────────────────────────────────────────────┐
//    │ 菜单栏                                        │
//    │ 工具栏                                        │
//    ├──────────┬────────────────────┬──────────────┤
//    │ 元件库    │      绘图区         │   属性表      │
//    │ TreeCtrl │  SchematicCanvas   │ PropertyGrid │
//    ├──────────┴────────────────────┴──────────────┤
//    │ 状态栏：提示 / 工具 / 坐标 / 缩放 / 元件数        │
//    └──────────────────────────────────────────────┘
//
//  左右两个 wxSplitterWindow 让三个分区都可拖拽调整宽度。
// ============================================================

#pragma once

#include <wx/wx.h>
#include <wx/splitter.h>
#include <wx/treectrl.h>

#include <vector>

#include "LibraryPanel.h"
#include "PropertyPanel.h"
#include "SchematicCanvas.h"

namespace se
{

class WaveformWindow;

class MainFrame : public wxFrame
{
public:
    MainFrame();
    SchematicCanvas* Canvas() const { return m_canvas; }

private:
    // ---- 界面构建 ----
    void CreateMenus();
    void CreateToolBars();
    void CreateStatusFields();
    void CreateLayout();

    // ---- 联动刷新 ----
    void SyncViewState();      // 把画布状态同步到菜单勾选与工具栏按钮
    void SyncSimState();       // 把仿真运行状态同步到"仿真"按钮的按下态与菜单勾
    void UpdateStatusBar();    // 刷新状态栏
    void UpdateProperties();   // 刷新属性表
    void SetHint(const wxString& text);

    // ---- 画布回调 ----
    void OnCanvasCursor(const wxPoint& world);
    void OnCanvasSelectionChanged();
    void OnCanvasViewChanged();

    // ---- 属性表编辑 ----
    //  属性面板改完一行 → 交给画布改模型 → 刷新标题 / 状态栏 / 属性表。
    //  被拒绝时（重号、非法坐标等）提示原因并把界面还原成模型里的真实值。
    void ApplyPropertyEdit(const wxString& key, const wxVariant& value);

    // ---- 命令统一入口 ----
    //  绑定区间 [wxID_LOWEST, ID_SIM_STOP]，既覆盖全部菜单项也覆盖工具栏；
    //  已在 switch 中实现的走真实逻辑，其余统一到底部的“尚未实现”分支。
    void OnCommand(wxCommandEvent& event);
    void OnCharHook(wxKeyEvent& event);
    void OnRecentFile(wxCommandEvent& event);
    void OnClose(wxCloseEvent& event);

    // ---- 文件 / 编辑（返回 false 表示操作被取消或失败）----
    void DoNewSchematic();
    bool DoOpenSchematic();
    bool OpenPath(const wxString& path);
    bool DoSaveSchematic(bool saveAs);
    bool DoExportNetlist();
    bool MaybeSave();                 // 有未保存修改时询问
    void UpdateTitle();
    void AddRecentFile(const wxString& path);
    void RebuildRecentMenu();

    // ---- 仿真波形窗口（独立窗口，首次打开时创建，关闭只是隐藏）----
    void ShowWaveformWindow();
    void RefreshWaveform();

    // ---- 面板 ----
    LibraryPanel*    m_library = nullptr;
    PropertyPanel*   m_props   = nullptr;
    SchematicCanvas* m_canvas  = nullptr;
    WaveformWindow*  m_waveform = nullptr;   // 懒创建；关闭时只隐藏

    wxSplitterWindow* m_splitOuter = nullptr;   // 左：元件库 | 右：其余
    wxSplitterWindow* m_splitRight = nullptr;   // 左：绘图区 | 右：属性表

    wxToolBar* m_toolBar = nullptr;

    // ---- 状态 ----
    wxPoint  m_cursorWorld;
    wxString m_currentFile;                       // 空 = 未命名
    wxMenu*  m_recentMenu = nullptr;
    std::vector<wxString> m_recentFiles;
};

} // namespace se

// ============================================================
//  minimal.cpp  —— 程序入口
//
//  由原来的“单文件骨架”拆分而来：
//    菜单 / 工具栏 / 三分区布局  → MainFrame.h / MainFrame.cpp
//    元件库数据模型              → ComponentCatalog.h / .cpp
//    元件库面板                  → LibraryPanel.h / .cpp
//    绘图区                      → SchematicCanvas.h / .cpp
//    属性表面板                  → PropertyPanel.h / .cpp
//    命令 ID                     → Ids.h
//
//  本文件只保留 wxApp 与主窗口的创建。
//  原文件（含全部菜单定义）已完整迁移到 MainFrame.cpp 的 CreateMenus()。
// ============================================================

#include <wx/wx.h>
#include <wx/image.h>
#include <wx/stdpaths.h>
#include <wx/filename.h>
#include <wx/ffile.h>

#include <cstdio>
#include <cstdlib>

#include "MainFrame.h"

#if wxDEBUG_LEVEL
// ------------------------------------------------------------
// Debug 断言处理
//
// 默认行为是弹一个模态对话框，自动化测试脚本会被它卡住，看不出
// 到底是哪条断言失败。设置环境变量 SE_ASSERT_LOG=1 后改成：
//    把断言内容追加到 exe 旁边的 assert.log，然后继续执行。
// 不设这个变量时行为完全不变（仍然弹默认对话框）。
// ------------------------------------------------------------
static void AssertLogger(const wxString& file, int line, const wxString& func,
                         const wxString& cond, const wxString& msg)
{
    // 注意：不能用 wxStandardPaths —— 断言可能发生在 wxApp 创建之前
    // （比如静态初始化里出错），那时 wxTheApp 是空的，wxStandardPaths
    // 自己会再断言一次 "create wxApp before calling this"，把真正的
    // 断言内容盖掉。改用 Win32 模块路径，任何时候都安全。
    wxString exePath;

    if (wxTheApp != nullptr)
    {
        exePath = wxStandardPaths::Get().GetExecutablePath();
    }
#ifdef __WXMSW__
    else
    {
        wchar_t modulePath[MAX_PATH * 2] = { 0 };

        if (::GetModuleFileNameW(nullptr, modulePath,
                                 static_cast<DWORD>(WXSIZEOF(modulePath))) > 0)
            exePath = wxString(modulePath);
    }
#else
    else
    {
        // 非 Windows：没有 wxApp 时没法问"程序在哪"，退到临时目录
        exePath = wxFileName::GetTempDir();
    }
#endif

    wxFileName logFile(exePath);
    logFile.SetFullName(L"assert.log");

    wxFFile fileOut(logFile.GetFullPath(), L"a");
    if (fileOut.IsOpened())
        fileOut.Write(wxString::Format(L"Assert: %s:%d %s %s %s\n", file, line, func, cond, msg), wxConvUTF8);
}
#endif

// ------------------------------------------------------------
// 应用程序
// ------------------------------------------------------------
class SchematicApp : public wxApp
{
public:
    bool OnInit() override
    {
        if (!wxApp::OnInit())
            return false;

#if wxDEBUG_LEVEL
        if (wxGetenv("SE_ASSERT_LOG") != nullptr)
            wxSetAssertHandler(&AssertLogger);
#endif

        // 位图 / PNG 图像处理器（wxArtProvider 的图标需要）
        wxInitAllImageHandlers();

        se::MainFrame* frame = new se::MainFrame();

        frame->Show(true);

        return true;
    }
};

wxIMPLEMENT_APP(SchematicApp);

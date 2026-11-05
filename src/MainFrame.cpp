#include "MainFrame.h"

#include <wx/aboutdlg.h>
#include <wx/artprov.h>
#include <wx/bmpbndl.h>
#include <wx/filedlg.h>
#include <wx/filename.h>
#include <wx/utils.h>

#include <algorithm>

#include "ComponentEditorDialog.h"
#include "Ids.h"
#include "SchematicIO.h"
#include "TextReportDialog.h"

namespace se
{

// ============================================================
//  工具栏图标
//
//  wxWidgets 3.2 内置 nanosvg，可以直接把 SVG 文本变成矢量图标，
//  不需要额外的 .ico / .png 资源文件，也不怕高 DPI 缩放模糊。
// ============================================================
namespace
{

wxBitmapBundle Icon(const char* svg)
{
    return wxBitmapBundle::FromSVG(svg, wxSize(24, 24));
}

const char* kSvgNew =
    R"(<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 24 24">)"
    R"(<path d="M6 3h8l4 4v14H6z" fill="none" stroke="#334155" stroke-width="1.6" stroke-linejoin="round"/>)"
    R"(<path d="M14 3v4h4" fill="none" stroke="#334155" stroke-width="1.6"/>)"
    R"(<path d="M12 11v6M9 14h6" stroke="#15803d" stroke-width="1.8" stroke-linecap="round"/></svg>)";

const char* kSvgOpen =
    R"(<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 24 24">)"
    R"(<path d="M3 6.5h6.2l1.8 2H21v10.5H3z" fill="none" stroke="#334155" stroke-width="1.6" stroke-linejoin="round"/></svg>)";

const char* kSvgSave =
    R"(<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 24 24">)"
    R"(<path d="M4 4h12.5L20 7.5V20H4z" fill="none" stroke="#334155" stroke-width="1.6" stroke-linejoin="round"/>)"
    R"(<path d="M8 4v6h7V4" fill="none" stroke="#334155" stroke-width="1.6"/>)"
    R"(<rect x="7.5" y="13" width="9" height="7" fill="none" stroke="#334155" stroke-width="1.6"/></svg>)";

const char* kSvgUndo =
    R"(<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 24 24">)"
    R"(<path d="M9.5 7.5H15a4.5 4.5 0 010 9h-5" fill="none" stroke="#334155" stroke-width="1.8" stroke-linecap="round"/>)"
    R"(<path d="M12.5 4.5L9 7.5l3.5 3" fill="none" stroke="#334155" stroke-width="1.8" stroke-linecap="round" stroke-linejoin="round"/></svg>)";

const char* kSvgRedo =
    R"(<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 24 24">)"
    R"(<path d="M14.5 7.5H9a4.5 4.5 0 000 9h5" fill="none" stroke="#334155" stroke-width="1.8" stroke-linecap="round"/>)"
    R"(<path d="M11.5 4.5L15 7.5l-3.5 3" fill="none" stroke="#334155" stroke-width="1.8" stroke-linecap="round" stroke-linejoin="round"/></svg>)";

const char* kSvgZoomIn =
    R"(<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 24 24">)"
    R"(<circle cx="10.5" cy="10.5" r="6" fill="none" stroke="#334155" stroke-width="1.8"/>)"
    R"(<path d="M15 15l5 5" stroke="#334155" stroke-width="2.2" stroke-linecap="round"/>)"
    R"(<path d="M10.5 7.5v6M7.5 10.5h6" stroke="#15803d" stroke-width="1.8" stroke-linecap="round"/></svg>)";

const char* kSvgZoomOut =
    R"(<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 24 24">)"
    R"(<circle cx="10.5" cy="10.5" r="6" fill="none" stroke="#334155" stroke-width="1.8"/>)"
    R"(<path d="M15 15l5 5" stroke="#334155" stroke-width="2.2" stroke-linecap="round"/>)"
    R"(<path d="M7.5 10.5h6" stroke="#15803d" stroke-width="1.8" stroke-linecap="round"/></svg>)";

const char* kSvgZoomFit =
    R"(<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 24 24">)"
    R"(<rect x="3" y="5.5" width="18" height="13" rx="1" fill="none" stroke="#334155" stroke-width="1.6"/>)"
    R"(<path d="M7.5 9.5l3 2.5-3 2.5M16.5 9.5l-3 2.5 3 2.5" fill="none" stroke="#15803d" stroke-width="1.7" stroke-linecap="round" stroke-linejoin="round"/></svg>)";

const char* kSvgZoom100 =
    R"(<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 24 24">)"
    R"(<path d="M4 8V4h4M20 8V4h-4M4 16v4h4M20 16v4h-4" fill="none" stroke="#334155" stroke-width="1.8" stroke-linecap="round"/>)"
    R"(<rect x="9" y="9" width="6" height="6" fill="none" stroke="#15803d" stroke-width="1.7"/></svg>)";

const char* kSvgGrid =
    R"(<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 24 24">)"
    R"(<rect x="3.5" y="3.5" width="17" height="17" fill="none" stroke="#334155" stroke-width="1.6"/>)"
    R"(<path d="M9.17 3.5v17M14.83 3.5v17M3.5 9.17h17M3.5 14.83h17" stroke="#94a3b8" stroke-width="1"/></svg>)";

const char* kSvgSnap =
    R"(<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 24 24">)"
    R"(<path d="M3.5 3.5h17v17h-17z" fill="none" stroke="#cbd5e1" stroke-width="1"/>)"
    R"(<path d="M3.5 12h17M12 3.5v17" stroke="#cbd5e1" stroke-width="1"/>)"
    R"(<circle cx="12" cy="12" r="3.2" fill="#15803d"/></svg>)";

const char* kSvgCrosshair =
    R"(<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 24 24">)"
    R"(<path d="M12 3v18M3 12h18" stroke="#334155" stroke-width="1.4"/>)"
    R"(<circle cx="12" cy="12" r="4" fill="none" stroke="#15803d" stroke-width="1.7"/></svg>)";

const char* kSvgSelect =
    R"(<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 24 24">)"
    R"(<path d="M6 2L6 18L10 14L13 20L15.5 19L12.5 13.2L18 13.2Z" fill="#334155" stroke="#334155" stroke-width="1" stroke-linejoin="round"/></svg>)";

const char* kSvgWire =
    R"(<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 24 24">)"
    R"(<path d="M3.5 18.5h6.5L14 5.5h6.5" fill="none" stroke="#15803d" stroke-width="1.9" stroke-linecap="round" stroke-linejoin="round"/>)"
    R"(<circle cx="3.5" cy="18.5" r="1.9" fill="#15803d"/><circle cx="20.5" cy="5.5" r="1.9" fill="#15803d"/></svg>)";

const char* kSvgText =
    R"(<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 24 24">)"
    R"(<path d="M5 6.5h14M12 6.5V19M9 19h6" fill="none" stroke="#334155" stroke-width="1.9" stroke-linecap="round"/></svg>)";

const char* kSvgClear =
    R"(<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 24 24">)"
    R"(<path d="M5 7h14M9 7V4.5h6V7M7.5 7l1 13.5h7l1-13.5" fill="none" stroke="#334155" stroke-width="1.6" stroke-linecap="round" stroke-linejoin="round"/>)"
    R"(<path d="M10.5 10.5v6.5M13.5 10.5v6.5" stroke="#b91c1c" stroke-width="1.5" stroke-linecap="round"/></svg>)";

const char* kSvgSimulate =
    R"(<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 24 24">)"
    R"(<path d="M7 4.5l12 7.5-12 7.5z" fill="#15803d"/></svg>)";

} // namespace

// ============================================================
//  构造
// ============================================================

MainFrame::MainFrame()
    : wxFrame(nullptr,
              wxID_ANY,
              L"Untitled - Schematic Editor",
              wxDefaultPosition,
              wxDefaultSize)
{
    // 按屏幕可用区域决定初始尺寸，避免窗口比屏幕还高
    const wxRect area = wxGetClientDisplayRect();

    const int width  = std::min(1280, std::max(900, area.width  - 80));
    const int height = std::min(820,  std::max(600, area.height - 80));

    SetSize(width, height);
    SetMinSize(wxSize(820, 560));

    // ---- 元件库：内置清单 + 程序目录下的 components.xml ----
    //  必须在 CreateLayout() 之前完成：元件库面板构造时就会把树建出来。
    //  第一次运行会自动生成一份带示例的模板文件，用户照着改就能加元件。
    {
        wxString error;

        // 这里的失败不该拦住启动：读不到用户库就只显示内置元件，
        // 菜单「工具 → 重新加载元件库」会把具体错误弹出来。
        ComponentCatalog::EnsureUserLibrary(&error);
        ComponentCatalog::Reload(&error);
    }

    CreateMenus();
    CreateToolBars();
    CreateStatusFields();
    CreateLayout();

    // ---- 面板联动 ----
    m_canvas->OnCursorMoved = [this](const wxPoint& world)
    {
        m_cursorWorld = world;
        UpdateStatusBar();
    };

    m_canvas->OnSelectionChanged = [this]()
    {
        UpdateProperties();
        UpdateStatusBar();
        UpdateTitle();
    };

    m_canvas->OnViewChanged = [this]()
    {
        SyncViewState();
        UpdateProperties();
        UpdateStatusBar();
    };

    m_canvas->OnStatusMessage = [this](const wxString& text)
    {
        SetHint(text);
    };

    m_library->SetOnSelect([this](const ComponentDef* def)
    {
        m_canvas->SetPendingComponent(def);

        if (def)
            SetHint(wxString::Format(L"已选中「%s」，在绘图区单击即可放置", def->name));
        else
            SetHint(L"已取消元件选择");

        UpdateProperties();
        UpdateStatusBar();
    });

    // 元件放完之后：把元件库里的高亮也一起清掉，
    // 这样“选元件 → 放一个 → 回到选择状态”是一条完整的动作，不用再按 Esc
    m_canvas->OnPlaceFinished = [this]()
    {
        m_library->ClearSelection();
    };

    // 一段导线画完（接到引脚 / 导线端点上）就自动回到“选择”，
    // 省掉以前必须先点一下右键的那步
    m_canvas->OnWireFinished = [this]()
    {
        m_canvas->SetTool(SchematicCanvas::ToolSelect);

        if (m_toolBar)
            m_toolBar->ToggleTool(ID_TOOL_SELECT, true);

        UpdateStatusBar();
        SetHint(L"导线已画完，已回到选择工具（要接着画再点一次“导线”）");
    };

    SyncViewState();
    UpdateProperties();
    UpdateStatusBar();
    UpdateTitle();

    SetHint(L"就绪 — 从左侧元件库选一个元件，然后在绘图区单击放置");

    Bind(wxEVT_CLOSE_WINDOW, &MainFrame::OnClose, this);

    Centre();
}

// ============================================================
//  菜单
//  （沿用小组已经写好的菜单结构，未做改动）
// ============================================================

void MainFrame::CreateMenus()
{
    wxMenuBar* menuBar = new wxMenuBar();

    // ========================================================
    // 文件
    // ========================================================
    wxMenu* fileMenu = new wxMenu;

    fileMenu->Append(wxID_NEW,  L"新建原理图\tCtrl+N");
    fileMenu->Append(wxID_OPEN, L"打开...\tCtrl+O");
    fileMenu->AppendSeparator();
    fileMenu->Append(wxID_SAVE, L"保存\tCtrl+S");
    fileMenu->Append(ID_FILE_SAVE_AS, L"另存为...\tCtrl+Shift+S");
    fileMenu->AppendSeparator();

    wxMenu* importMenu = new wxMenu;
    importMenu->Append(ID_FILE_IMPORT, L"导入原理图 (.ses)...");
    fileMenu->AppendSubMenu(importMenu, L"导入");

    wxMenu* exportMenu = new wxMenu;
    exportMenu->Append(ID_FILE_EXPORT, L"导出网表 (.net)...");
    fileMenu->AppendSubMenu(exportMenu, L"导出");

    fileMenu->AppendSeparator();

    m_recentMenu = new wxMenu;
    wxMenuItem* recentEmpty = m_recentMenu->Append(wxID_ANY, L"暂无最近文件");
    recentEmpty->Enable(false);
    fileMenu->AppendSubMenu(m_recentMenu, L"最近打开");

    fileMenu->AppendSeparator();
    fileMenu->Append(ID_FILE_CLOSE_PROJECT, L"关闭原理图");
    fileMenu->AppendSeparator();
    fileMenu->Append(wxID_EXIT, L"退出\tAlt+F4");

    menuBar->Append(fileMenu, L"文件(&F)");

    // ========================================================
    // 编辑
    // ========================================================
    wxMenu* editMenu = new wxMenu;

    editMenu->Append(wxID_UNDO, L"撤销\tCtrl+Z");
    editMenu->Append(ID_EDIT_REDO, L"重做\tCtrl+Y");
    editMenu->AppendSeparator();
    editMenu->Append(wxID_CUT,   L"剪切\tCtrl+X");
    editMenu->Append(wxID_COPY,  L"复制\tCtrl+C");
    editMenu->Append(wxID_PASTE, L"粘贴\tCtrl+V");
    editMenu->AppendSeparator();
    editMenu->Append(ID_EDIT_DELETE, L"删除\tDelete");
    editMenu->AppendSeparator();
    editMenu->Append(ID_EDIT_SELECT_ALL,   L"全选\tCtrl+A");
    editMenu->Append(ID_EDIT_DESELECT_ALL, L"取消选择\tCtrl+Shift+A");
    editMenu->AppendSeparator();
    // 旋转 / 翻转只用 Ctrl 组合键当加速键：单独的 R / H 会把
    // 元件库搜索框里的字母也吃掉。
    editMenu->Append(ID_EDIT_ROTATE_CW,  L"旋转 90°（顺时针）\tCtrl+R");
    editMenu->Append(ID_EDIT_ROTATE_CCW, L"旋转 90°（逆时针）\tCtrl+Shift+R");
    editMenu->Append(ID_EDIT_FLIP_H,     L"水平翻转（左右）\tCtrl+H");
    editMenu->Append(ID_EDIT_FLIP_V,     L"垂直翻转（上下）\tCtrl+Shift+H");

    menuBar->Append(editMenu, L"编辑(&E)");

    // ========================================================
    // 视图
    // ========================================================
    wxMenu* viewMenu = new wxMenu;

    viewMenu->Append(ID_VIEW_ZOOM_IN,  L"放大\tCtrl++");
    viewMenu->Append(ID_VIEW_ZOOM_OUT, L"缩小\tCtrl+-");
    viewMenu->Append(ID_VIEW_ZOOM_FIT, L"适合窗口\tHome");
    viewMenu->Append(ID_VIEW_ZOOM_100, L"实际大小 100%");
    viewMenu->AppendSeparator();

    viewMenu->AppendCheckItem(ID_VIEW_GRID, L"显示网格");
    viewMenu->Check(ID_VIEW_GRID, true);

    viewMenu->AppendCheckItem(ID_VIEW_SNAP_GRID, L"吸附到网格");
    viewMenu->Check(ID_VIEW_SNAP_GRID, true);

    viewMenu->AppendCheckItem(ID_VIEW_CROSSHAIR, L"显示十字光标");

    viewMenu->AppendSeparator();
    viewMenu->AppendCheckItem(ID_VIEW_FULLSCREEN, L"全屏模式\tF11");

    menuBar->Append(viewMenu, L"视图(&V)");

    // ========================================================
    // 放置
    // ========================================================
    wxMenu* placeMenu = new wxMenu;

    placeMenu->Append(ID_PLACE_COMPONENT, L"放置元件...\tA");
    placeMenu->AppendSeparator();

    wxMenu* passiveMenu = new wxMenu;
    passiveMenu->Append(ID_PLACE_RESISTOR,  L"电阻");
    passiveMenu->Append(ID_PLACE_CAPACITOR, L"电容");
    passiveMenu->Append(ID_PLACE_INDUCTOR,  L"电感");
    placeMenu->AppendSubMenu(passiveMenu, L"无源器件");

    wxMenu* semiconductorMenu = new wxMenu;
    semiconductorMenu->Append(ID_PLACE_DIODE, L"二极管");
    semiconductorMenu->Append(ID_PLACE_LED,   L"LED");
    semiconductorMenu->AppendSeparator();
    semiconductorMenu->Append(ID_PLACE_NPN, L"NPN 晶体管");
    semiconductorMenu->Append(ID_PLACE_PNP, L"PNP 晶体管");
    placeMenu->AppendSubMenu(semiconductorMenu, L"半导体器件");

    wxMenu* analogMenu = new wxMenu;
    analogMenu->Append(ID_PLACE_OPAMP, L"运算放大器");
    placeMenu->AppendSubMenu(analogMenu, L"模拟器件");

    placeMenu->AppendSeparator();
    placeMenu->Append(ID_PLACE_WIRE,    L"导线\tW");
    placeMenu->Append(ID_PLACE_BUS,     L"总线\tB");
    placeMenu->Append(ID_PLACE_JUNCTION, L"连接点");
    placeMenu->AppendSeparator();
    placeMenu->Append(ID_PLACE_NET_LABEL,    L"网络标签\tL");
    placeMenu->Append(ID_PLACE_GLOBAL_LABEL, L"全局标签");
    placeMenu->AppendSeparator();

    wxMenu* powerMenu = new wxMenu;
    powerMenu->Append(ID_PLACE_POWER, L"电源符号");
    powerMenu->Append(ID_PLACE_GND,   L"GND");
    placeMenu->AppendSubMenu(powerMenu, L"电源");

    placeMenu->Append(ID_PLACE_NOCONNECT, L"未连接标记\tQ");
    placeMenu->AppendSeparator();
    placeMenu->Append(ID_PLACE_TEXT, L"文本...");

    menuBar->Append(placeMenu, L"放置(&P)");

    // ========================================================
    // 检查
    // ========================================================
    wxMenu* inspectMenu = new wxMenu;

    inspectMenu->Append(ID_INSPECT_PROPERTIES, L"属性\tE");
    inspectMenu->AppendSeparator();
    inspectMenu->Append(ID_INSPECT_ERC,      L"电气规则检查 (ERC)...");
    inspectMenu->Append(ID_INSPECT_NETLIST,  L"查看网络表...");
    inspectMenu->AppendSeparator();
    inspectMenu->Append(ID_INSPECT_STATISTICS, L"原理图统计...");

    menuBar->Append(inspectMenu, L"检查(&I)");

    // ========================================================
    // 工具
    // ========================================================
    wxMenu* toolsMenu = new wxMenu;

    toolsMenu->Append(ID_TOOLS_ANNOTATE,         L"元件自动编号...");
    toolsMenu->Append(ID_TOOLS_ASSIGN_FOOTPRINTS, L"分配封装...");
    toolsMenu->AppendSeparator();
    toolsMenu->Append(ID_TOOLS_GENERATE_NETLIST, L"生成网络表...");
    toolsMenu->Append(ID_TOOLS_GENERATE_BOM,     L"生成物料清单 (BOM)...");
    toolsMenu->AppendSeparator();
    toolsMenu->Append(ID_TOOLS_SIMULATOR, L"电路仿真器...");
    toolsMenu->Append(ID_SIM_TICK,        L"仿真：时钟单步\tSpace");
    toolsMenu->AppendSeparator();
    toolsMenu->Append(ID_LIBRARY_MANAGE,  L"元件库管理...");
    toolsMenu->Append(ID_LIBRARY_RELOAD,  L"重新加载元件库");

    menuBar->Append(toolsMenu, L"工具(&T)");

    // ========================================================
    // 首选项
    // ========================================================
    wxMenu* prefMenu = new wxMenu;

    prefMenu->Append(ID_PREF_GENERAL, L"常规设置...");
    prefMenu->Append(ID_PREF_GRID,    L"网格设置...");
    prefMenu->Append(ID_PREF_HOTKEYS, L"快捷键...");
    prefMenu->AppendSeparator();
    prefMenu->Append(ID_PREF_PATHS,   L"库与路径...");

    menuBar->Append(prefMenu, L"首选项(&R)");

    // ========================================================
    // 帮助
    // ========================================================
    wxMenu* helpMenu = new wxMenu;

    helpMenu->Append(ID_HELP_DOCUMENTATION, L"用户手册");
    helpMenu->Append(ID_HELP_SHORTCUTS,     L"快捷键参考");
    helpMenu->AppendSeparator();
    helpMenu->Append(wxID_ABOUT, L"关于 Schematic Editor");

    menuBar->Append(helpMenu, L"帮助(&H)");

    SetMenuBar(menuBar);

    // ========================================================
    // 命令绑定
    //
    //  一次性把 [wxID_LOWEST, ID_COMMAND_END] 整段绑到 OnCommand，
    //  覆盖所有菜单项与工具栏按钮；OnCommand 内部 switch 分发，
    //  未实现的落到“尚未实现”分支。
    //
    //  为什么用 wxID_LOWEST 而不是 wxID_NEW：
    //  wxID_OPEN(=5000) 比 wxID_NEW(=5002) 小，用 wxID_NEW 当左边界
    //  会把「打开」漏掉。
    // ========================================================
    Bind(wxEVT_MENU, &MainFrame::OnCommand, this, wxID_LOWEST, ID_COMMAND_END);
    Bind(wxEVT_TOOL, &MainFrame::OnCommand, this, wxID_LOWEST, ID_COMMAND_END);

    // 最近打开文件：单独绑定（不参与上面的兜底区间）
    Bind(wxEVT_MENU, &MainFrame::OnRecentFile, this,
         ID_FILE_RECENT_BASE, ID_FILE_RECENT_BASE + ID_FILE_RECENT_MAX - 1);
}

// ============================================================
//  工具栏
// ============================================================

void MainFrame::CreateToolBars()
{
    m_toolBar = CreateToolBar(wxTB_HORIZONTAL | wxTB_FLAT | wxTB_NODIVIDER);
    m_toolBar->SetToolBitmapSize(wxSize(24, 24));

    // ---- 文件 ----
    m_toolBar->AddTool(wxID_NEW,  L"新建", Icon(kSvgNew),  L"新建原理图 (Ctrl+N)");
    m_toolBar->AddTool(wxID_OPEN, L"打开", Icon(kSvgOpen), L"打开原理图 (Ctrl+O)");
    m_toolBar->AddTool(wxID_SAVE, L"保存", Icon(kSvgSave), L"保存原理图 (Ctrl+S)");
    m_toolBar->AddSeparator();

    // ---- 编辑 ----
    m_toolBar->AddTool(wxID_UNDO,    L"撤销", Icon(kSvgUndo), L"撤销 (Ctrl+Z)");
    m_toolBar->AddTool(ID_EDIT_REDO, L"重做", Icon(kSvgRedo), L"重做 (Ctrl+Y)");
    m_toolBar->AddSeparator();

    // ---- 视图 ----
    m_toolBar->AddTool(ID_VIEW_ZOOM_IN,  L"放大",     Icon(kSvgZoomIn),  L"放大 (Ctrl++)");
    m_toolBar->AddTool(ID_VIEW_ZOOM_OUT, L"缩小",     Icon(kSvgZoomOut), L"缩小 (Ctrl+-)");
    m_toolBar->AddTool(ID_VIEW_ZOOM_FIT, L"适合窗口", Icon(kSvgZoomFit), L"适合窗口 (Home)");
    m_toolBar->AddTool(ID_VIEW_ZOOM_100, L"实际大小", Icon(kSvgZoom100), L"缩放回 100%");
    m_toolBar->AddSeparator();

    m_toolBar->AddCheckTool(ID_VIEW_GRID,      L"显示网格", Icon(kSvgGrid),      wxNullBitmap, L"显示 / 隐藏网格");
    m_toolBar->AddCheckTool(ID_VIEW_SNAP_GRID, L"吸附网格", Icon(kSvgSnap),      wxNullBitmap, L"放置时吸附到网格");
    m_toolBar->AddCheckTool(ID_VIEW_CROSSHAIR, L"十字光标", Icon(kSvgCrosshair), wxNullBitmap, L"显示十字光标");
    m_toolBar->AddSeparator();

    // ---- 工具 ----
    m_toolBar->AddRadioTool(ID_TOOL_SELECT, L"选择", Icon(kSvgSelect), wxNullBitmap, L"选择 / 放置元件 (Esc 取消)");
    m_toolBar->AddRadioTool(ID_TOOL_WIRE,   L"导线", Icon(kSvgWire),   wxNullBitmap, L"绘制导线 (任务 4)");
    m_toolBar->AddRadioTool(ID_TOOL_TEXT,   L"文本", Icon(kSvgText),   wxNullBitmap, L"放置文本");
    m_toolBar->AddSeparator();

    // ---- 其它 ----
    m_toolBar->AddTool(ID_VIEW_RESET, L"清空画布", Icon(kSvgClear),    L"清空画布上的所有元件");
    m_toolBar->AddTool(ID_SIM_RUN,    L"仿真",     Icon(kSvgSimulate), L"运行逻辑仿真 (任务 6)");

    m_toolBar->Realize();

    // 初始状态
    m_toolBar->ToggleTool(ID_VIEW_GRID, true);
    m_toolBar->ToggleTool(ID_VIEW_SNAP_GRID, true);
    m_toolBar->ToggleTool(ID_TOOL_SELECT, true);
}

// ============================================================
//  状态栏
// ============================================================

void MainFrame::CreateStatusFields()
{
    CreateStatusBar(5);

    const int widths[5] = { -2, 190, 160, 110, 270 };
    SetStatusWidths(5, widths);

    SetStatusText(L"就绪", 0);
}

// ============================================================
//  三分区布局
// ============================================================

void MainFrame::CreateLayout()
{
    // 外层：左（元件库） | 右（绘图区 + 属性表）
    m_splitOuter = new wxSplitterWindow(this,
                                        wxID_ANY,
                                        wxDefaultPosition,
                                        wxDefaultSize,
                                        wxSP_LIVE_UPDATE | wxSP_3DSASH);
    m_splitOuter->SetMinimumPaneSize(170);
    // gravity = 0：窗口变宽时保持左侧元件库宽度不变
    m_splitOuter->SetSashGravity(0.0);

    // 内层：左（绘图区） | 右（属性表）
    m_splitRight = new wxSplitterWindow(m_splitOuter,
                                        wxID_ANY,
                                        wxDefaultPosition,
                                        wxDefaultSize,
                                        wxSP_LIVE_UPDATE | wxSP_3DSASH);
    m_splitRight->SetMinimumPaneSize(170);
    // gravity = 1：窗口变宽时保持右侧属性表宽度不变，多出来的都给绘图区
    m_splitRight->SetSashGravity(1.0);

    m_library = new LibraryPanel(m_splitOuter);
    m_canvas  = new SchematicCanvas(m_splitRight);
    m_props   = new PropertyPanel(m_splitRight);

    const int totalWidth = GetClientSize().x;

    m_splitRight->SplitVertically(m_canvas, m_props,
                                  std::max(400, totalWidth - 300));
    m_splitOuter->SplitVertically(m_library, m_splitRight, 260);

    wxBoxSizer* sizer = new wxBoxSizer(wxVERTICAL);
    sizer->Add(m_splitOuter, 1, wxEXPAND);
    SetSizer(sizer);
}

// ============================================================
//  联动刷新
// ============================================================

void MainFrame::SyncViewState()
{
    if (m_toolBar)
    {
        m_toolBar->ToggleTool(ID_VIEW_GRID,      m_canvas->IsGridVisible());
        m_toolBar->ToggleTool(ID_VIEW_SNAP_GRID, m_canvas->IsSnapEnabled());
        m_toolBar->ToggleTool(ID_VIEW_CROSSHAIR, m_canvas->IsCrosshairVisible());
    }

    if (wxMenuBar* bar = GetMenuBar())
    {
        bar->Check(ID_VIEW_GRID,      m_canvas->IsGridVisible());
        bar->Check(ID_VIEW_SNAP_GRID, m_canvas->IsSnapEnabled());
        bar->Check(ID_VIEW_CROSSHAIR, m_canvas->IsCrosshairVisible());
    }
}

void MainFrame::UpdateStatusBar()
{
    // 字段 0 留给提示消息，这里刷新 1..4
    wxString tool = L"工具：选择";

    if (m_canvas->IsSimulating())
    {
        tool = m_canvas->GetSimulator().GetClockHigh()
                   ? L"仿真中 · 时钟高电平"
                   : L"仿真中 · 时钟低电平";
    }
    else
    {
        switch (m_canvas->GetTool())
        {
            case SchematicCanvas::ToolSelect:
                if (const ComponentDef* def = m_canvas->GetPendingComponent())
                    tool = wxString::Format(L"工具：放置「%s」", def->name);
                else
                    tool = L"工具：选择";
                break;

            case SchematicCanvas::ToolWire:
                tool = m_canvas->IsDrawingWire()
                           ? L"工具：导线（绘制中，右键/Esc 结束）"
                           : L"工具：导线（单击放折点）";
                break;

            case SchematicCanvas::ToolText:
                tool = L"工具：文本（单击画布输入文字）";
                break;
        }
    }

    const NetGraph& graph = m_canvas->GetNetGraph();

    SetStatusText(tool, 1);
    SetStatusText(wxString::Format(L"坐标：%d, %d", m_cursorWorld.x, m_cursorWorld.y), 2);
    SetStatusText(wxString::Format(L"缩放：%.0f%%", m_canvas->GetZoom() * 100.0), 3);
    SetStatusText(wxString::Format(L"元件 %d · 导线 %d · 文本 %d · 网络 %d · 选中 %d",
                                   m_canvas->GetComponentCount(),
                                   m_canvas->GetWireCount(),
                                   m_canvas->GetTextCount(),
                                   static_cast<int>(graph.nets.size()),
                                   m_canvas->GetSelectionCount()), 4);
}

// ------------------------------------------------------------
//  属性表
//  选中元件 → 元件信息 + 每个引脚连到哪个网络
//  选中导线 → 导线几何 + 所属网络 + 网络上的全部引脚
// ------------------------------------------------------------
void MainFrame::UpdateProperties()
{
    m_props->Begin(L"属性");

    const NetGraph& graph = m_canvas->GetNetGraph();

    auto NetNameOf = [&](int netIndex) -> wxString
    {
        if (netIndex < 0 || netIndex >= static_cast<int>(graph.nets.size()))
            return L"<未连接>";

        return graph.nets[netIndex].name;
    };

    if (const ComponentInstance* comp = m_canvas->GetSelectedComponent())
    {
        const int compIndex = m_canvas->GetSelectedComponentIndex();

        m_props->AddCategory(wxString::Format(L"元件实例 · %s", comp->refdes));
        m_props->AddRow(L"元件名称", comp->name);
        m_props->AddRow(L"位号", comp->refdes);
        m_props->AddIntRow(L"X 坐标", comp->pos.x);
        m_props->AddIntRow(L"Y 坐标", comp->pos.y);
        m_props->AddIntRow(L"引脚数", static_cast<int>(comp->pins.size()));
        m_props->AddRow(L"旋转 / 镜像", DescribeXform(comp->xform));

        m_props->AddCategory(L"PCB 信息");
        m_props->AddRow(L"封装",
                        comp->footprint.IsEmpty()
                            ? wxString(L"（非物理元件，不写入网表）")
                            : comp->footprint);
        m_props->AddRow(L"元件值", comp->value.IsEmpty() ? comp->name : comp->value);

        m_props->AddCategory(L"引脚连接");

        for (size_t k = 0; k < comp->pins.size(); ++k)
        {
            const int netIndex = (compIndex >= 0 &&
                                  compIndex < static_cast<int>(graph.pinNet.size()) &&
                                  k < graph.pinNet[compIndex].size())
                                     ? graph.pinNet[compIndex][k]
                                     : -1;

            m_props->AddRow(wxString::Format(L"%s（%s）",
                                             comp->pins[k].name,
                                             comp->pins[k].dir == PinDir::Output ? L"输出" : L"输入"),
                            NetNameOf(netIndex));
        }
    }
    else if (m_canvas->GetSelectionCount() > 1)
    {
        // ---- 多选：给出汇总信息 + 逐个列出位号 ----
        m_props->AddCategory(L"多选");

        const int compCount = m_canvas->GetSelectedComponentCount();
        const int wireCount = m_canvas->GetSelectedWireCount();

        m_props->AddIntRow(L"选中元件", compCount);
        m_props->AddIntRow(L"选中线段", wireCount);

        if (compCount > 0)
        {
            m_props->AddCategory(L"选中的元件");

            int shown = 0;

            for (int index : m_canvas->GetSelectedComponents())
            {
                if (index < 0 || index >= static_cast<int>(m_canvas->GetComponents().size()))
                    continue;

                if (++shown > 30)
                {
                    m_props->AddRow(L"...", wxString::Format(L"其余 %d 个略",
                                                             compCount - 30));
                    break;
                }

                const ComponentInstance& item = m_canvas->GetComponents()[index];
                m_props->AddRow(item.refdes, item.name);
            }
        }
    }
    else if (const Wire* wire = m_canvas->GetSelectedWire())
    {
        const int netIndex = m_canvas->GetSelectedNetIndex();

        m_props->AddCategory(wxString::Format(L"导线 · 线段 #%d", wire->id));
        m_props->AddRow(L"起点", wxString::Format(L"(%d, %d)", wire->a.x, wire->a.y));
        m_props->AddRow(L"终点", wxString::Format(L"(%d, %d)", wire->b.x, wire->b.y));
        m_props->AddRow(L"走向", wire->a.y == wire->b.y ? L"水平" : L"垂直");
        m_props->AddIntRow(L"长度", wire->Length());
        m_props->AddRow(L"所属网络", NetNameOf(netIndex));

        m_props->AddCategory(L"网络上的引脚");

        if (netIndex >= 0 && netIndex < static_cast<int>(graph.nets.size()))
        {
            const Net& net = graph.nets[netIndex];

            if (net.pins.empty())
            {
                m_props->AddRow(L"（空）", L"该网络还没有连到任何引脚");
            }
            else
            {
                for (const PinRef& ref : net.pins)
                    m_props->AddRow(ref.refdes, ref.name);
            }
        }
        else
        {
            m_props->AddRow(L"（悬空）", L"该导线没有连到任何网络");
        }
    }
    else if (const TextItem* item = m_canvas->GetSelectedText())
    {
        m_props->AddCategory(L"文本注记");
        m_props->AddRow(L"内容", item->text);
        m_props->AddIntRow(L"X 坐标", item->pos.x);
        m_props->AddIntRow(L"Y 坐标", item->pos.y);
        m_props->AddIntRow(L"字号", item->pointSize);
        m_props->AddRow(L"说明", L"纯图面注记，不参与电气连通性");
    }
    else if (const ComponentDef* def = m_library->GetSelected())
    {
        m_props->AddCategory(L"元件库条目");
        m_props->AddRow(L"名称", def->name);
        m_props->AddRow(L"位号前缀", def->refPrefix);
        m_props->AddIntRow(L"输入引脚", def->inputs);
        m_props->AddIntRow(L"输出引脚", def->outputs);
        m_props->AddIntRow(L"外形尺寸", def->w);
        m_props->AddRow(L"说明", def->description);
    }
    else
    {
        m_props->AddCategory(L"选中对象");
        m_props->AddRow(L"选中对象", L"（无）— 单击画布上的元件或导线");
    }

    m_props->AddCategory(L"画布视图");
    m_props->AddRow(L"缩放", wxString::Format(L"%.0f%%", m_canvas->GetZoom() * 100.0));
    m_props->AddIntRow(L"网格步长", m_canvas->GetGridStep());
    m_props->AddRow(L"显示网格", m_canvas->IsGridVisible() ? L"是" : L"否");
    m_props->AddRow(L"吸附网格", m_canvas->IsSnapEnabled() ? L"是" : L"否");
    m_props->AddIntRow(L"元件总数", m_canvas->GetComponentCount());
    m_props->AddIntRow(L"导线总数", m_canvas->GetWireCount());
    m_props->AddIntRow(L"文本总数", m_canvas->GetTextCount());
    m_props->AddIntRow(L"网络总数", static_cast<int>(graph.nets.size()));

    m_props->End();
}

void MainFrame::SetHint(const wxString& text)
{
    SetStatusText(text, 0);
}

// ============================================================
//  画布回调
// ============================================================

void MainFrame::OnCanvasCursor(const wxPoint& world)
{
    m_cursorWorld = world;
    UpdateStatusBar();
}

void MainFrame::OnCanvasSelectionChanged()
{
    UpdateProperties();
    UpdateStatusBar();
}

void MainFrame::OnCanvasViewChanged()
{
    SyncViewState();
    UpdateProperties();
    UpdateStatusBar();
}

// ============================================================
//  文件操作（任务 5）
// ============================================================

void MainFrame::UpdateTitle()
{
    const wxString name = m_currentFile.IsEmpty()
                              ? wxString(L"Untitled")
                              : wxFileName(m_currentFile).GetFullName();

    SetTitle(wxString::Format(L"%s%s - Schematic Editor",
                              m_canvas->IsModified() ? L"*" : L"",
                              name));
}

// ------------------------------------------------------------
// 有未保存修改时先问一句；返回 false 表示用户取消了整个操作
// ------------------------------------------------------------
bool MainFrame::MaybeSave()
{
    if (!m_canvas->IsModified())
        return true;

    const int answer = wxMessageBox(L"当前原理图有未保存的修改，是否先保存？",
                                    L"Schematic Editor",
                                    wxYES_NO | wxCANCEL | wxICON_QUESTION,
                                    this);

    if (answer == wxCANCEL)
        return false;

    if (answer == wxYES)
        return DoSaveSchematic(false);

    return true;
}

// ------------------------------------------------------------
void MainFrame::DoNewSchematic()
{
    if (!MaybeSave())
        return;

    m_canvas->ResetDocument();      // 新文档：不算“已修改”，撤销历史也清空
    m_library->ClearSelection();

    m_currentFile.Clear();
    UpdateTitle();
    UpdateProperties();
    UpdateStatusBar();

    SetHint(L"已新建空白原理图");
}

// ------------------------------------------------------------
// 按路径打开（打开对话框与“最近打开”共用）
// ------------------------------------------------------------
bool MainFrame::OpenPath(const wxString& path)
{
    SchematicDocument doc;
    wxString          error;

    if (!SchematicIO::Load(path, doc, &error))
    {
        wxMessageBox(wxString::Format(L"打开失败：\n%s", error),
                     L"打开原理图", wxOK | wxICON_ERROR, this);
        return false;
    }

    m_canvas->SetDocument(doc.components, doc.wires, doc.texts,
                          doc.gridStep, doc.gridVisible, doc.snapEnabled, doc.zoom);

    m_library->ClearSelection();
    m_currentFile = path;

    AddRecentFile(path);
    UpdateTitle();
    SyncViewState();
    UpdateProperties();
    UpdateStatusBar();

    SetHint(wxString::Format(L"已打开 %s（%d 个元件、%d 条导线、%d 段文本、%d 个网络）",
                             wxFileName(path).GetFullName(),
                             m_canvas->GetComponentCount(),
                             m_canvas->GetWireCount(),
                             m_canvas->GetTextCount(),
                             static_cast<int>(m_canvas->GetNetGraph().nets.size())));

    return true;
}

bool MainFrame::DoOpenSchematic()
{
    if (!MaybeSave())
        return false;

    wxFileDialog dialog(this,
                        L"打开原理图",
                        wxEmptyString,
                        wxEmptyString,
                        SchematicIO::SchematicWildcard(),
                        wxFD_OPEN | wxFD_FILE_MUST_EXIST);

    if (dialog.ShowModal() != wxID_OK)
        return false;

    return OpenPath(dialog.GetPath());
}

// ------------------------------------------------------------
bool MainFrame::DoSaveSchematic(bool saveAs)
{
    wxString path = m_currentFile;

    if (saveAs || path.IsEmpty())
    {
        const wxString defaultName = path.IsEmpty()
                                         ? wxString(L"untitled.ses")
                                         : wxFileName(path).GetFullName();

        wxFileDialog dialog(this,
                            L"保存原理图",
                            wxEmptyString,
                            defaultName,
                            SchematicIO::SchematicWildcard(),
                            wxFD_SAVE | wxFD_OVERWRITE_PROMPT);

        if (dialog.ShowModal() != wxID_OK)
            return false;

        path = dialog.GetPath();
    }

    // 把画布当前状态打包成文档
    SchematicDocument doc;
    doc.components  = m_canvas->GetComponents();
    doc.wires       = m_canvas->GetWires();
    doc.texts       = m_canvas->GetTexts();
    doc.gridStep    = m_canvas->GetGridStep();
    doc.gridVisible = m_canvas->IsGridVisible();
    doc.snapEnabled = m_canvas->IsSnapEnabled();
    doc.zoom        = m_canvas->GetZoom();

    wxString error;

    if (!SchematicIO::Save(path, doc, &error))
    {
        wxMessageBox(wxString::Format(L"保存失败：\n%s", error),
                     L"保存原理图", wxOK | wxICON_ERROR, this);
        return false;
    }

    m_currentFile = path;
    m_canvas->MarkSaved();

    AddRecentFile(path);
    UpdateTitle();

    SetHint(wxString::Format(L"已保存到 %s（%d 个元件、%d 条导线、%d 个引脚）",
                             path,
                             static_cast<int>(doc.components.size()),
                             static_cast<int>(doc.wires.size()),
                             doc.PinCount()));

    return true;
}

// ------------------------------------------------------------
bool MainFrame::DoExportNetlist()
{
    if (m_canvas->GetComponentCount() == 0)
    {
        wxMessageBox(L"画布上还没有元件，没有可导出的网络表。",
                     L"导出网表", wxOK | wxICON_INFORMATION, this);
        return false;
    }

    const wxString defaultName = m_currentFile.IsEmpty()
                                     ? wxString(L"untitled.net")
                                     : wxFileName(m_currentFile).GetName() + L".net";

    wxFileDialog dialog(this,
                        L"导出网络表",
                        wxEmptyString,
                        defaultName,
                        L"标准网表 - Protel/Altium 格式 (*.net)|*.net|"
                        L"可读报告 - 网络清单+元件清单 (*.txt)|*.txt|"
                        L"所有文件 (*.*)|*.*",
                        wxFD_SAVE | wxFD_OVERWRITE_PROMPT);

    if (dialog.ShowModal() != wxID_OK)
        return false;

    // 按用户选的过滤器决定格式：0 = Protel 标准网表，1 = 可读报告
    const bool protel = (dialog.GetFilterIndex() == 0);

    const wxString content = protel ? m_canvas->BuildNetlistProtel()
                                    : m_canvas->BuildNetlist();

    wxString error;

    if (!SchematicIO::WriteTextFile(dialog.GetPath(), content, &error))
    {
        wxMessageBox(wxString::Format(L"导出失败：\n%s", error),
                     L"导出网表", wxOK | wxICON_ERROR, this);
        return false;
    }

    SetHint(wxString::Format(L"网络表已导出到 %s（%s）",
                             dialog.GetPath(),
                             protel ? L"Protel 标准格式" : L"可读报告"));

    wxMessageBox(wxString::Format(L"网络表已导出：\n%s\n\n格式：%s",
                                  dialog.GetPath(),
                                  protel ? L"Protel/Altium 标准网表（可被 PCB 软件加载）"
                                         : L"可读报告（含网络清单与元件清单）"),
                 L"导出成功", wxOK | wxICON_INFORMATION, this);

    return true;
}

// ------------------------------------------------------------
// 最近打开文件（本次会话内有效）
// ------------------------------------------------------------
void MainFrame::AddRecentFile(const wxString& path)
{
    m_recentFiles.erase(std::remove(m_recentFiles.begin(), m_recentFiles.end(), path),
                        m_recentFiles.end());

    m_recentFiles.insert(m_recentFiles.begin(), path);

    if (static_cast<int>(m_recentFiles.size()) > ID_FILE_RECENT_MAX)
        m_recentFiles.resize(ID_FILE_RECENT_MAX);

    RebuildRecentMenu();
}

void MainFrame::RebuildRecentMenu()
{
    if (!m_recentMenu)
        return;

    while (m_recentMenu->GetMenuItemCount() > 0)
        m_recentMenu->Destroy(m_recentMenu->FindItemByPosition(0));

    if (m_recentFiles.empty())
    {
        wxMenuItem* empty = m_recentMenu->Append(wxID_ANY, L"暂无最近文件");
        empty->Enable(false);
        return;
    }

    for (size_t i = 0; i < m_recentFiles.size(); ++i)
    {
        m_recentMenu->Append(ID_FILE_RECENT_BASE + static_cast<int>(i),
                             wxString::Format(L"&%d  %s",
                                              static_cast<int>(i) + 1,
                                              wxFileName(m_recentFiles[i]).GetFullName()));
    }
}

void MainFrame::OnRecentFile(wxCommandEvent& event)
{
    const int index = event.GetId() - ID_FILE_RECENT_BASE;

    if (index < 0 || index >= static_cast<int>(m_recentFiles.size()))
        return;

    const wxString path = m_recentFiles[index];

    if (!MaybeSave())
        return;

    if (!OpenPath(path))
    {
        // 打开失败（文件被删/被移动）就从列表里摘掉
        m_recentFiles.erase(m_recentFiles.begin() + index);
        RebuildRecentMenu();
    }
}

// ------------------------------------------------------------
void MainFrame::OnClose(wxCloseEvent& event)
{
    if (event.CanVeto() && !MaybeSave())
    {
        event.Veto();
        return;
    }

    Destroy();
}

// ============================================================
//  命令统一入口
// ============================================================

void MainFrame::OnCommand(wxCommandEvent& event)
{
    switch (event.GetId())
    {
        // ---------------- 文件 ----------------
        case wxID_NEW:
            DoNewSchematic();
            return;

        case wxID_OPEN:
            DoOpenSchematic();
            return;

        case wxID_SAVE:
            DoSaveSchematic(false);
            return;

        case ID_FILE_SAVE_AS:
            DoSaveSchematic(true);
            return;

        case ID_FILE_IMPORT:
            // 导入 = 打开一份 .ses（与“打开”同一套逻辑）
            DoOpenSchematic();
            return;

        case ID_FILE_EXPORT:
            DoExportNetlist();
            return;

        case ID_FILE_CLOSE_PROJECT:
        {
            if (!MaybeSave())
                return;

            m_canvas->ResetDocument();      // 关闭原理图：连撤销历史一起清掉
            m_library->ClearSelection();
            m_currentFile.Clear();

            UpdateTitle();
            UpdateProperties();
            UpdateStatusBar();
            SetHint(L"已关闭当前原理图");
            return;
        }

        case wxID_EXIT:
            Close();          // 交给 wxEVT_CLOSE_WINDOW 处理未保存提示
            return;

        case wxID_ABOUT:
        {
            wxAboutDialogInfo info;
            info.SetName(L"Schematic Editor");
            info.SetVersion(L"1.0");
            info.SetDescription(L"电路原理图编辑器\n基于 C++ 与 wxWidgets");
            info.SetCopyright(L"工业软件创新训练 I · 综合设计");
            wxAboutBox(info, this);
            return;
        }

        // ---------------- 编辑 ----------------
        case ID_EDIT_DELETE:
            m_canvas->DeleteSelection();
            return;

        case ID_EDIT_SELECT_ALL:
            m_canvas->SelectAll();
            return;

        case ID_EDIT_DESELECT_ALL:
            m_canvas->DeselectAll();
            return;

        case wxID_CUT:
            m_canvas->CutSelection();
            UpdateStatusBar();
            return;

        case wxID_COPY:
            m_canvas->CopySelection();
            UpdateStatusBar();
            return;

        case wxID_PASTE:
            m_canvas->PasteClipboard();
            UpdateStatusBar();
            return;

        case ID_EDIT_ROTATE_CW:
            m_canvas->RotateSelection(1);
            UpdateStatusBar();
            UpdateProperties();
            return;

        case ID_EDIT_ROTATE_CCW:
            m_canvas->RotateSelection(-1);
            UpdateStatusBar();
            UpdateProperties();
            return;

        case ID_EDIT_FLIP_H:
            m_canvas->FlipSelection(true);
            UpdateStatusBar();
            UpdateProperties();
            return;

        case ID_EDIT_FLIP_V:
            m_canvas->FlipSelection(false);
            UpdateStatusBar();
            UpdateProperties();
            return;

        case wxID_UNDO:
            if (m_canvas->CanUndo())
            {
                const wxString label = m_canvas->GetUndoLabel();
                m_canvas->Undo();
                SetHint(wxString::Format(L"已撤销：%s", label));
            }
            else
            {
                SetHint(L"没有可撤销的操作");
            }
            return;

        case ID_EDIT_REDO:
            if (m_canvas->CanRedo())
            {
                const wxString label = m_canvas->GetRedoLabel();
                m_canvas->Redo();
                SetHint(wxString::Format(L"已重做：%s", label));
            }
            else
            {
                SetHint(L"没有可重做的操作");
            }
            return;

        // ---------------- 视图 ----------------
        case ID_VIEW_ZOOM_IN:
            m_canvas->ZoomIn();
            return;

        case ID_VIEW_ZOOM_OUT:
            m_canvas->ZoomOut();
            return;

        case ID_VIEW_ZOOM_FIT:
            m_canvas->ZoomFit();
            SetHint(L"已适合窗口");
            return;

        case ID_VIEW_ZOOM_100:
            m_canvas->ZoomActual();
            SetHint(L"缩放已重置为 100%");
            return;

        case ID_VIEW_GRID:
            m_canvas->SetGridVisible(event.IsChecked());
            SyncViewState();
            SetHint(event.IsChecked() ? L"已显示网格" : L"已隐藏网格");
            return;

        case ID_VIEW_SNAP_GRID:
            m_canvas->SetSnapEnabled(event.IsChecked());
            SyncViewState();
            SetHint(event.IsChecked() ? L"已开启网格吸附" : L"已关闭网格吸附");
            return;

        case ID_VIEW_CROSSHAIR:
            m_canvas->SetCrosshairVisible(event.IsChecked());
            SyncViewState();
            SetHint(event.IsChecked() ? L"已显示十字光标" : L"已隐藏十字光标");
            return;

        case ID_VIEW_FULLSCREEN:
            ShowFullScreen(event.IsChecked());
            return;

        // ---------------- 工具 ----------------
        case ID_TOOL_SELECT:
            m_canvas->SetTool(SchematicCanvas::ToolSelect);
            UpdateStatusBar();
            return;

        case ID_TOOL_WIRE:
            m_canvas->SetTool(SchematicCanvas::ToolWire);
            UpdateStatusBar();
            SetHint(L"导线工具：单击放第一个点，再次单击放折点（自动走正交折线，"
                    L"靠近引脚/导线端点会自动吸附）；右键 / Esc / 回车结束");
            return;

        case ID_TOOL_TEXT:
            m_canvas->SetTool(SchematicCanvas::ToolText);
            UpdateStatusBar();
            SetHint(L"文本工具：在画布上单击，输入要放置的文字");
            return;

        // ---------------- 放置菜单里的导线 / 文本 ----------------
        case ID_PLACE_WIRE:
            m_canvas->SetTool(SchematicCanvas::ToolWire);
            if (m_toolBar)
                m_toolBar->ToggleTool(ID_TOOL_WIRE, true);
            UpdateStatusBar();
            SetHint(L"已切到导线工具：单击放点，右键 / Esc 结束");
            return;

        case ID_PLACE_TEXT:
            m_canvas->SetTool(SchematicCanvas::ToolText);
            if (m_toolBar)
                m_toolBar->ToggleTool(ID_TOOL_TEXT, true);
            UpdateStatusBar();
            SetHint(L"已切到文本工具：在画布上单击，输入要放置的文字");
            return;

        // ---------------- 网表 / 检查 ----------------
        case ID_INSPECT_NETLIST:
        {
            const wxString text = m_canvas->BuildNetlist();

            TextReportDialog dialog(this,
                                    L"电路网络表 (Netlist)",
                                    text,
                                    true,
                                    L"netlist.net");
            dialog.ShowModal();
            SetHint(L"已生成网络表（可在对话框中保存为文件）");
            return;
        }

        case ID_TOOLS_GENERATE_NETLIST:
            DoExportNetlist();
            return;

        case ID_INSPECT_ERC:
        {
            const wxString text = m_canvas->BuildErcReport();

            TextReportDialog dialog(this, L"电气规则检查 (ERC)", text, true, L"erc.txt");
            dialog.ShowModal();
            SetHint(L"电气规则检查完成");
            return;
        }

        case ID_INSPECT_STATISTICS:
        {
            const NetGraph& graph = m_canvas->GetNetGraph();

            int pinTotal = 0;
            int floating = 0;

            for (const ComponentInstance& comp : m_canvas->GetComponents())
                pinTotal += static_cast<int>(comp.pins.size());

            for (const Net& net : graph.nets)
            {
                if (net.pins.size() < 2)
                    ++floating;
            }

            wxString text;
            text << L"原理图统计\n";
            text << L"========================\n\n";
            text << wxString::Format(L"元件数量   : %d\n", m_canvas->GetComponentCount());
            text << wxString::Format(L"引脚总数   : %d\n", pinTotal);
            text << wxString::Format(L"导线数量   : %d\n", m_canvas->GetWireCount());
            text << wxString::Format(L"连接点数量 : %d\n", static_cast<int>(graph.junctions.size()));
            text << wxString::Format(L"网络数量   : %d\n", static_cast<int>(graph.nets.size()));
            text << wxString::Format(L"孤立网络   : %d\n", floating);

            TextReportDialog dialog(this, L"原理图统计", text, true, L"statistics.txt");
            dialog.ShowModal();
            return;
        }

        // ---------------- 其它 ----------------
        case ID_VIEW_RESET:
            m_canvas->ClearAll(true);      // 用户主动清空 → 算修改
            m_library->ClearSelection();
            UpdateProperties();
            UpdateStatusBar();
            SetHint(L"已清空画布");
            return;

        // ---------------- 电路仿真（任务 6）----------------
        case ID_SIM_RUN:
        {
            if (m_canvas->IsSimulating())
            {
                m_canvas->StopSimulation();
                SetHint(L"仿真已停止（导线恢复普通着色）");
            }
            else
            {
                if (m_canvas->GetComponentCount() == 0)
                {
                    wxMessageBox(L"画布上还没有元件，没有可仿真的电路。",
                                 L"电路仿真", wxOK | wxICON_INFORMATION, this);
                    return;
                }

                m_canvas->StartSimulation();
            }

            if (m_toolBar)
                m_toolBar->ToggleTool(ID_SIM_RUN, m_canvas->IsSimulating());

            UpdateStatusBar();
            return;
        }

        case ID_TOOLS_SIMULATOR:
        {
            // 打开仿真面板：先启动（如果还没启动），再显示报告
            if (!m_canvas->IsSimulating())
            {
                if (m_canvas->GetComponentCount() == 0)
                {
                    wxMessageBox(L"画布上还没有元件，没有可仿真的电路。",
                                 L"电路仿真", wxOK | wxICON_INFORMATION, this);
                    return;
                }

                m_canvas->StartSimulation();

                if (m_toolBar)
                    m_toolBar->ToggleTool(ID_SIM_RUN, true);
            }

            TextReportDialog dialog(this,
                                    L"电路仿真器（仿真进行中）",
                                    m_canvas->BuildSimulationReport(),
                                    true,
                                    L"simulation.txt");
            dialog.ShowModal();

            UpdateStatusBar();
            return;
        }

        case ID_SIM_TICK:
            m_canvas->TickClock();
            UpdateStatusBar();
            return;

        // ---------------- 元件库（任务 3）----------------
        case ID_LIBRARY_MANAGE:
        {
            ComponentEditorDialog dialog(this);

            if (dialog.ShowModal() == wxID_OK)
            {
                // 保存后 XML 已重写、内存里的元件表也重建过了；
                // 树里的指针全都失效，必须整体重建（这里会顺带清掉待放置元件）
                m_library->Reload();
                UpdateProperties();
                UpdateStatusBar();

                SetHint(wxString::Format(L"元件库已更新：共 %d 个元件（其中自定义 %d 个）",
                                         ComponentCatalog::ComponentCount(),
                                         ComponentCatalog::UserComponentCount()));
            }

            return;
        }

        case ID_LIBRARY_RELOAD:
        {
            wxString error;

            if (!ComponentCatalog::Reload(&error))
            {
                wxMessageBox(error, L"重新加载元件库失败",
                             wxOK | wxICON_ERROR, this);
                return;
            }

            m_library->Reload();
            UpdateProperties();
            UpdateStatusBar();

            SetHint(wxString::Format(
                L"已重新加载元件库：共 %d 个元件（其中自定义 %d 个，文件 %s）",
                ComponentCatalog::ComponentCount(),
                ComponentCatalog::UserComponentCount(),
                ComponentCatalog::GetLibraryPath()));

            return;
        }

        case ID_INSPECT_PROPERTIES:
            SetHint(L"属性面板已在右侧常驻显示");
            return;

        default:
            break;
    }

    // ---------------- 尚未实现的菜单项 ----------------
    wxString label;

    if (wxMenuBar* bar = GetMenuBar())
    {
        if (wxMenuItem* item = bar->FindItem(event.GetId()))
            label = item->GetItemLabelText();
    }

    if (label.IsEmpty())
        label = wxString::Format(L"ID %d", event.GetId());

    SetHint(wxString::Format(L"「%s」尚未实现（TODO）", label));
}

} // namespace se


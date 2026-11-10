// ============================================================
//  Ids.h  —— 命令 ID 集中定义
//
//  说明：
//    * 前一段沿用小组成员已经写好的菜单 ID 编号（保持连续、不重排）；
//    * 后一段是本次新增的“工具栏 / 面板”专用 ID，全部排在
//      ID_HELP_SHORTCUTS 之后，这样菜单兜底绑定区间
//      [wxID_NEW, ID_HELP_SHORTCUTS] 不会把它们一并吃掉。
// ============================================================

#pragma once

#include <wx/defs.h>

namespace se
{

enum
{
    // --------------------------------------------------------
    // 文件
    // --------------------------------------------------------
    ID_FILE_SAVE_AS = wxID_HIGHEST + 1,
    ID_FILE_CLOSE_PROJECT,
    ID_FILE_IMPORT,
    ID_FILE_EXPORT,
    ID_FILE_IMPORT_KICAD,

    // --------------------------------------------------------
    // 编辑
    // --------------------------------------------------------
    ID_EDIT_REDO,
    ID_EDIT_DELETE,
    ID_EDIT_SELECT_ALL,
    ID_EDIT_DESELECT_ALL,

    // --------------------------------------------------------
    // 视图
    // --------------------------------------------------------
    ID_VIEW_ZOOM_IN,
    ID_VIEW_ZOOM_OUT,
    ID_VIEW_ZOOM_FIT,
    ID_VIEW_ZOOM_100,
    ID_VIEW_GRID,
    ID_VIEW_SNAP_GRID,
    ID_VIEW_CROSSHAIR,
    ID_VIEW_FULLSCREEN,

    // --------------------------------------------------------
    // 放置
    // --------------------------------------------------------
    ID_PLACE_COMPONENT,
    ID_PLACE_RESISTOR,
    ID_PLACE_CAPACITOR,
    ID_PLACE_INDUCTOR,
    ID_PLACE_DIODE,
    ID_PLACE_LED,
    ID_PLACE_OPAMP,
    ID_PLACE_NPN,
    ID_PLACE_PNP,

    ID_PLACE_WIRE,
    ID_PLACE_BUS,
    ID_PLACE_JUNCTION,
    ID_PLACE_NET_LABEL,
    ID_PLACE_GLOBAL_LABEL,
    ID_PLACE_POWER,
    ID_PLACE_GND,
    ID_PLACE_NOCONNECT,
    ID_PLACE_TEXT,

    // --------------------------------------------------------
    // 检查
    // --------------------------------------------------------
    ID_INSPECT_PROPERTIES,
    ID_INSPECT_ERC,
    ID_INSPECT_NETLIST,
    ID_INSPECT_STATISTICS,

    // --------------------------------------------------------
    // 工具
    // --------------------------------------------------------
    ID_TOOLS_ANNOTATE,
    ID_TOOLS_ASSIGN_FOOTPRINTS,
    ID_TOOLS_GENERATE_NETLIST,
    ID_TOOLS_GENERATE_BOM,
    ID_TOOLS_SIMULATOR,

    // --------------------------------------------------------
    // 首选项
    // --------------------------------------------------------
    ID_PREF_GENERAL,
    ID_PREF_GRID,
    ID_PREF_HOTKEYS,
    ID_PREF_PATHS,

    // --------------------------------------------------------
    // 帮助
    // --------------------------------------------------------
    ID_HELP_DOCUMENTATION,
    ID_HELP_SHORTCUTS,

    // ========================================================
    // 以下为新增：工具栏专用 ID（不在菜单兜底区间内）
    // ========================================================

    ID_TOOL_SELECT,
    ID_TOOL_WIRE,
    ID_TOOL_TEXT,
    ID_VIEW_RESET,
    ID_SIM_RUN,
    ID_SIM_TICK,        // 时钟单步（仿真中，等价于按空格）
    ID_SIM_STOP,

    // 面板
    ID_LIBRARY_TREE,
    ID_PROPERTY_GRID,

    // --------------------------------------------------------
    // 元件库（任务 3 补完：用户自定义元件）
    // --------------------------------------------------------
    ID_LIBRARY_MANAGE,     // 工具 → 元件库管理...
    ID_LIBRARY_RELOAD,     // 工具 → 重新加载元件库

    // --------------------------------------------------------
    // 编辑：旋转 / 翻转（任务 4 补完）
    // --------------------------------------------------------
    ID_EDIT_ROTATE_CW,     // 顺时针 90°      Ctrl+R
    ID_EDIT_ROTATE_CCW,    // 逆时针 90°      Ctrl+Shift+R
    ID_EDIT_FLIP_H,        // 水平翻转（左右）Ctrl+H
    ID_EDIT_FLIP_V,        // 垂直翻转（上下）Ctrl+Shift+H

    // --------------------------------------------------------
    // 兜底绑定区间的上界
    //   MainFrame 用 [wxID_LOWEST, ID_COMMAND_END] 一次绑完所有菜单/工具栏命令，
    //   所以它必须是本枚举里最后一个“命令型”ID。
    // --------------------------------------------------------
    ID_COMMAND_END

    // 说明：ID_FILE_RECENT_BASE 定义在下面的枚举里（见文件末尾），
    //       它单独绑定，不在上面的菜单兜底区间内。
};

// ------------------------------------------------------------
// 最近打开文件（单独绑定，不参与菜单兜底区间）
// ------------------------------------------------------------
enum
{
    ID_FILE_RECENT_BASE = ID_COMMAND_END + 1,
    ID_FILE_RECENT_MAX  = 8
};

} // namespace se

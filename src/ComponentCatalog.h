// ============================================================
//  ComponentCatalog.h  —— 元件库数据模型
//
//  这是“任务 3：元件库模块”的核心模型：
//    * 分类（Category）→ 元件（ComponentDef）两级结构，
//      与元件库面板的树形控件一一对应；
//    * ComponentKind 决定引脚怎么摆（逻辑门 / 无源 / 电源 / 端口）；
//    * 数据源是“内置清单 + 用户自定义 XML”两份合并出来的：
//        内置部分写在 ComponentCatalog.cpp 里（开箱即用）；
//        用户部分从程序目录下的 components.xml 读，用户可以自己加元件、
//        也可以同名覆盖内置元件的字段（比如把电阻的默认值改成 1k）。
//    * 合并结果缓存在 Categories() 里，重载后旧指针会失效，
//      所以调用方（元件库面板）必须跟着刷新——见 LibraryPanel::Reload()。
// ============================================================

#pragma once

#include <wx/string.h>

#include <vector>

#include "NetModel.h"

namespace se
{

// ------------------------------------------------------------
// 元件定义
// ------------------------------------------------------------
struct ComponentDef
{
    wxString      name;          // 显示名，例如 “与门”
    wxString      refPrefix;     // 位号前缀，例如 “U” → U1 / U2
    wxString      description;   // 说明文字
    int           inputs  = 0;   // 输入引脚数
    int           outputs = 0;   // 输出引脚数
    ComponentKind kind    = ComponentKind::Logic;  // 电气形态（决定引脚摆法）
    int           w       = 60;  // 元件框宽（世界坐标）
    int           h       = 40;  // 元件框高

    // ---- PCB 网表需要 ----
    wxString      footprint;     // 默认封装名（空 = 非物理元件，如电源符号/网络标签）
    wxString      value;         // 默认元件值 / 型号

    // ---- 来源 ----
    bool          userDefined = false;  // true = 来自用户 XML（界面用星号标出）
};

// ------------------------------------------------------------
// 分类
// ------------------------------------------------------------
struct CategoryDef
{
    wxString                  name;
    std::vector<ComponentDef> items;
};

// ------------------------------------------------------------
// 元件目录
// ------------------------------------------------------------
class ComponentCatalog
{
public:
    static bool ValidateDefinition(const ComponentDef& def, wxString* error = nullptr);

    // 内置 + 用户自定义合并后的分类表（静态缓存）
    static const std::vector<CategoryDef>& Categories();

    // 只有用户自定义的那部分（写回 XML 时用，保持用户原来的分类结构）
    static const std::vector<CategoryDef>& UserCategories();

    // 按名称查找元件，找不到返回 nullptr
    static const ComponentDef* Find(const wxString& name);

    // 元件总数（内置 + 用户）
    static int ComponentCount();
    static int UserComponentCount();

    // ========================================================
    //  用户自定义元件（XML 数据源）
    // ========================================================

    // 元件库文件路径：程序所在目录下的 components.xml
    static wxString GetLibraryPath();
    static void     SetLibraryPath(const wxString& path);

    // 首次运行时写一份带示例的模板文件（文件已存在则什么都不做）
    static bool EnsureUserLibrary(wxString* error = nullptr);

    // 重新从 XML 读一遍并重建合并表（失败时保持旧数据不变）
    static bool Reload(wxString* error = nullptr);

    // 直接把一份分类表写成 XML（元件库管理对话框用）
    static bool SaveUserLibrary(const std::vector<CategoryDef>& categories,
                                wxString* error = nullptr);

    // 形态枚举 ↔ XML 里的文本
    static wxString      KindToText(ComponentKind kind);
    static ComponentKind TextToKind(const wxString& text);
    static wxString      KindToChinese(ComponentKind kind);

    // 用户库文件的路径是否是程序自动创建的默认位置
    static bool IsDefaultLibraryPath();

private:
    static void LoadBuiltinIfNeeded();
    static void RebuildMerged();
};

} // namespace se

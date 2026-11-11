#include "ComponentCatalog.h"
#include "DigitalSimulator.h"
#include <wx/wfstream.h>

#include <wx/filefn.h>
#include <wx/filename.h>
#include <wx/stdpaths.h>
#include <wx/xml/xml.h>
#include <set>

namespace se
{

namespace
{

// 内置元件库（写死在代码里，保证“拷走 exe 就能用”）
const std::vector<CategoryDef>& BuiltinData();

// 合并结果缓存
std::vector<CategoryDef> g_builtin;
std::vector<CategoryDef> g_user;
std::vector<CategoryDef> g_merged;

wxString g_libraryPath;
bool     g_inited = false;

// ------------------------------------------------------------
// 小工具
// ------------------------------------------------------------
int GetIntAttr(wxXmlNode* node, const wxString& key, int fallback)
{
    const wxString text = node->GetAttribute(key);

    long value = 0;

    if (!text.IsEmpty() && text.ToLong(&value))
        return static_cast<int>(value);

    return fallback;
}

void SetError(wxString* error, const wxString& message)
{
    if (error)
        *error = message;
}

// 在分类表里按名称定位元件
bool Locate(std::vector<CategoryDef>& cats, const wxString& name, int& catIndex, int& itemIndex)
{
    for (size_t c = 0; c < cats.size(); ++c)
    {
        for (size_t i = 0; i < cats[c].items.size(); ++i)
        {
            if (cats[c].items[i].name == name)
            {
                catIndex = static_cast<int>(c);
                itemIndex = static_cast<int>(i);
                return true;
            }
        }
    }

    return false;
}

// 用户库模板：第一次运行时自动生成，顺便把字段含义写在注释里
std::vector<CategoryDef> MakeLibraryTemplate()
{
    std::vector<CategoryDef> cats;

    CategoryDef mine;
    mine.name = L"用户自定义";

    mine.items.push_back({ L"三输入与门", L"U", L"Y = A · B · C（示例：自定义元件）",
                           3, 1, ComponentKind::Logic, 70, 46, L"DIP-14", L"AND3" });

    mine.items.push_back({ L"四输入或门", L"U", L"Y = A + B + C + D（示例：自定义元件）",
                           4, 1, ComponentKind::Logic, 74, 54, L"DIP-14", L"OR4" });

    cats.push_back(mine);

    return cats;
}

// ------------------------------------------------------------
// 解析元件库 XML
// ------------------------------------------------------------
bool ParseLibraryFile(const wxString& path, std::vector<CategoryDef>& out, wxString* error)
{
    if (!wxFileExists(path))
    {
        SetError(error, wxString::Format(L"元件库文件不存在：%s", path));
        return false;
    }

    wxXmlDocument doc;

    if (!doc.Load(path))
    {
        SetError(error, wxString::Format(L"无法解析元件库文件（XML 格式有误）：%s", path));
        return false;
    }

    wxXmlNode* root = doc.GetRoot();

    if (!root)
    {
        SetError(error, L"元件库文件是空的");
        return false;
    }

    if (root->GetName() != "ComponentLibrary" || root->GetAttribute("version", "1") != "1") {
        SetError(error, L"元件库根节点或版本不受支持"); return false;
    }
    std::vector<CategoryDef> cats;

    for (wxXmlNode* catNode = root->GetChildren(); catNode; catNode = catNode->GetNext())
    {
        if (catNode->GetType() != wxXML_ELEMENT_NODE)
            continue;

        if (catNode->GetName() != L"Category")
            continue;

        CategoryDef category;
        category.name = catNode->GetAttribute(L"name", L"用户自定义");

        for (wxXmlNode* node = catNode->GetChildren(); node; node = node->GetNext())
        {
            if (node->GetType() != wxXML_ELEMENT_NODE)
                continue;

            if (node->GetName() != L"Component")
                continue;

            ComponentDef def;

            def.name = node->GetAttribute(L"name");

            if (def.name.IsEmpty())
                continue;   // 没名字的条目直接跳过，不因为一条坏数据丢掉整个库

            def.refPrefix   = node->GetAttribute(L"refPrefix", L"U");
            def.description = node->GetAttribute(L"description");
            def.inputs      = GetIntAttr(node, L"inputs", 0);
            def.outputs     = GetIntAttr(node, L"outputs", 0);
            def.kind        = ComponentCatalog::TextToKind(node->GetAttribute(L"kind", L"Logic"));
            def.w           = GetIntAttr(node, L"width", 60);
            def.h           = GetIntAttr(node, L"height", 40);
            def.footprint   = node->GetAttribute(L"footprint");
            def.value       = node->GetAttribute(L"value");
            def.userDefined = true;
            def.logicModel = node->GetAttribute("logicModel");
            def.truthTable = node->GetAttribute("truthTable");
            def.delayTicks = GetIntAttr(node, "delayTicks", 1);
            def.clockPeriodTicks = GetIntAttr(node, "clockPeriodTicks", 10);

            // 夹得太小的元件框在画布上会挤成一团，兜一下底
            if (def.w < 24)
                def.w = 24;
            if (def.h < 20)
                def.h = 20;

            if (!ComponentCatalog::ValidateDefinition(def, error)) return false;
            category.items.push_back(def);
        }

        if (!category.items.empty())
            cats.push_back(category);
    }

    out.swap(cats);
    return true;
}

// ------------------------------------------------------------
// 内置元件库
//
// 字段顺序：name, refPrefix, description, inputs, outputs,
//           kind, w, h, footprint, value
//
// footprint 为空的元件表示"不是物理元件"（电源符号、网络标签），
// 它们只参与网络命名，不会写进 PCB 网表的元件表。
// ------------------------------------------------------------
const std::vector<CategoryDef>& BuiltinData()
{
    static const std::vector<CategoryDef> kCategories =
    {
        {
            L"数字逻辑",
            {
                { L"与门",   L"U", L"两输入与门，Y = A · B",        2, 1, ComponentKind::Logic, 60, 40, L"DIP-14", L"AND2" },
                { L"或门",   L"U", L"两输入或门，Y = A + B",        2, 1, ComponentKind::Logic, 60, 40, L"DIP-14", L"OR2" },
                { L"非门",   L"U", L"反相器，Y = /A",               1, 1, ComponentKind::Logic, 55, 40, L"DIP-14", L"NOT" },
                { L"与非门", L"U", L"两输入与非门，Y = /(A · B)",   2, 1, ComponentKind::Logic, 60, 40, L"DIP-14", L"NAND2" },
                { L"或非门", L"U", L"两输入或非门，Y = /(A + B)",   2, 1, ComponentKind::Logic, 60, 40, L"DIP-14", L"NOR2" },
                { L"异或门", L"U", L"两输入异或门，Y = A ⊕ B",      2, 1, ComponentKind::Logic, 60, 40, L"DIP-14", L"XOR2" },
                { L"同或门", L"U", L"两输入同或门，Y = /(A ⊕ B)",   2, 1, ComponentKind::Logic, 60, 40, L"DIP-14", L"XNOR2" },
                { L"半加器", L"U", L"半加器：A/B 输入，S 和、C 进位", 2, 2, ComponentKind::Logic, 64, 46, L"DIP-14", L"HA" },
                { L"全加器", L"U", L"全加器：A/B/Cin，S 和、C 进位", 3, 2, ComponentKind::Logic, 68, 52, L"DIP-16", L"FA" },
            }
        },
        {
            L"时序逻辑",
            {
                // 这一组的元件"有记忆"：输出不只取决于当前输入，
                // 还取决于时钟上一个上升沿时存进来的值。
                // logicModel 显式写出来，这样元件改名了也不会丢失行为。
                { L"D 触发器",   L"U", L"D 触发器：CLK 上升沿采样 D，输出 Q / QN", 2, 2, ComponentKind::Logic,  70,  60, L"DIP-14", L"DFF",     false, L"dff" },
                { L"JK 触发器",  L"U", L"JK 触发器：J 置位、K 复位、J=K=1 翻转",   3, 2, ComponentKind::Logic,  80,  60, L"DIP-14", L"JKFF",    false, L"jkff" },
                { L"4 位计数器", L"U", L"4 位二进制计数器：CLK 上升沿加 1，RST 清零", 2, 4, ComponentKind::Logic,  90, 100, L"DIP-16", L"CNT4",    false, L"counter" },
                { L"ROM 16×4",   L"U", L"16×4 只读存储器：A0~A3 选址，D0~D3 输出", 4, 4, ComponentKind::Logic,  90, 100, L"DIP-16", L"ROM16X4", false, L"rom" },
                { L"RAM 16×4",   L"U", L"16×4 读写存储器：WE 上升沿写入，读为异步", 9, 4, ComponentKind::Logic, 110, 200, L"DIP-24", L"RAM16X4", false, L"ram" },
            }
        },
        {
            L"输入输出",
            {
                { L"输入引脚", L"IN",  L"电路输入端口",          0, 1, ComponentKind::Port, 46, 34, L"HDR-1X1", L"IN" },
                { L"输出引脚", L"OUT", L"电路输出端口",          1, 0, ComponentKind::Port, 46, 34, L"HDR-1X1", L"OUT" },
                { L"时钟",     L"CLK", L"时钟信号源",            0, 1, ComponentKind::Port, 46, 34, L"HDR-1X1", L"CLK" },
                { L"常量",     L"V",   L"逻辑常量 0 / 1",        0, 1, ComponentKind::Port, 46, 34, L"HDR-1X1", L"CONST" },
                { L"探针",     L"P",   L"观察某条网络的逻辑值",  1, 1, ComponentKind::Port, 46, 34, L"TP-1X1",  L"PROBE" },
            }
        },
        {
            L"无源器件",
            {
                { L"电阻", L"R", L"理想电阻（两端器件）", 1, 1, ComponentKind::Passive, 34, 60, L"0805", L"10k" },
                { L"电容", L"C", L"理想电容（两端器件）", 1, 1, ComponentKind::Passive, 34, 60, L"0603", L"100nF" },
                { L"电感", L"L", L"理想电感（两端器件）", 1, 1, ComponentKind::Passive, 34, 60, L"0805", L"10uH" },
            }
        },
        {
            L"半导体器件",
            {
                { L"二极管",     L"D", L"PN 结二极管", 1, 1, ComponentKind::Passive, 34, 60, L"SOD-123", L"1N4148" },
                { L"LED",        L"D", L"发光二极管",  1, 1, ComponentKind::Passive, 34, 60, L"0805",    L"LED-Red" },
                { L"NPN 晶体管", L"Q", L"NPN 三极管：B 输入，C/E 输出", 1, 2, ComponentKind::Logic, 60, 50, L"TO-92", L"2N3904" },
                { L"PNP 晶体管", L"Q", L"PNP 三极管：B 输入，C/E 输出", 1, 2, ComponentKind::Logic, 60, 50, L"TO-92", L"2N3906" },
            }
        },
        {
            L"模拟器件",
            {
                { L"运算放大器", L"U", L"理想运放：两输入一输出", 2, 1, ComponentKind::Logic, 62, 46, L"SOIC-8", L"LM358" },
            }
        },
        {
            L"电源与地",
            {
                // 电源符号不是物理元件 → footprint 留空，只给网络起名
                { L"VCC", L"#PWR", L"电源正极", 0, 1, ComponentKind::Power, 36, 34, L"", L"VCC" },
                { L"GND", L"#PWR", L"电源地",   0, 1, ComponentKind::Power, 36, 34, L"", L"GND" },
            }
        },
        {
            L"连线与标签",
            {
                // 标签也不是物理元件 → footprint 留空
                { L"网络标签", L"N", L"局部网络命名（贴在导线上）", 1, 1, ComponentKind::Port, 46, 34, L"", L"NETLABEL" },
                { L"全局标签", L"G", L"跨页全局网络命名",           1, 1, ComponentKind::Port, 46, 34, L"", L"GLOBLABEL" },
            }
        },
    };

    return kCategories;
}

} // namespace

// ============================================================
//  形态枚举 ↔ 文本
// ============================================================

wxString ComponentCatalog::KindToText(ComponentKind kind)
{
    switch (kind)
    {
        case ComponentKind::Passive: return L"Passive";
        case ComponentKind::Power:   return L"Power";
        case ComponentKind::Port:    return L"Port";
        case ComponentKind::Logic:
        default:                     return L"Logic";
    }
}

ComponentKind ComponentCatalog::TextToKind(const wxString& text)
{
    wxString key = text;
    key.Trim().MakeLower();

    if (key == L"passive" || key == L"无源" || key == L"无源器件")
        return ComponentKind::Passive;

    if (key == L"power" || key == L"电源" || key == L"电源与地")
        return ComponentKind::Power;

    if (key == L"port" || key == L"端口" || key == L"输入输出")
        return ComponentKind::Port;

    return ComponentKind::Logic;
}

wxString ComponentCatalog::KindToChinese(ComponentKind kind)
{
    switch (kind)
    {
        case ComponentKind::Passive: return L"无源器件";
        case ComponentKind::Power:   return L"电源/地";
        case ComponentKind::Port:    return L"端口/标签";
        case ComponentKind::Logic:
        default:                     return L"逻辑器件";
    }
}

// ============================================================
//  文件路径
// ============================================================

wxString ComponentCatalog::GetLibraryPath()
{
    if (!g_libraryPath.IsEmpty())
        return g_libraryPath;

    wxFileName exe(wxStandardPaths::Get().GetExecutablePath());
    exe.SetFullName(L"components.xml");

    return exe.GetFullPath();
}

void ComponentCatalog::SetLibraryPath(const wxString& path)
{
    g_libraryPath = path;
    g_inited      = false;
    g_user.clear();
    g_merged.clear();
}

bool ComponentCatalog::IsDefaultLibraryPath()
{
    wxFileName exe(wxStandardPaths::Get().GetExecutablePath());
    exe.SetFullName(L"components.xml");

    return GetLibraryPath() == exe.GetFullPath();
}

// ============================================================
//  载入 / 保存
// ============================================================

bool ComponentCatalog::EnsureUserLibrary(wxString* error)
{
    const wxString path = GetLibraryPath();

    if (wxFileExists(path))
        return true;

    // 第一次运行：写一份带示例的模板，用户照着改就行
    return SaveUserLibrary(MakeLibraryTemplate(), error);
}

bool ComponentCatalog::SaveUserLibrary(const std::vector<CategoryDef>& categories,
                                       wxString* error)
{
    for (const auto& category : categories)
        for (const auto& def : category.items)
            if (!ValidateDefinition(def, error)) return false;

    const wxString path = GetLibraryPath();

    // 默认就按 UTF-8 写（<?xml version="1.0" encoding="UTF-8"?>），中文不会乱码
    wxXmlDocument doc;

    wxXmlNode* root = new wxXmlNode(wxXML_ELEMENT_NODE, L"ComponentLibrary");
    root->AddAttribute(L"version", L"1");
    root->AddAttribute(L"note", L"用户自定义元件库；保存后用「工具 → 重新加载元件库」生效");

    root->AddChild(new wxXmlNode(wxXML_COMMENT_NODE, wxEmptyString,
        L"\n  每个 <Component> 的字段：\n"
        L"    name        元件显示名（也是查找用的键；与内置元件同名 = 覆盖内置元件的字段）\n"
        L"    refPrefix   位号前缀，例如 U → U1 / U2\n"
        L"    description 说明文字（元件库提示与属性表里显示）\n"
        L"    inputs      输入引脚数（决定引脚名 A/B/C…）\n"
        L"    outputs     输出引脚数（决定引脚名 Y / Y1 / Y2…）\n"
        L"    kind        电气形态：Logic（左入右出）/ Passive（左右两端）/\n"
        L"                Power（底部单脚）/ Port（端口或标签）\n"
        L"    width/height 元件框尺寸（世界坐标，建议 10 的倍数）\n"
        L"    footprint   默认封装名，留空表示非物理元件（不进 PCB 网表）\n"
        L"    value       默认元件值 / 型号\n"));

    for (const CategoryDef& category : categories)
    {
        wxXmlNode* catNode = new wxXmlNode(wxXML_ELEMENT_NODE, L"Category");
        catNode->AddAttribute(L"name", category.name);

        for (const ComponentDef& def : category.items)
        {
            wxXmlNode* node = new wxXmlNode(wxXML_ELEMENT_NODE, L"Component");

            node->AddAttribute(L"name",        def.name);
            node->AddAttribute(L"refPrefix",   def.refPrefix);
            node->AddAttribute(L"description", def.description);
            node->AddAttribute(L"inputs",      wxString::Format(L"%d", def.inputs));
            node->AddAttribute(L"outputs",     wxString::Format(L"%d", def.outputs));
            node->AddAttribute(L"kind",        KindToText(def.kind));
            node->AddAttribute(L"width",       wxString::Format(L"%d", def.w));
            node->AddAttribute(L"height",      wxString::Format(L"%d", def.h));
            node->AddAttribute(L"footprint",   def.footprint);
            node->AddAttribute(L"value",       def.value);
            node->AddAttribute("logicModel", def.logicModel);
            node->AddAttribute("truthTable", def.truthTable);
            node->AddAttribute("delayTicks", wxString::Format("%d", def.delayTicks));
            node->AddAttribute("clockPeriodTicks", wxString::Format("%d", def.clockPeriodTicks));

            catNode->AddChild(node);
        }

        root->AddChild(catNode);
    }

    doc.SetRoot(root);

    wxTempFileOutputStream output(path);
    if (!output.IsOk() || !doc.Save(output) || !output.Commit())
    {
        SetError(error, wxString::Format(L"无法写入元件库文件：%s", path));
        return false;
    }

    return true;
}

bool ComponentCatalog::Reload(wxString* error)
{
    LoadBuiltinIfNeeded();

    std::vector<CategoryDef> loaded;

    const wxString path = GetLibraryPath();

    if (wxFileExists(path))
    {
        wxString parseError;

        if (!ParseLibraryFile(path, loaded, &parseError))
        {
            // 解析失败：保留上一次的数据，只把错误抛给调用方
            SetError(error, parseError);
            // Keep both the old definitions and their UI pointers unchanged.
            g_inited = true;
            return false;
        }
    }

    g_user = loaded;
    RebuildMerged();
    g_inited = true;

    return true;
}

void ComponentCatalog::LoadBuiltinIfNeeded()
{
    if (g_builtin.empty())
        g_builtin = BuiltinData();
}

void ComponentCatalog::RebuildMerged()
{
    LoadBuiltinIfNeeded();

    g_merged = g_builtin;

    for (const CategoryDef& userCategory : g_user)
    {
        std::vector<ComponentDef> fresh;   // 内置里没有、要新加进来的

        for (const ComponentDef& userItem : userCategory.items)
        {
            ComponentDef def = userItem;
            def.userDefined  = true;

            int catIndex = -1;
            int itemIndex = -1;

            if (Locate(g_merged, def.name, catIndex, itemIndex))
                g_merged[catIndex].items[itemIndex] = def;   // 同名 → 覆盖内置元件
            else
                fresh.push_back(def);
        }

        if (fresh.empty())
            continue;

        // 找到同名分类就并进去，否则在末尾新建一个
        int target = -1;

        for (size_t c = 0; c < g_merged.size(); ++c)
        {
            if (g_merged[c].name == userCategory.name)
            {
                target = static_cast<int>(c);
                break;
            }
        }

        if (target < 0)
        {
            CategoryDef created;
            created.name = userCategory.name;

            g_merged.push_back(created);
            target = static_cast<int>(g_merged.size()) - 1;
        }

        for (const ComponentDef& def : fresh)
            g_merged[target].items.push_back(def);
    }
}

// ============================================================
//  对外查询
// ============================================================

bool ComponentCatalog::ValidateDefinition(const ComponentDef& def, wxString* error)
{
    auto reject = [&](const wxString& message) { SetError(error, message); return false; };
    if (def.name.IsEmpty() || def.refPrefix.IsEmpty() || def.inputs < 0 || def.outputs < 0 ||
        def.inputs > 32 || def.outputs > 32 || def.w < 24 || def.h < 20 || def.w > 1000 || def.h > 1000)
        return reject(L"元件名称、前缀、脚数或尺寸不合法");
    ComponentInstance probe;
    probe.name = def.name; probe.w = def.w; probe.h = def.h;
    probe.pins = MakePins(def.name, def.kind, def.inputs, def.outputs);
    if (probe.pins.empty()) return reject(L"元件至少需要一个引脚");
    std::set<std::pair<int, int>> positions;
    for (size_t k = 0; k < probe.pins.size(); ++k) {
        auto at = PinPosition(probe, static_cast<int>(k));
        if (!positions.insert({at.x, at.y}).second)
            return reject(L"引脚位置重叠：请增大元件尺寸或减少引脚数");
    }
    if (def.delayTicks < 1 || def.delayTicks > 1000 || def.clockPeriodTicks < 2 ||
        def.clockPeriodTicks > 10000 || def.clockPeriodTicks % 2)
        return reject(L"传播延迟须为1至1000步，时钟周期须为2至10000的偶数步");
    if (!def.logicModel.empty()) {
        probe.id = 1; probe.logicModel = def.logicModel; probe.truthTable = def.truthTable;
        probe.delayTicks = def.delayTicks; probe.clockPeriodTicks = def.clockPeriodTicks;
        probe.value = def.value;
        NetGraph graph; graph.pinNet.emplace_back();
        for (size_t k = 0; k < probe.pins.size(); ++k) graph.pinNet[0].push_back(static_cast<int>(k));
        graph.nets.resize(probe.pins.size());
        DigitalSimulator simulator; wxString reason;
        if (!simulator.Reset({probe}, graph, reason)) return reject(reason);
    }
    if (error) error->clear();
    return true;
}

const std::vector<CategoryDef>& ComponentCatalog::Categories()
{
    if (!g_inited)
    {
        EnsureUserLibrary(nullptr);
        Reload(nullptr);
    }

    return g_merged;
}

const std::vector<CategoryDef>& ComponentCatalog::UserCategories()
{
    if (!g_inited)
    {
        EnsureUserLibrary(nullptr);
        Reload(nullptr);
    }

    return g_user;
}

const ComponentDef* ComponentCatalog::Find(const wxString& name)
{
    for (const CategoryDef& cat : Categories())
    {
        for (const ComponentDef& item : cat.items)
        {
            if (item.name == name)
                return &item;
        }
    }

    return nullptr;
}

int ComponentCatalog::ComponentCount()
{
    int total = 0;

    for (const CategoryDef& cat : Categories())
        total += static_cast<int>(cat.items.size());

    return total;
}

int ComponentCatalog::UserComponentCount()
{
    int total = 0;

    for (const CategoryDef& cat : UserCategories())
        total += static_cast<int>(cat.items.size());

    return total;
}

} // namespace se

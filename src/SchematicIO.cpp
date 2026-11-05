#include "SchematicIO.h"

#include <wx/file.h>
#include <wx/filename.h>
#include <wx/xml/xml.h>

#include "ComponentCatalog.h"

namespace se
{

// ============================================================
//  局部工具
// ============================================================
namespace
{

// 整数 / 布尔属性（wxXmlAttribute 只有字符串，统一在这里转换）
void AddIntAttr(wxXmlNode* node, const char* name, int value)
{
    node->AddAttribute(name, wxString::Format(L"%d", value));
}

void AddBoolAttr(wxXmlNode* node, const char* name, bool value)
{
    node->AddAttribute(name, value ? L"1" : L"0");
}

int GetIntAttr(const wxXmlNode* node, const char* name, int defaultValue)
{
    wxString text;

    if (!node->GetAttribute(name, &text))
        return defaultValue;

    long value = 0;

    if (!text.ToLong(&value))
        return defaultValue;

    return static_cast<int>(value);
}

bool GetBoolAttr(const wxXmlNode* node, const char* name, bool defaultValue)
{
    wxString text;

    if (!node->GetAttribute(name, &text))
        return defaultValue;

    return text == L"1" || text.Lower() == L"true";
}

wxString GetStrAttr(const wxXmlNode* node, const char* name, const wxString& defaultValue = wxEmptyString)
{
    wxString text;

    if (!node->GetAttribute(name, &text))
        return defaultValue;

    return text;
}

// 枚举 ↔ 字符串
const char* DirToStr(PinDir dir)
{
    switch (dir)
    {
        case PinDir::Input:   return "input";
        case PinDir::Output:  return "output";
        case PinDir::Bidir:   return "bidir";
        case PinDir::Passive: return "passive";
    }

    return "passive";
}

PinDir StrToDir(const wxString& text)
{
    if (text == L"output")  return PinDir::Output;
    if (text == L"bidir")   return PinDir::Bidir;
    if (text == L"passive") return PinDir::Passive;

    return PinDir::Input;
}

const char* SideToStr(Side side)
{
    switch (side)
    {
        case Side::Left:   return "left";
        case Side::Right:  return "right";
        case Side::Top:    return "top";
        case Side::Bottom: return "bottom";
    }

    return "left";
}

Side StrToSide(const wxString& text)
{
    if (text == L"right")  return Side::Right;
    if (text == L"top")    return Side::Top;
    if (text == L"bottom") return Side::Bottom;

    return Side::Left;
}

void SetError(wxString* error, const wxString& message)
{
    if (error)
        *error = message;
}

} // namespace

// ============================================================
//  通配符
// ============================================================

wxString SchematicIO::SchematicWildcard()
{
    return L"原理图文件 (*.ses)|*.ses|XML 文件 (*.xml)|*.xml|所有文件 (*.*)|*.*";
}

wxString SchematicIO::NetlistWildcard()
{
    return L"网络表文件 (*.net)|*.net|文本文件 (*.txt)|*.txt|所有文件 (*.*)|*.*";
}

// ============================================================
//  保存
// ============================================================

bool SchematicIO::Save(const wxString& path, const SchematicDocument& doc, wxString* error)
{
    wxXmlDocument xml;
    xml.SetFileEncoding(L"UTF-8");

    wxXmlNode* root = new wxXmlNode(nullptr, wxXML_ELEMENT_NODE, L"schematic");
    root->AddAttribute(L"version", L"1.0");
    root->AddAttribute(L"format", L"circuit-schematic");
    root->AddAttribute(L"generator", L"SchematicEditor 1.0");

    // ---- 视图设置 ----
    wxXmlNode* view = new wxXmlNode(nullptr, wxXML_ELEMENT_NODE, L"view");
    AddIntAttr(view, "gridStep", doc.gridStep);
    AddBoolAttr(view, "gridVisible", doc.gridVisible);
    AddBoolAttr(view, "snapEnabled", doc.snapEnabled);
    view->AddAttribute(L"zoom", wxString::Format(L"%.4f", doc.zoom));
    root->AddChild(view);

    // ---- 元件 ----
    wxXmlNode* components = new wxXmlNode(nullptr, wxXML_ELEMENT_NODE, L"components");
    components->AddAttribute(L"count", wxString::Format(L"%d", static_cast<int>(doc.components.size())));

    for (const ComponentInstance& comp : doc.components)
    {
        wxXmlNode* node = new wxXmlNode(nullptr, wxXML_ELEMENT_NODE, L"component");
        AddIntAttr(node, "id", comp.id);
        node->AddAttribute(L"name", comp.name);
        node->AddAttribute(L"refdes", comp.refdes);
        AddIntAttr(node, "x", comp.pos.x);
        AddIntAttr(node, "y", comp.pos.y);
        AddIntAttr(node, "w", comp.w);
        AddIntAttr(node, "h", comp.h);
        node->AddAttribute(L"footprint", comp.footprint);
        node->AddAttribute(L"value", comp.value);

        // 引脚一起存：文件自包含，不依赖当前元件库版本
        for (const Pin& pin : comp.pins)
        {
            wxXmlNode* pinNode = new wxXmlNode(nullptr, wxXML_ELEMENT_NODE, L"pin");
            pinNode->AddAttribute(L"name", pin.name);
            pinNode->AddAttribute(L"dir", DirToStr(pin.dir));
            pinNode->AddAttribute(L"side", SideToStr(pin.side));
            AddIntAttr(pinNode, "slot", pin.slot);
            AddIntAttr(pinNode, "count", pin.count);
            AddIntAttr(pinNode, "number", pin.number);

            node->AddChild(pinNode);
        }

        components->AddChild(node);
    }

    root->AddChild(components);

    // ---- 导线 ----
    wxXmlNode* wires = new wxXmlNode(nullptr, wxXML_ELEMENT_NODE, L"wires");
    wires->AddAttribute(L"count", wxString::Format(L"%d", static_cast<int>(doc.wires.size())));

    for (const Wire& wire : doc.wires)
    {
        wxXmlNode* node = new wxXmlNode(nullptr, wxXML_ELEMENT_NODE, L"wire");
        AddIntAttr(node, "id", wire.id);
        AddIntAttr(node, "x1", wire.a.x);
        AddIntAttr(node, "y1", wire.a.y);
        AddIntAttr(node, "x2", wire.b.x);
        AddIntAttr(node, "y2", wire.b.y);

        wires->AddChild(node);
    }

    root->AddChild(wires);

    // ---- 文本注记 ----
    wxXmlNode* texts = new wxXmlNode(nullptr, wxXML_ELEMENT_NODE, L"texts");
    texts->AddAttribute(L"count", wxString::Format(L"%d", static_cast<int>(doc.texts.size())));

    for (const TextItem& item : doc.texts)
    {
        wxXmlNode* node = new wxXmlNode(nullptr, wxXML_ELEMENT_NODE, L"text");
        AddIntAttr(node, "x", item.pos.x);
        AddIntAttr(node, "y", item.pos.y);
        AddIntAttr(node, "size", item.pointSize);

        // 文字内容存在属性里。
        // 注意不能用 node->SetContent()：wxXmlDocument::Save 对元素节点
        // 只写子节点、不写自身 content，会静默丢掉文字（踩过这个坑）。
        node->AddAttribute(L"content", item.text);

        texts->AddChild(node);
    }

    root->AddChild(texts);

    xml.SetRoot(root);

    // 确保目录存在
    const wxFileName fileName(path);

    if (!fileName.GetPath().IsEmpty() && !wxFileName::DirExists(fileName.GetPath()))
    {
        SetError(error, wxString::Format(L"目录不存在：%s", fileName.GetPath()));
        return false;
    }

    if (!xml.Save(path))
    {
        SetError(error, L"写不进去（文件被占用或没有写权限？）");
        return false;
    }

    return true;
}

// ============================================================
//  加载
// ============================================================

bool SchematicIO::Load(const wxString& path, SchematicDocument& doc, wxString* error)
{
    wxXmlDocument xml;

    if (!xml.Load(path))
    {
        SetError(error, L"无法读取文件，或 XML 格式有误");
        return false;
    }

    wxXmlNode* root = xml.GetRoot();

    if (!root)
    {
        SetError(error, L"文件是空的");
        return false;
    }

    if (root->GetName() != L"schematic")
    {
        SetError(error, wxString::Format(L"根节点是 <%s>，不是本程序的原理图文件",
                                         root->GetName()));
        return false;
    }

    SchematicDocument loaded;

    for (wxXmlNode* section = root->GetChildren(); section; section = section->GetNext())
    {
        if (section->GetType() != wxXML_ELEMENT_NODE)
            continue;

        const wxString sectionName = section->GetName();

        // ---------------- view ----------------
        if (sectionName == L"view")
        {
            loaded.gridStep    = GetIntAttr(section, "gridStep", 10);
            loaded.gridVisible = GetBoolAttr(section, "gridVisible", true);
            loaded.snapEnabled = GetBoolAttr(section, "snapEnabled", true);

            wxString zoomText = GetStrAttr(section, "zoom", L"1.0");
            double   zoom     = 1.0;

            if (zoomText.ToDouble(&zoom) && zoom > 0.01)
                loaded.zoom = zoom;
        }
        // ---------------- components ----------------
        else if (sectionName == L"components")
        {
            for (wxXmlNode* node = section->GetChildren(); node; node = node->GetNext())
            {
                if (node->GetType() != wxXML_ELEMENT_NODE || node->GetName() != L"component")
                    continue;

                ComponentInstance comp;
                comp.id     = GetIntAttr(node, "id", 0);
                comp.name   = GetStrAttr(node, "name");
                comp.refdes = GetStrAttr(node, "refdes");
                comp.pos    = wxPoint(GetIntAttr(node, "x", 0), GetIntAttr(node, "y", 0));
                comp.w      = GetIntAttr(node, "w", 60);
                comp.h      = GetIntAttr(node, "h", 40);

                // 封装 / 元件值：老文件没有这两个属性，回退到元件库默认值
                comp.footprint = GetStrAttr(node, "footprint");
                comp.value     = GetStrAttr(node, "value");

                if (comp.footprint.IsEmpty() && comp.value.IsEmpty())
                {
                    if (const ComponentDef* def = ComponentCatalog::Find(comp.name))
                    {
                        comp.footprint = def->footprint;
                        comp.value     = def->value;
                    }
                }

                int pinOrder = 0;

                for (wxXmlNode* pinNode = node->GetChildren(); pinNode; pinNode = pinNode->GetNext())
                {
                    if (pinNode->GetType() != wxXML_ELEMENT_NODE || pinNode->GetName() != L"pin")
                        continue;

                    Pin pin;
                    pin.name  = GetStrAttr(pinNode, "name");
                    pin.dir   = StrToDir(GetStrAttr(pinNode, "dir", L"input"));
                    pin.side  = StrToSide(GetStrAttr(pinNode, "side", L"left"));
                    pin.slot  = GetIntAttr(pinNode, "slot", 0);
                    pin.count = GetIntAttr(pinNode, "count", 1);

                    // 老文件没有 number 属性 → 按出现顺序补 1,2,3…
                    ++pinOrder;
                    pin.number = GetIntAttr(pinNode, "number", pinOrder);

                    comp.pins.push_back(pin);
                }

                // 引脚定义缺失时兜底：按名字重建（老文件 / 手写文件容错）
                if (comp.pins.empty())
                    comp.pins = MakePins(comp.name, ComponentKind::Logic, 0, 1);

                loaded.components.push_back(comp);
            }
        }
        // ---------------- wires ----------------
        else if (sectionName == L"wires")
        {
            for (wxXmlNode* node = section->GetChildren(); node; node = node->GetNext())
            {
                if (node->GetType() != wxXML_ELEMENT_NODE || node->GetName() != L"wire")
                    continue;

                Wire wire;
                wire.id = GetIntAttr(node, "id", 0);
                wire.a  = wxPoint(GetIntAttr(node, "x1", 0), GetIntAttr(node, "y1", 0));
                wire.b  = wxPoint(GetIntAttr(node, "x2", 0), GetIntAttr(node, "y2", 0));

                loaded.wires.push_back(wire);
            }
        }
        // ---------------- texts ----------------
        else if (sectionName == L"texts")
        {
            for (wxXmlNode* node = section->GetChildren(); node; node = node->GetNext())
            {
                if (node->GetType() != wxXML_ELEMENT_NODE || node->GetName() != L"text")
                    continue;

                TextItem item;
                item.pos       = wxPoint(GetIntAttr(node, "x", 0), GetIntAttr(node, "y", 0));
                item.pointSize = GetIntAttr(node, "size", 12);
                item.text      = GetStrAttr(node, "content");

                if (item.text.Trim().IsEmpty())
                    continue;

                loaded.texts.push_back(item);
            }
        }
    }

    doc = loaded;
    return true;
}

// ============================================================
//  纯文本写出
// ============================================================

bool SchematicIO::WriteTextFile(const wxString& path, const wxString& text, wxString* error)
{
    wxFile file;

    if (!file.Create(path, true))
    {
        SetError(error, L"无法创建文件（路径不存在或没有写权限？）");
        return false;
    }

    // 统一按 UTF-8 写出（网表里有中文注释；不加 BOM，便于 SPICE 类工具直接读取）
    if (!file.Write(text, wxConvUTF8))
    {
        SetError(error, L"写入过程中出错");
        file.Close();
        return false;
    }

    file.Close();
    return true;
}

} // namespace se

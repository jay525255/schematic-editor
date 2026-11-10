#include "SchematicIO.h"

#include <wx/file.h>
#include <wx/filename.h>
#include <wx/xml/xml.h>
#include <wx/wfstream.h>

#include "ComponentCatalog.h"
#include <cmath>
#include <set>

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

bool ValidateDocument(const SchematicDocument& doc, wxString* error)
{
    auto reject = [&](const wxString& reason) { SetError(error, reason); return false; };
    auto coordinate = [](const wxPoint& p) { return p.x >= -1000000 && p.x <= 1000000 && p.y >= -1000000 && p.y <= 1000000; };
    if (doc.gridStep < 1 || doc.gridStep > 1000 || !std::isfinite(doc.zoom) || doc.zoom < 0.01 || doc.zoom > 8)
        return reject(L"视图参数不合法");
    std::set<int> componentIds, wireIds; std::set<wxString> references;
    for (const auto& comp : doc.components) {
        if (comp.id <= 0 || comp.id > 1000000 || !componentIds.insert(comp.id).second || comp.refdes.empty() || !references.insert(comp.refdes).second)
            return reject(L"元件ID或位号为空/重复，请在原文件中修正后重新打开");
        if (comp.name.empty() || comp.w < 1 || comp.h < 1 || comp.w > (comp.imported ? 100000 : 10000) || comp.h > (comp.imported ? 100000 : 10000) || !coordinate(comp.pos) || comp.xform < 0 || comp.xform > 7)
            return reject(L"元件名称、尺寸、坐标或旋转参数不合法");
        if (comp.pins.empty() && !comp.imported) return reject(L"元件缺少引脚定义");
        std::set<int> numbers; std::set<std::pair<int,int>> positions;
        for (size_t k = 0; k < comp.pins.size(); ++k) {
            const auto& pin = comp.pins[k];
            if (pin.name.empty() || pin.number < 1 || !numbers.insert(pin.number).second || pin.count < 1 || pin.count > (comp.imported ? 4096 : 64) || pin.slot < 0 || pin.slot >= pin.count)
                return reject(L"引脚名称、编号或布局参数不合法");
            auto at = PinPosition(comp, static_cast<int>(k));
            if (!positions.insert({at.x, at.y}).second && !comp.imported) return reject(L"同一元件多个引脚位置重叠");
        }
        for (const auto& g : comp.graphics) {
            if (g.points.empty() || g.points.size() > 10000 || g.width < 0 || g.width > 1000 || g.textSize < 1 || g.textSize > 10000 || g.fill < 0 || g.fill > 2)
                return reject(L"导入图形参数不合法");
            for (const auto& p : g.points) if (!coordinate(p)) return reject(L"导入图形坐标不合法");
        }
        for (const auto& p : comp.pins) if (p.exact && (!coordinate(p.endpoint) || !coordinate(p.anchor))) return reject(L"引脚坐标不合法");
        if (comp.delayTicks < 1 || comp.delayTicks > 1000 || comp.clockPeriodTicks < 2 || comp.clockPeriodTicks > 10000 || comp.clockPeriodTicks % 2)
            return reject(L"数字仿真时间参数不合法");
    }
    for (const auto& wire : doc.wires)
        if (wire.id <= 0 || wire.id > 1000000 || !wireIds.insert(wire.id).second || !wire.IsAxisAligned() || wire.a == wire.b || !coordinate(wire.a) || !coordinate(wire.b))
            return reject(L"导线ID重复、坐标不合法或存在斜线/零长导线");
    for (const auto& text : doc.texts)
        if (!coordinate(text.pos) || text.pointSize < 1 || text.pointSize > 200)
            return reject(L"注记位置或字号不合法");
    return true;
}

// Reject malformed numeric fields before the legacy default helpers can hide them.
bool ValidateXmlNumbers(const wxXmlNode* node, wxString* error)
{
    std::set<wxString> fields;
    const auto name = node->GetName();
    if (name == "component") fields = {"id","x","y","w","h","xform","delayTicks","clockPeriodTicks"};
    else if (name == "pin") fields = {"slot","count","number","px","py","ax","ay"};
    else if (name == "graphic") fields = {"width","fill","size"};
    else if (name == "point") fields = {"x","y"};
    else if (name == "wire") fields = {"id","x1","y1","x2","y2"};
    else if (name == "text") fields = {"x","y","size"};
    else if (name == "view") fields = {"gridStep"};
    for (const auto& key : fields) {
        wxString value; long number;
        if (node->GetAttribute(key, &value) && (!value.ToLong(&number) || number < -1000000 || number > 1000000)) {
            SetError(error, L"数值字段不合法：" + name + "." + key); return false;
        }
    }
    for (const auto* child = node->GetChildren(); child; child = child->GetNext())
        if (child->GetType() == wxXML_ELEMENT_NODE && !ValidateXmlNumbers(child, error)) return false;
    return true;
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
    if (!ValidateDocument(doc, error)) return false;
    wxXmlDocument xml;
    xml.SetFileEncoding(L"UTF-8");

    wxXmlNode* root = new wxXmlNode(nullptr, wxXML_ELEMENT_NODE, L"schematic");
    bool extended = false;
    for (const auto& c : doc.components) extended = extended || c.imported;
    root->AddAttribute(L"version", extended ? L"1.2" : L"1.1");
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

        // 旋转 / 镜像：0..7（0..3 = 顺时针 0/90/180/270 度，4..7 = 先水平镜像再转）
        // 老文件里没有这个属性，读的时候默认 0，完全兼容
        AddIntAttr(node, "xform", comp.xform);
        node->AddAttribute("logicModel", comp.logicModel);
        node->AddAttribute("truthTable", comp.truthTable);
        AddIntAttr(node, "delayTicks", comp.delayTicks);
        AddIntAttr(node, "clockPeriodTicks", comp.clockPeriodTicks);

        if (comp.imported) {
            AddBoolAttr(node, "imported", true); AddBoolAttr(node, "pcbExcluded", comp.pcbExcluded);
            node->AddAttribute("netLabel", comp.netLabel);
            for (const auto& prop : comp.properties) {
                auto* n = new wxXmlNode(wxXML_ELEMENT_NODE, "property");
                n->AddAttribute("name", prop.first); n->AddAttribute("value", prop.second); node->AddChild(n);
            }
            for (const auto& g : comp.graphics) {
                auto* n = new wxXmlNode(wxXML_ELEMENT_NODE, "graphic");
                n->AddAttribute("text", g.text); n->AddAttribute("property", g.property);
                AddIntAttr(n,"size",g.textSize); AddIntAttr(n,"fill",g.fill); AddIntAttr(n,"width",g.width);
                for (const auto& pt : g.points) {
                    auto* pn = new wxXmlNode(wxXML_ELEMENT_NODE,"point");
                    AddIntAttr(pn,"x",pt.x); AddIntAttr(pn,"y",pt.y); n->AddChild(pn);
                }
                node->AddChild(n);
            }
        }
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

            if (pin.exact) {
                AddBoolAttr(pinNode,"exact",true); AddIntAttr(pinNode,"px",pin.endpoint.x); AddIntAttr(pinNode,"py",pin.endpoint.y);
                AddIntAttr(pinNode,"ax",pin.anchor.x); AddIntAttr(pinNode,"ay",pin.anchor.y);
                pinNode->AddAttribute("pad",pin.pad); pinNode->AddAttribute("implicitNet",pin.implicitNet);
                AddBoolAttr(pinNode,"hidden",pin.hidden); AddBoolAttr(pinNode,"noConnect",pin.noConnect);
            }
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

    wxTempFileOutputStream output(path);
    if (!output.IsOk() || !xml.Save(output) || !output.Commit())
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

    const auto version = root->GetAttribute("version", "1.0");
    if (version != "1.0" && version != "1.1" && version != "1.2") {
        SetError(error, L"不支持的文件版本：" + version); return false;
    }
    if (!ValidateXmlNumbers(root, error)) return false;
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

                // 旋转 / 镜像：老文件没有这个属性 → 0（不转）
                comp.xform = GetIntAttr(node, "xform", 0);
                comp.logicModel = GetStrAttr(node, "logicModel");
                comp.truthTable = GetStrAttr(node, "truthTable");
                comp.delayTicks = GetIntAttr(node, "delayTicks", 1);
                comp.clockPeriodTicks = GetIntAttr(node, "clockPeriodTicks", 10);

                if (!node->HasAttribute("footprint") && !node->HasAttribute("value"))
                {
                    if (const ComponentDef* def = ComponentCatalog::Find(comp.name))
                    {
                        comp.footprint = def->footprint;
                        comp.value     = def->value;
                    }
                }

                comp.imported = GetBoolAttr(node,"imported",false);
                comp.pcbExcluded = GetBoolAttr(node,"pcbExcluded",false); comp.netLabel = GetStrAttr(node,"netLabel");
                for (auto* n = node->GetChildren(); n; n = n->GetNext()) {
                    if (n->GetName() == "property") comp.properties.push_back({GetStrAttr(n,"name"),GetStrAttr(n,"value")});
                    if (n->GetName() != "graphic") continue;
                    SymbolGraphic g; g.text = GetStrAttr(n,"text"); g.property = GetStrAttr(n,"property");
                    g.textSize = GetIntAttr(n,"size",100); g.fill = GetIntAttr(n,"fill",0); g.width = GetIntAttr(n,"width",10);
                    for (auto* pt = n->GetChildren(); pt; pt = pt->GetNext()) if (pt->GetName() == "point")
                        g.points.push_back({GetIntAttr(pt,"x",0),GetIntAttr(pt,"y",0)});
                    comp.graphics.push_back(g);
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

                    pin.exact = GetBoolAttr(pinNode,"exact",false);
                    pin.endpoint = {GetIntAttr(pinNode,"px",0),GetIntAttr(pinNode,"py",0)};
                    pin.anchor = {GetIntAttr(pinNode,"ax",0),GetIntAttr(pinNode,"ay",0)};
                    pin.pad = GetStrAttr(pinNode,"pad"); pin.implicitNet = GetStrAttr(pinNode,"implicitNet");
                    pin.hidden = GetBoolAttr(pinNode,"hidden",false); pin.noConnect = GetBoolAttr(pinNode,"noConnect",false);
                    comp.pins.push_back(pin);
                }

                // 引脚定义缺失时兜底：按名字重建（老文件 / 手写文件容错）
                if (comp.pins.empty())
                    if (const auto* def = ComponentCatalog::Find(comp.name))
                        comp.pins = MakePins(comp.name, def->kind, def->inputs, def->outputs);

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

    if (!ValidateDocument(loaded, error)) return false;
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

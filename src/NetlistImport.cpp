#include "NetlistImport.h"

#include <wx/sstream.h>
#include <wx/textfile.h>
#include <wx/tokenzr.h>
#include <wx/wfstream.h>
#include <wx/wxcrt.h>

#include <algorithm>
#include <cmath>
#include <map>

namespace se
{

namespace
{

// ------------------------------------------------------------
// 通用小工具：把文本切成"最外层括号块"
// ------------------------------------------------------------
std::vector<wxString> ChildBlocks(const wxString& text)
{
    std::vector<wxString> blocks;
    int    depth = 0;
    size_t start = 0;

    for (size_t i = 0; i < text.length(); ++i)
    {
        const wxUniChar c = text[i];

        if (c == '(')
        {
            if (depth == 0)
                start = i;
            ++depth;
        }
        else if (c == ')')
        {
            if (depth > 0)
                --depth;
            if (depth == 0)
                blocks.push_back(text.Mid(start, i - start + 1));
        }
    }

    return blocks;
}

wxString Inner(const wxString& block)
{
    if (block.length() >= 2 && block.StartsWith("(") && block.EndsWith(")"))
        return block.Mid(1, block.length() - 2);
    return block;
}

// 取块头：第一个空格/右括号之前的词，例如 "(net (code 1)" → "net"
wxString HeadOf(const wxString& block)
{
    const wxString body = Inner(block);
    size_t         end  = body.find_first_of(" \t\r\n()");

    if (end == wxString::npos)
        return body;

    return body.Left(end);
}

// 在块里找 "(key xxx)" 的值；带引号的去掉引号
wxString FieldOf(const wxString& block, const wxString& key)
{
    const wxString needle = "(" + key;
    size_t         pos    = block.find(needle);

    while (pos != wxString::npos)
    {
        const size_t after = pos + needle.length();

        // 必须真的是这个 key（后面紧跟空格或括号），避免 "(ref" 命中 "(reference"
        if (after < block.length())
        {
            const wxUniChar next = block[after];
            if (next != ' ' && next != '\t' && next != '\r' && next != '\n' && next != '(' && next != ')')
            {
                pos = block.find(needle, after);
                continue;
            }
        }

        size_t i = after;
        while (i < block.length() && wxIsspace(block[i]))
            ++i;

        if (i < block.length() && block[i] == '"')
        {
            const size_t close = block.find('"', i + 1);
            if (close == wxString::npos)
                return wxEmptyString;
            return block.Mid(i + 1, close - i - 1);
        }

        size_t end = i;
        while (end < block.length() && block[end] != ')' && !wxIsspace(block[end]))
            ++end;

        return block.Mid(i, end - i);
    }

    return wxEmptyString;
}

struct RawNet
{
    wxString name;
    std::vector<std::pair<wxString, wxString>> nodes; // (refdes, pin)
};

struct RawComponent
{
    wxString              ref;
    wxString              value;
    wxString              footprint;
    std::vector<wxString> pins; // 出现顺序，去重
};

struct RawDesign
{
    std::vector<RawComponent> components;
    std::vector<RawNet>       nets;
    std::vector<wxString>     warnings;
};

// ---------------- KiCad 原生网表 ----------------

bool ParseKiCad(const wxString& text, RawDesign& design, wxString& error)
{
    std::vector<wxString> top = ChildBlocks(text);
    wxString              componentsBlock;
    wxString              netsBlock;

    for (const wxString& block : top)
    {
        const wxString head = HeadOf(block);
        if (head == "components")
            componentsBlock = block;
        else if (head == "nets")
            netsBlock = block;
        else if (head == "export")
        {
            for (const wxString& child : ChildBlocks(Inner(block)))
            {
                const wxString childHead = HeadOf(child);
                if (childHead == "components")
                    componentsBlock = child;
                else if (childHead == "nets")
                    netsBlock = child;
            }
        }
    }

    if (componentsBlock.IsEmpty() && netsBlock.IsEmpty())
    {
        error = L"看起来是 KiCad 网表，但找不到 (components ...) 或 (nets ...) 段";
        return false;
    }

    std::map<wxString, size_t> indexOf;

    for (const wxString& block : ChildBlocks(Inner(componentsBlock)))
    {
        if (HeadOf(block) != "comp")
            continue;

        RawComponent comp;
        comp.ref       = FieldOf(block, "ref");
        comp.value     = FieldOf(block, "value");
        comp.footprint = FieldOf(block, "footprint");

        if (comp.ref.IsEmpty())
            continue;

        design.components.push_back(comp);
    }

    for (const wxString& block : ChildBlocks(Inner(netsBlock)))
    {
        if (HeadOf(block) != "net")
            continue;

        RawNet net;
        net.name = FieldOf(block, "name");

        for (const wxString& nodeBlock : ChildBlocks(Inner(block)))
        {
            if (HeadOf(nodeBlock) != "node")
                continue;

            const wxString ref = FieldOf(nodeBlock, "ref");
            const wxString pin = FieldOf(nodeBlock, "pin");
            if (ref.IsEmpty() || pin.IsEmpty())
                continue;

            net.nodes.emplace_back(ref, pin);
        }

        if (net.nodes.empty())
            continue;

        // 只有一个引脚的网络（悬空焊盘）照样保留：引脚是元件的一部分，
        // 丢掉它会让元件少一根腿，但不需要为它画线。
        if (net.nodes.size() == 1)
            design.warnings.push_back(wxString::Format(L"网络 %s 只有一个引脚（保留引脚，不画线）", net.name));

        design.nets.push_back(net);
    }

    return true;
}

// ---------------- Protel / Altium 网表 ----------------

// 把块内容按"任意空白"切成词。Protel 网表常见写法是每个字段独占一行，
// 所以必须先把 CR/LF/TAB 都压成空格，只按空格切是切不开的。
static std::vector<wxString> Tokenize(const wxString& body)
{
    wxString flat = body;
    flat.Replace(L"\r", L" ");
    flat.Replace(L"\n", L" ");
    flat.Replace(L"\t", L" ");

    wxArrayString         parts = wxSplit(flat, ' ', '\0');
    std::vector<wxString> words;

    for (const wxString& raw : parts)
    {
        const wxString w = wxString(raw).Trim().Trim(false);
        if (!w.IsEmpty())
            words.push_back(w);
    }

    return words;
}

bool ParseProtel(const wxString& text, RawDesign& design, wxString& error)
{
    // 语法：[位号 封装 值] 与 (网络名 位号-引脚 位号-引脚 ...)
    // 两种写法都接受：块内用换行分隔，或全在一行用空格分隔。
    wxString rest = text;
    bool     any  = false;

    for (size_t i = 0; i < rest.length(); ++i)
    {
        const wxUniChar c = rest[i];

        if (c == '[')
        {
            const size_t close = rest.find(']', i + 1);
            if (close == wxString::npos)
                break;

            wxString              body  = rest.Mid(i + 1, close - i - 1);
            std::vector<wxString> words = Tokenize(body);

            if (words.size() >= 1)
            {
                RawComponent comp;
                comp.ref       = words[0];
                comp.footprint = words.size() >= 2 ? words[1] : wxString();
                comp.value     = words.size() >= 3 ? words[2] : wxString();
                design.components.push_back(comp);
                any = true;
            }

            i = close;
        }
        else if (c == '(')
        {
            const size_t close = rest.find(')', i + 1);
            if (close == wxString::npos)
                break;

            wxString              body  = rest.Mid(i + 1, close - i - 1);
            std::vector<wxString> words = Tokenize(body);

            if (!words.empty())
            {
                RawNet net;
                net.name = words[0];

                for (size_t k = 1; k < words.size(); ++k)
                {
                    const wxString& token = words[k];
                    const size_t    dash  = token.find('-');
                    if (dash == wxString::npos || dash == 0)
                    {
                        design.warnings.push_back(wxString::Format(L"%s 里的引脚写法无法识别：%s", net.name, token));
                        continue;
                    }

                    net.nodes.emplace_back(token.Left(dash), token.Mid(dash + 1));
                }

                if (net.nodes.empty())
                    continue;

                if (net.nodes.size() == 1)
                    design.warnings.push_back(wxString::Format(L"网络 %s 只有一个引脚（保留引脚，不画线）", net.name));

                design.nets.push_back(net);

                any = true;
            }

            i = close;
        }
    }

    if (!any)
    {
        error = L"看不出来这是 Protel/Altium 网表（没有 [位号 ...] 或 (网络名 ...) 块）";
        return false;
    }

    return true;
}

// ---------------- 网表 → 图元 ----------------

wxString PinSpec(const wxString& pin)
{
    return pin.IsEmpty() ? wxString(L"?") : pin;
}

} // namespace

NetlistFormat DetectNetlistFormat(const wxString& text)
{
    if (text.Contains("(nets") && (text.Contains("(comp") || text.Contains("(export")))
        return NetlistFormat::KiCad;

    if (text.Contains("[") && text.Contains("]") && text.Contains("("))
        return NetlistFormat::Protel;

    return NetlistFormat::Unknown;
}

bool ImportNetlistText(const wxString& text, NetlistImportResult& result, wxString& error)
{
    result = NetlistImportResult();

    RawDesign design;

    switch (DetectNetlistFormat(text))
    {
        case NetlistFormat::KiCad:
            if (!ParseKiCad(text, design, error))
                return false;
            break;

        case NetlistFormat::Protel:
            if (!ParseProtel(text, design, error))
                return false;
            break;

        default:
            error = L"识别不出网表格式：既不是 KiCad 原生网表（(export ... (nets ...))），"
                    L"也不是 Protel/Altium 网表（[位号 ...] + (网络名 ...)）。";
            return false;
    }

    if (design.components.empty())
    {
        error = L"网表里没有任何元件";
        return false;
    }

    // ---- 元件里出现过的引脚（按网络里遇到的顺序收集，保持和网表一致）----
    std::map<wxString, std::vector<wxString>> pinsOf;

    for (const RawNet& net : design.nets)
    {
        for (const auto& node : net.nodes)
        {
            std::vector<wxString>& pins = pinsOf[node.first];
            if (std::find(pins.begin(), pins.end(), node.second) == pins.end())
                pins.push_back(node.second);
        }
    }

    // ---- 布局：每行 4 个，行高固定，网络车道排在所有元件下方 ----
    const int kColumns = 4;
    const int kCellW   = 240;
    const int kCellH   = 260;

    std::map<wxString, size_t> compIndex;

    for (size_t i = 0; i < design.components.size(); ++i)
        compIndex[design.components[i].ref] = i;

    result.document.components.clear();
    result.document.wires.clear();
    result.document.texts.clear();
    result.document.gridStep    = 10;
    result.document.gridVisible = true;
    result.document.snapEnabled = true;
    result.document.zoom        = 1.0;

    for (size_t i = 0; i < design.components.size(); ++i)
    {
        const RawComponent& raw = design.components[i];

        ComponentInstance comp;
        comp.id      = static_cast<int>(i) + 1;
        comp.name    = L"网表元件";
        comp.refdes  = raw.ref;
        comp.value   = raw.value;
        comp.footprint = raw.footprint;

        std::vector<wxString> pins = pinsOf[raw.ref];
        if (pins.empty())
            pins.push_back(L"1"); // 网表里没出现过这个元件的引脚（悬空器件），给它一个引脚以便显示

        const int n         = static_cast<int>(pins.size());
        const int leftCount = (n + 1) / 2;
        const int rightCount = n - leftCount;
        const int perSide   = std::max(1, std::max(leftCount, rightCount));

        comp.w = 90;
        comp.h = std::max(40, 20 * (perSide + 1));

        const int col = static_cast<int>(i) % kColumns;
        const int row = static_cast<int>(i) / kColumns;
        comp.pos      = wxPoint(col * kCellW, row * kCellH);

        for (int k = 0; k < n; ++k)
        {
            Pin pin;
            pin.side   = (k < leftCount) ? Side::Left : Side::Right;
            pin.slot   = (k < leftCount) ? k : (k - leftCount);
            pin.count  = (k < leftCount) ? leftCount : rightCount;
            pin.name   = PinSpec(pins[static_cast<size_t>(k)]);
            pin.pad    = pins[static_cast<size_t>(k)];
            pin.dir    = PinDir::Passive;
            pin.number = k + 1;
            comp.pins.push_back(pin);
        }

        if (raw.footprint.IsEmpty())
            result.warnings.push_back(wxString::Format(L"%s 在网表里没有封装", raw.ref));

        result.document.components.push_back(comp);
    }

    // ---- 引脚世界坐标 ----
    std::map<wxString, std::map<wxString, wxPoint>> pinPos;

    for (const ComponentInstance& comp : result.document.components)
    {
        for (size_t k = 0; k < comp.pins.size(); ++k)
            pinPos[comp.refdes][comp.pins[k].pad] = PinPosition(comp, static_cast<int>(k));
    }

    // ---- 布线：每个网络在元件下方占一条横向车道 ----
    const int rows      = (static_cast<int>(result.document.components.size()) + kColumns - 1) / kColumns;
    int       laneY     = rows * kCellH + 80;
    int       nextWire  = 1;

    for (const RawNet& net : design.nets)
    {
        std::vector<wxPoint> points;

        for (const auto& node : net.nodes)
        {
            const auto compIt = pinPos.find(node.first);
            if (compIt == pinPos.end())
            {
                result.warnings.push_back(wxString::Format(L"网络 %s 引用了不存在的元件 %s", net.name, node.first));
                continue;
            }

            const auto pinIt = compIt->second.find(node.second);
            if (pinIt == compIt->second.end())
            {
                result.warnings.push_back(
                    wxString::Format(L"网络 %s 引用了 %s 上不存在的引脚 %s", net.name, node.first, node.second));
                continue;
            }

            points.push_back(pinIt->second);
        }

        if (points.size() < 2)
        {
            ++result.netCount;   // 悬空焊盘也是一条网络（只有一个引脚）
            continue;
        }

        int minX = points.front().x;
        int maxX = points.front().x;
        for (const wxPoint& p : points)
        {
            minX = std::min(minX, p.x);
            maxX = std::max(maxX, p.x);
        }

        // 车道横线
        Wire trunk;
        trunk.id = nextWire++;
        trunk.a  = wxPoint(minX, laneY);
        trunk.b  = wxPoint(maxX, laneY);
        if (trunk.a != trunk.b)
            result.document.wires.push_back(trunk);

        // 每个引脚竖着引到车道上（端点落在横线上 = T 型连接）
        std::vector<int> usedX;
        for (const wxPoint& p : points)
        {
            if (std::find(usedX.begin(), usedX.end(), p.x) != usedX.end())
                continue;
            usedX.push_back(p.x);

            Wire drop;
            drop.id = nextWire++;
            drop.a  = wxPoint(p.x, p.y);
            drop.b  = wxPoint(p.x, laneY);
            if (drop.a != drop.b)
                result.document.wires.push_back(drop);
        }

        ++result.netCount;
        laneY += 20;
    }

    result.componentCount = static_cast<int>(result.document.components.size());
    result.wireCount      = static_cast<int>(result.document.wires.size());

    if (result.netCount == 0)
        result.warnings.push_back(L"网表里没有任何 \"两个以上引脚\" 的网络，画面上只会看到一堆孤立元件");

    return true;
}

bool ImportNetlistFile(const wxString& path, NetlistImportResult& result, wxString& error)
{
    wxFileInputStream stream(path);

    if (!stream.IsOk())
    {
        error = wxString::Format(L"打不开文件：%s", path);
        return false;
    }

    wxString text;
    {
        wxFileInputStream in(path);
        wxStringOutputStream out;
        in.Read(out);
        text = out.GetString();
    }

    if (text.IsEmpty())
    {
        error = L"文件是空的";
        return false;
    }

    if (!ImportNetlistText(text, result, error))
        return false;

    return true;
}

} // namespace se
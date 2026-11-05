#include "NetModel.h"

#include <wx/datetime.h>

#include <algorithm>
#include <map>
#include <set>
#include <utility>

namespace se
{

// ============================================================
//  Wire
// ============================================================

int Wire::Length() const
{
    return std::abs(b.x - a.x) + std::abs(b.y - a.y);
}

// ============================================================
//  几何
// ============================================================

Side PinSide(const ComponentInstance& comp, int pinIndex)
{
    if (pinIndex < 0 || pinIndex >= static_cast<int>(comp.pins.size()))
        return Side::Left;

    return comp.pins[pinIndex].side;
}

// ------------------------------------------------------------
// 引脚沿边的偏移量必须落在 10 单位网格上。
// 否则引脚会落在网格之外，画导线时"吸附到引脚"和"吸附到网格"
// 会互相打架：点一下引脚、再点一下空白，就会多出一段 1~3 单位的折角。
// ------------------------------------------------------------
namespace
{

int AlignPinOffset(int v)
{
    if (v >= 0)
        return ((v + 5) / 10) * 10;

    return -(((-v) + 5) / 10) * 10;
}

} // namespace

wxPoint PinAnchor(const ComponentInstance& comp, int pinIndex)
{
    if (pinIndex < 0 || pinIndex >= static_cast<int>(comp.pins.size()))
        return comp.pos;

    const Pin& pin = comp.pins[pinIndex];
    const int  n   = std::max(1, pin.count);
    const int  k   = pin.slot;

    switch (pin.side)
    {
        case Side::Left:
            return wxPoint(comp.pos.x,
                           comp.pos.y + AlignPinOffset(comp.h * (k + 1) / (n + 1)));

        case Side::Right:
            return wxPoint(comp.pos.x + comp.w,
                           comp.pos.y + AlignPinOffset(comp.h * (k + 1) / (n + 1)));

        case Side::Top:
            return wxPoint(comp.pos.x + AlignPinOffset(comp.w * (k + 1) / (n + 1)),
                           comp.pos.y);

        case Side::Bottom:
        default:
            return wxPoint(comp.pos.x + AlignPinOffset(comp.w * (k + 1) / (n + 1)),
                           comp.pos.y + comp.h);
    }
}

wxPoint PinPosition(const ComponentInstance& comp, int pinIndex)
{
    const wxPoint anchor = PinAnchor(comp, pinIndex);

    switch (PinSide(comp, pinIndex))
    {
        case Side::Left:   return wxPoint(anchor.x - kPinLen, anchor.y);
        case Side::Right:  return wxPoint(anchor.x + kPinLen, anchor.y);
        case Side::Top:    return wxPoint(anchor.x, anchor.y - kPinLen);
        case Side::Bottom: return wxPoint(anchor.x, anchor.y + kPinLen);
    }

    return anchor;
}

bool PointOnSegment(const wxPoint& p, const wxPoint& a, const wxPoint& b)
{
    // 叉积为 0 → 共线；再用包围盒判断是否落在线段内（含端点）
    const long long cross =
        static_cast<long long>(p.x - a.x) * (b.y - a.y) -
        static_cast<long long>(p.y - a.y) * (b.x - a.x);

    if (cross != 0)
        return false;

    return p.x >= std::min(a.x, b.x) && p.x <= std::max(a.x, b.x) &&
           p.y >= std::min(a.y, b.y) && p.y <= std::max(a.y, b.y);
}

bool IsEndpointConnected(const std::vector<ComponentInstance>& components,
                         const std::vector<Wire>&              wires,
                         const wxPoint&                        point,
                         int                                   selfWireIndex)
{
    // 1) 落在某个元件引脚上
    for (const ComponentInstance& comp : components)
    {
        for (size_t k = 0; k < comp.pins.size(); ++k)
        {
            if (PinPosition(comp, static_cast<int>(k)) == point)
                return true;
        }
    }

    // 2) 与别的导线端点重合
    for (size_t i = 0; i < wires.size(); ++i)
    {
        if (static_cast<int>(i) == selfWireIndex)
            continue;

        if (wires[i].a == point || wires[i].b == point)
            return true;
    }

    // 3) 落在别的导线上（T 型连接）
    for (size_t i = 0; i < wires.size(); ++i)
    {
        if (static_cast<int>(i) == selfWireIndex)
            continue;

        if (PointOnSegment(point, wires[i].a, wires[i].b))
            return true;
    }

    return false;
}

// ============================================================
//  引脚表生成
// ============================================================

namespace
{

wxString InputPinName(int i)
{
    if (i < 26)
        return wxString::Format(L"%c", static_cast<wxChar>(L'A' + i));

    return wxString::Format(L"IN%d", i + 1);
}

wxString OutputPinName(const wxString& compName, int i)
{
    const bool isAdder =
        compName.Find(L"半加器") != wxNOT_FOUND || compName.Find(L"全加器") != wxNOT_FOUND;

    if (isAdder)
        return (i == 0) ? wxString(L"S") : wxString(L"C");

    if (i == 0)
        return L"Y";

    return wxString::Format(L"Y%d", i + 1);
}

} // namespace

std::vector<Pin> MakePins(const wxString& compName,
                          ComponentKind   kind,
                          int             inputs,
                          int             outputs)
{
    std::vector<Pin> pins;

    switch (kind)
    {
        case ComponentKind::Passive:
            pins.push_back({ L"1", PinDir::Passive, Side::Left,  0, 1 });
            pins.push_back({ L"2", PinDir::Passive, Side::Right, 0, 1 });
            break;

        case ComponentKind::Power:
            pins.push_back({ compName, PinDir::Passive, Side::Bottom, 0, 1 });
            break;

        case ComponentKind::Port:
            // 端口的方向和 Logisim 一致：
            //   “输入引脚 / 时钟 / 常量” 是把信号送进电路 → 引脚在右边
            //   “输出引脚 / 探针 / 标签” 是从电路取出信号 → 引脚在左边
            if (inputs > 0)
                pins.push_back({ L"IN",  PinDir::Input,  Side::Left, 0, 1 });
            else
                pins.push_back({ L"OUT", PinDir::Output, Side::Right, 0, 1 });
            break;

        case ComponentKind::Logic:
        default:
            for (int i = 0; i < inputs; ++i)
                pins.push_back({ InputPinName(i), PinDir::Input, Side::Left, i, inputs });

            for (int i = 0; i < outputs; ++i)
                pins.push_back({ OutputPinName(compName, i), PinDir::Output, Side::Right, i, outputs });
            break;
    }

    // 引脚序号：按生成顺序 1,2,3…（PCB 网表按序号引用引脚）
    for (size_t i = 0; i < pins.size(); ++i)
        pins[i].number = static_cast<int>(i) + 1;

    return pins;
}

// ============================================================
//  并查集
// ============================================================

namespace
{

class Dsu
{
public:
    void Init(int n)
    {
        m_parent.resize(n);

        for (int i = 0; i < n; ++i)
            m_parent[i] = i;
    }

    int Find(int x)
    {
        while (m_parent[x] != x)
        {
            m_parent[x] = m_parent[m_parent[x]];
            x = m_parent[x];
        }

        return x;
    }

    void Unite(int a, int b)
    {
        a = Find(a);
        b = Find(b);

        if (a != b)
            m_parent[a] = b;
    }

private:
    std::vector<int> m_parent;
};

} // namespace

// ============================================================
//  连通性求解
// ============================================================

NetGraph NetBuilder::Build(const std::vector<ComponentInstance>& components,
                           const std::vector<Wire>&              wires)
{
    NetGraph graph;

    // --------------------------------------------------------
    // 1) 把所有“连接点”登记成图节点
    //    同坐标的点共用一个节点，因此“端点重合”天然连通
    // --------------------------------------------------------
    std::map<std::pair<int, int>, int> pointIndex;
    std::vector<wxPoint>               points;

    auto NodeOf = [&](const wxPoint& p) -> int
    {
        const std::pair<int, int> key(p.x, p.y);
        const auto it = pointIndex.find(key);

        if (it != pointIndex.end())
            return it->second;

        const int id = static_cast<int>(points.size());
        pointIndex[key] = id;
        points.push_back(p);

        return id;
    };

    // 引脚 → 节点
    graph.pinNet.assign(components.size(), std::vector<int>());

    std::vector<PinRef> pinRefs;      // 与下面 pinNode 一一对应
    std::vector<int>    pinNode;
    std::vector<int>    pinCompIndex;

    for (size_t c = 0; c < components.size(); ++c)
    {
        const ComponentInstance& comp = components[c];

        graph.pinNet[c].assign(comp.pins.size(), -1);

        for (size_t k = 0; k < comp.pins.size(); ++k)
        {
            PinRef ref;
            ref.compIndex = static_cast<int>(c);
            ref.pinIndex  = static_cast<int>(k);
            ref.refdes    = comp.refdes;
            ref.name      = comp.pins[k].name;
            ref.pos       = PinPosition(comp, static_cast<int>(k));

            pinRefs.push_back(ref);
            pinCompIndex.push_back(static_cast<int>(c));
            pinNode.push_back(NodeOf(ref.pos));
        }
    }

    // 导线端点 → 节点
    std::vector<int> wireNodeA(wires.size());
    std::vector<int> wireNodeB(wires.size());

    for (size_t i = 0; i < wires.size(); ++i)
    {
        wireNodeA[i] = NodeOf(wires[i].a);
        wireNodeB[i] = NodeOf(wires[i].b);
    }

    // --------------------------------------------------------
    // 2) 并查集建图
    // --------------------------------------------------------
    Dsu dsu;
    dsu.Init(static_cast<int>(points.size()));

    // 2.1 每条导线把自己的两端连起来
    for (size_t i = 0; i < wires.size(); ++i)
        dsu.Unite(wireNodeA[i], wireNodeB[i]);

    // 2.2 T 型连接：任何“点”（引脚 / 导线端点）落在某条导线上，即与该导线连通
    //     注意：只处理严格落在线段内部的情况；十字交叉处没有点，
    //          因此不会误连通（要连通必须放连接点，而连接点也是一个“点”）
    const size_t nodeCount = points.size();

    for (size_t w = 0; w < wires.size(); ++w)
    {
        const wxPoint& a = wires[w].a;
        const wxPoint& b = wires[w].b;

        for (size_t n = 0; n < nodeCount; ++n)
        {
            const wxPoint& p = points[n];

            if (p == a || p == b)
                continue;

            if (PointOnSegment(p, a, b))
                dsu.Unite(static_cast<int>(n), wireNodeA[w]);
        }
    }

    // --------------------------------------------------------
    // 3) 按连通分量归并出网络
    // --------------------------------------------------------
    std::map<int, int> rootToNet;

    // 3.1 先把带引脚的分量建出来（保证网络编号只给真正有引脚的网络）
    for (size_t i = 0; i < pinRefs.size(); ++i)
    {
        const int root = dsu.Find(pinNode[i]);

        if (rootToNet.find(root) == rootToNet.end())
        {
            Net net;
            net.index = static_cast<int>(rootToNet.size()) + 1;
            net.name  = wxString::Format(L"N%d", net.index);

            rootToNet[root] = static_cast<int>(graph.nets.size());
            graph.nets.push_back(net);
        }

        const int netIdx = rootToNet[root];

        graph.nets[netIdx].pins.push_back(pinRefs[i]);
        graph.pinNet[pinCompIndex[i]][pinRefs[i].pinIndex] = netIdx;
    }

    // 3.2 收集每个网络的连接点（用于高亮显示）
    for (size_t n = 0; n < points.size(); ++n)
    {
        const int root = dsu.Find(static_cast<int>(n));
        const auto it  = rootToNet.find(root);

        if (it != rootToNet.end())
            graph.nets[it->second].nodes.push_back(points[n]);
    }

    // 3.3 电源网络改名：连到 GND / VCC 的网络直接用电源符号名
    for (Net& net : graph.nets)
    {
        for (const PinRef& ref : net.pins)
        {
            const int c = ref.compIndex;

            if (c < 0 || c >= static_cast<int>(components.size()))
                continue;

            const wxString& compName = components[c].name;

            if (compName == L"GND" || compName == L"VCC")
            {
                net.name    = compName;
                net.isPower = true;
                break;
            }
        }
    }

    // 3.4 导线 → 网络
    graph.wireNet.assign(wires.size(), -1);

    for (size_t w = 0; w < wires.size(); ++w)
    {
        const int root = dsu.Find(wireNodeA[w]);
        const auto it  = rootToNet.find(root);

        if (it != rootToNet.end())
            graph.wireNet[w] = it->second;
    }

    // --------------------------------------------------------
    // 4) 连接点（junction）判定
    //
    //    某个点上“接触”的导线数 >= 3 就画实心点。
    //    接触数 = 端点在此的导线数 + 引脚数 + 2 × 穿过此点的导线数
    //    （穿过算两根，因为左右各算一股）
    // --------------------------------------------------------
    for (size_t n = 0; n < points.size(); ++n)
    {
        const wxPoint& p = points[n];

        int touch = 0;

        for (size_t w = 0; w < wires.size(); ++w)
        {
            if (wires[w].a == p || wires[w].b == p)
                ++touch;
            else if (PointOnSegment(p, wires[w].a, wires[w].b))
                touch += 2;
        }

        for (const PinRef& ref : pinRefs)
        {
            if (ref.pos == p)
                ++touch;
        }

        if (touch >= 3)
            graph.junctions.push_back(p);
    }

    return graph;
}

// ============================================================
//  网表文本
//
//  格式说明（本工程自定义的“标准网表”，便于 PCB / 仿真后端解析）：
//    * 以 * 开头的是注释行
//    * [网络清单] 每行：* [网络名] 位号-引脚 位号-引脚 ...
//    * [元件清单] 每个元件一段：位号 元件名，随后每行一个 "引脚 -> 网络"
//  将来要换成 Protel / KiCad / EDIF 格式，只需替换本函数，
//  模型层与界面层都不用动。
// ============================================================

wxString NetBuilder::ToNetlist(const std::vector<ComponentInstance>& components,
                               const NetGraph&                       graph)
{
    wxString text;

    text << L"* ==========================================================\n";
    text << L"*  SchematicEditor 电路网络表 (Netlist)\n";
    text << L"*  生成时间 : " << wxDateTime::Now().FormatISOCombined(' ') << L"\n";
    text << L"*  元件数   : " << static_cast<int>(components.size()) << L"\n";
    text << L"*  网络数   : " << static_cast<int>(graph.nets.size()) << L"\n";
    text << L"*  导线数   : " << static_cast<int>(graph.wireNet.size()) << L"\n";
    text << L"* ==========================================================\n\n";

    // ---- 网络清单 ----
    text << L"* ---------------- 网络清单 (NETS) ----------------\n";

    for (const Net& net : graph.nets)
    {
        text << L"* [" << net.name << L"]";

        for (const PinRef& ref : net.pins)
            text << L" " << ref.refdes << L"-" << ref.name;

        if (net.isPower)
            text << L"   (电源网络)";

        text << L"\n";
    }

    text << L"\n";

    // ---- 元件清单 ----
    text << L"* ---------------- 元件清单 (ELEMENTS) ----------------\n";

    for (size_t c = 0; c < components.size(); ++c)
    {
        const ComponentInstance& comp = components[c];

        text << L"* " << comp.refdes << L"  " << comp.name
             << L"   @ (" << comp.pos.x << L", " << comp.pos.y << L")\n";

        for (size_t k = 0; k < comp.pins.size(); ++k)
        {
            const int netIdx = (c < graph.pinNet.size() && k < graph.pinNet[c].size())
                                   ? graph.pinNet[c][k]
                                   : -1;

            const wxString netName = (netIdx >= 0 && netIdx < static_cast<int>(graph.nets.size()))
                                         ? graph.nets[netIdx].name
                                         : wxString(L"<未连接>");

            text << L"*     " << comp.pins[k].name << L" -> " << netName << L"\n";
        }

        text << L"*\n";
    }

    // ---- SPICE 风格元件行（节点顺序 = 引脚顺序）----
    text << L"* ---------------- SPICE 风格元件行 ----------------\n";

    for (size_t c = 0; c < components.size(); ++c)
    {
        const ComponentInstance& comp = components[c];

        text << L"X" << comp.refdes;

        for (size_t k = 0; k < comp.pins.size(); ++k)
        {
            const int netIdx = (c < graph.pinNet.size() && k < graph.pinNet[c].size())
                                   ? graph.pinNet[c][k]
                                   : -1;

            const wxString netName = (netIdx >= 0 && netIdx < static_cast<int>(graph.nets.size()))
                                         ? graph.nets[netIdx].name
                                         : wxString::Format(L"NC_%s_%s", comp.refdes, comp.pins[k].name);

            text << L" " << netName;
        }

        text << L" " << comp.name << L"\n";
    }

    text << L"* ---------------- END ----------------\n";

    return text;
}

// ============================================================
//  标准 PCB 网表（Protel / Altium 格式）
//
//  格式说明（Altium、KiCad 都能直接导入这个格式）：
//      [ 位号  封装  元件值 ]        每个元件一段
//      ( 网络名  位号-引脚号 ... )   每个网络一段
//
//  两点设计约定：
//    * 封装为空的元件（电源符号、网络标签）不写进网表 ——
//      它们在 PCB 上不是实体元件，只负责给网络起名。
//    * 只输出"至少有两个引脚"的网络 —— 单引脚网络在 PCB 上没有意义
//      （会由 ERC 报出来）。
// ============================================================

wxString NetBuilder::ToProtel(const std::vector<ComponentInstance>& components,
                              const NetGraph&                       graph)
{
    wxString text;

    // 判断某个元件是否"物理元件"（有封装）
    auto IsPhysical = [&](int compIndex) -> bool
    {
        if (compIndex < 0 || compIndex >= static_cast<int>(components.size()))
            return false;

        wxString footprint = components[compIndex].footprint;   // Trim() 非 const，先拷一份

        return !footprint.Trim().IsEmpty();
    };

    // ---------------- 元件段 [ ... ] ----------------
    for (size_t c = 0; c < components.size(); ++c)
    {
        const ComponentInstance& comp = components[c];

        if (!IsPhysical(static_cast<int>(c)))
            continue;

        text << L"[\n";
        text << comp.refdes << L"\n";
        text << comp.footprint << L"\n";
        text << (comp.value.IsEmpty() ? comp.name : comp.value) << L"\n";
        text << L"]\n";
    }

    // ---------------- 网络段 ( ... ) ----------------
    for (const Net& net : graph.nets)
    {
        // 只统计物理元件的引脚
        std::vector<wxString> nodes;

        for (const PinRef& ref : net.pins)
        {
            if (!IsPhysical(ref.compIndex))
                continue;

            const ComponentInstance& comp = components[ref.compIndex];

            const int pinNumber = (ref.pinIndex >= 0 &&
                                   ref.pinIndex < static_cast<int>(comp.pins.size()))
                                      ? comp.pins[ref.pinIndex].number
                                      : (ref.pinIndex + 1);

            nodes.push_back(wxString::Format(L"%s-%d", comp.refdes, pinNumber));
        }

        if (nodes.size() < 2)
            continue;

        text << L"(\n";
        text << net.name << L"\n";

        for (const wxString& node : nodes)
            text << node << L"\n";

        text << L")\n";
    }

    return text;
}

// ============================================================
//  电气规则检查
// ============================================================

wxString NetBuilder::RunErc(const std::vector<ComponentInstance>& components,
                            const std::vector<Wire>&              wires,
                            const NetGraph&                       graph)
{
    wxString report;

    int issues = 0;

    report << L"电气规则检查 (ERC) 报告\n";
    report << L"========================================\n\n";

    // ---- 1. 悬空引脚 ----
    report << L"[1] 悬空引脚（未连接任何导线）\n";

    for (size_t c = 0; c < components.size(); ++c)
    {
        for (size_t k = 0; k < components[c].pins.size(); ++k)
        {
            const int netIdx = (c < graph.pinNet.size() && k < graph.pinNet[c].size())
                                   ? graph.pinNet[c][k]
                                   : -1;

            // 网络里只有这一个引脚，说明没接到别的东西
            const bool floating =
                (netIdx < 0) ||
                (netIdx < static_cast<int>(graph.nets.size()) &&
                 graph.nets[netIdx].pins.size() < 2);

            if (floating)
            {
                ++issues;
                report << wxString::Format(L"    - %s 的引脚 %s 悬空\n",
                                           components[c].refdes,
                                           components[c].pins[k].name);
            }
        }
    }

    if (issues == 0)
        report << L"    （无）\n";

    // ---- 2. 悬空导线端点 ----
    report << L"\n[2] 悬空的导线端点（没有接到引脚或别的导线）\n";

    int danglingEnds = 0;

    for (size_t w = 0; w < wires.size(); ++w)
    {
        const wxPoint ends[2] = { wires[w].a, wires[w].b };

        for (const wxPoint& p : ends)
        {
            if (IsEndpointConnected(components, wires, p, static_cast<int>(w)))
                continue;

            ++danglingEnds;
            ++issues;

            report << wxString::Format(L"    - 导线 #%d 的端点 (%d,%d) 悬空\n",
                                       wires[w].id, p.x, p.y);
        }
    }

    if (danglingEnds == 0)
        report << L"    （无）\n";

    // ---- 3. 只有单个引脚的“网络” ----
    report << L"\n[3] 只连了一个引脚的孤立网络\n";

    int lonely = 0;

    for (const Net& net : graph.nets)
    {
        if (net.pins.size() < 2)
        {
            ++lonely;

            report << wxString::Format(L"    - %s : ", net.name);

            for (const PinRef& ref : net.pins)
                report << ref.refdes << L"-" << ref.name << L" ";

            report << L"\n";
        }
    }

    if (lonely == 0)
        report << L"    （无）\n";

    report << L"\n========================================\n";

    if (issues == 0)
        report << L"结论：未发现问题。\n";
    else
        report << wxString::Format(L"结论：共发现 %d 处需要确认的连接。\n", issues);

    return report;
}

} // namespace se

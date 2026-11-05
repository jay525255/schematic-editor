#include "SimModel.h"

#include <algorithm>

namespace se
{

// ============================================================
//  逻辑值文本
// ============================================================
wxString LogicText(LogicValue v)
{
    switch (v)
    {
        case LogicValue::L: return L"0";
        case LogicValue::H: return L"1";
        default:            return L"X";
    }
}

// ============================================================
//  元件名 → 仿真行为
//
//  直接用元件名判定，而不是给数据结构加字段：
//  元件名本来就随 .ses 文件保存，改成查表后不必再改文件格式。
// ============================================================
SimKind SimKindOf(const wxString& compName)
{
    // ---- 逻辑门 ----
    if (compName == L"与门")   return SimKind::And;
    if (compName == L"或门")   return SimKind::Or;
    if (compName == L"非门")   return SimKind::Not;
    if (compName == L"与非门") return SimKind::Nand;
    if (compName == L"或非门") return SimKind::Nor;
    if (compName == L"异或门") return SimKind::Xor;
    if (compName == L"同或门") return SimKind::Xnor;

    if (compName == L"半加器") return SimKind::HalfAdder;
    if (compName == L"全加器") return SimKind::FullAdder;

    // ---- 驱动源 ----
    if (compName == L"输入引脚") return SimKind::Source;
    if (compName == L"常量")     return SimKind::Source;
    if (compName == L"时钟")     return SimKind::Clock;

    // ---- 观察端 ----
    if (compName == L"输出引脚") return SimKind::Sink;
    if (compName == L"探针")     return SimKind::Sink;
    if (compName == L"网络标签") return SimKind::Sink;
    if (compName == L"全局标签") return SimKind::Sink;

    // ---- 用户自定义元件 ----
    //  components.xml 里可以自己加门（例如“三输入与门”“四输入或门”），
    //  名称不完全等于内置名字，所以这里再按“名字里含哪种门”兜一层。
    //  顺序仍然要“复合门在前”，否则“与非门”会先命中“与门”。
    if (compName.Contains(L"与非门")) return SimKind::Nand;
    if (compName.Contains(L"或非门")) return SimKind::Nor;
    if (compName.Contains(L"同或门")) return SimKind::Xnor;
    if (compName.Contains(L"异或门")) return SimKind::Xor;
    if (compName.Contains(L"与门"))   return SimKind::And;
    if (compName.Contains(L"或门"))   return SimKind::Or;
    if (compName.Contains(L"非门"))   return SimKind::Not;

    if (compName.Contains(L"半加器")) return SimKind::HalfAdder;
    if (compName.Contains(L"全加器")) return SimKind::FullAdder;

    // 其余（电阻/电容/电感/二极管/LED/三极管/运放/电源符号）不参与逻辑仿真
    return SimKind::None;
}

bool IsSimulatable(const wxString& compName)
{
    return SimKindOf(compName) != SimKind::None;
}

// ============================================================
//  三值逻辑运算
// ============================================================
namespace
{

LogicValue DoNot(LogicValue a)
{
    if (a == LogicValue::L) return LogicValue::H;
    if (a == LogicValue::H) return LogicValue::L;
    return LogicValue::X;
}

LogicValue DoAnd(LogicValue a, LogicValue b)
{
    if (a == LogicValue::L || b == LogicValue::L) return LogicValue::L;
    if (a == LogicValue::H && b == LogicValue::H) return LogicValue::H;
    return LogicValue::X;
}

LogicValue DoOr(LogicValue a, LogicValue b)
{
    if (a == LogicValue::H || b == LogicValue::H) return LogicValue::H;
    if (a == LogicValue::L && b == LogicValue::L) return LogicValue::L;
    return LogicValue::X;
}

LogicValue DoXor(LogicValue a, LogicValue b)
{
    if (a == LogicValue::X || b == LogicValue::X) return LogicValue::X;

    const bool av = (a == LogicValue::H);
    const bool bv = (b == LogicValue::H);

    return (av != bv) ? LogicValue::H : LogicValue::L;
}

// 把输入表折叠成两输入运算（多输入门用）
LogicValue Fold(const std::vector<LogicValue>& args,
                LogicValue (*op)(LogicValue, LogicValue),
                LogicValue identity)
{
    if (args.empty())
        return identity;

    LogicValue acc = args[0];

    for (size_t i = 1; i < args.size(); ++i)
        acc = op(acc, args[i]);

    return acc;
}

} // namespace

// ============================================================
//  仿真器
// ============================================================

void Simulator::Reset(int componentCount)
{
    m_inputValues.assign(std::max(0, componentCount), LogicValue::L);
    m_netValues.clear();
    m_clockHigh      = false;
    m_hasConflict    = false;
    m_unresolvedNets = 0;
    m_passes         = 0;
}

LogicValue Simulator::NetValue(int netIndex) const
{
    if (netIndex < 0 || netIndex >= static_cast<int>(m_netValues.size()))
        return LogicValue::X;

    return m_netValues[netIndex];
}

LogicValue Simulator::InputValue(int compIndex) const
{
    if (compIndex < 0 || compIndex >= static_cast<int>(m_inputValues.size()))
        return LogicValue::L;

    return m_inputValues[compIndex];
}

void Simulator::SetInputValue(int compIndex, LogicValue value)
{
    if (compIndex < 0 || compIndex >= static_cast<int>(m_inputValues.size()))
        return;

    m_inputValues[compIndex] = value;
}

void Simulator::ToggleInputValue(int compIndex)
{
    if (compIndex < 0 || compIndex >= static_cast<int>(m_inputValues.size()))
        return;

    // L → H → L 循环
    m_inputValues[compIndex] = (m_inputValues[compIndex] == LogicValue::H)
                                   ? LogicValue::L
                                   : LogicValue::H;
}

// ------------------------------------------------------------
bool Simulator::Evaluate(const std::vector<ComponentInstance>& components,
                         const NetGraph&                       graph)
{
    const int netCount = static_cast<int>(graph.nets.size());

    m_netValues.assign(std::max(0, netCount), LogicValue::X);
    m_hasConflict    = false;
    m_unresolvedNets = 0;
    m_passes         = 0;

    if (m_inputValues.size() != components.size())
        m_inputValues.resize(components.size(), LogicValue::L);

    // --------------------------------------------------------
    // 1) 统计每个网络有几个"驱动者"（元件输出引脚）
    //    多于一个就是多驱动冲突，该网络恒为 X
    // --------------------------------------------------------
    std::vector<int> drivers(static_cast<size_t>(std::max(0, netCount)), 0);

    for (size_t c = 0; c < components.size(); ++c)
    {
        const ComponentInstance& comp = components[c];

        for (size_t k = 0; k < comp.pins.size(); ++k)
        {
            if (comp.pins[k].dir != PinDir::Output)
                continue;

            if (c >= graph.pinNet.size() || k >= graph.pinNet[c].size())
                continue;

            const int net = graph.pinNet[c][k];

            if (net >= 0 && net < netCount)
                ++drivers[net];
        }
    }

    std::vector<bool> conflicted(static_cast<size_t>(std::max(0, netCount)), false);

    for (int n = 0; n < netCount; ++n)
    {
        if (drivers[n] > 1)
        {
            conflicted[n] = true;
            m_hasConflict = true;
        }
    }

    // --------------------------------------------------------
    // 2) 定点迭代
    // --------------------------------------------------------
    const int maxPass = std::max(30, static_cast<int>(components.size()) * 6 + 20);

    bool converged = false;

    for (int pass = 0; pass < maxPass; ++pass)
    {
        m_passes = pass + 1;

        bool changed = false;

        for (size_t c = 0; c < components.size(); ++c)
        {
            const ComponentInstance& comp = components[c];
            const SimKind kind = SimKindOf(comp.name);

            if (kind == SimKind::None || kind == SimKind::Sink)
                continue;

            // ---- 收集输入引脚的当前值 ----
            std::vector<LogicValue> inputs;

            for (size_t k = 0; k < comp.pins.size(); ++k)
            {
                if (comp.pins[k].dir != PinDir::Input)
                    continue;

                const int net = (c < graph.pinNet.size() && k < graph.pinNet[c].size())
                                    ? graph.pinNet[c][k]
                                    : -1;

                inputs.push_back(NetValue(net));
            }

            // ---- 算出输出值 ----
            std::vector<LogicValue> outputs;

            switch (kind)
            {
                case SimKind::And:  outputs.push_back(Fold(inputs, DoAnd, LogicValue::H)); break;
                case SimKind::Or:   outputs.push_back(Fold(inputs, DoOr,  LogicValue::L)); break;
                case SimKind::Nand: outputs.push_back(DoNot(Fold(inputs, DoAnd, LogicValue::H))); break;
                case SimKind::Nor:  outputs.push_back(DoNot(Fold(inputs, DoOr,  LogicValue::L))); break;
                case SimKind::Xor:  outputs.push_back(Fold(inputs, DoXor, LogicValue::L)); break;
                case SimKind::Xnor: outputs.push_back(DoNot(Fold(inputs, DoXor, LogicValue::L))); break;
                case SimKind::Not:  outputs.push_back(DoNot(inputs.empty() ? LogicValue::X : inputs[0])); break;

                case SimKind::HalfAdder:
                {
                    const LogicValue a = inputs.size() > 0 ? inputs[0] : LogicValue::X;
                    const LogicValue b = inputs.size() > 1 ? inputs[1] : LogicValue::X;

                    outputs.push_back(DoXor(a, b));    // S
                    outputs.push_back(DoAnd(a, b));    // C
                    break;
                }

                case SimKind::FullAdder:
                {
                    const LogicValue a = inputs.size() > 0 ? inputs[0] : LogicValue::X;
                    const LogicValue b = inputs.size() > 1 ? inputs[1] : LogicValue::X;
                    const LogicValue ci = inputs.size() > 2 ? inputs[2] : LogicValue::X;

                    outputs.push_back(DoXor(DoXor(a, b), ci));                  // S
                    outputs.push_back(DoOr(DoAnd(a, b),
                                           DoAnd(ci, DoXor(a, b))));           // Cout
                    break;
                }

                case SimKind::Source:
                    outputs.push_back(InputValue(static_cast<int>(c)));
                    break;

                case SimKind::Clock:
                    outputs.push_back(m_clockHigh ? LogicValue::H : LogicValue::L);
                    break;

                default:
                    break;
            }

            // ---- 写到输出引脚所在的网络 ----
            size_t outIdx = 0;

            for (size_t k = 0; k < comp.pins.size() && outIdx < outputs.size(); ++k)
            {
                if (comp.pins[k].dir != PinDir::Output)
                    continue;

                const int net = (c < graph.pinNet.size() && k < graph.pinNet[c].size())
                                    ? graph.pinNet[c][k]
                                    : -1;

                const LogicValue value = outputs[outIdx++];

                if (net < 0 || net >= netCount)
                    continue;

                if (conflicted[net])
                    continue;      // 多驱动网络保持 X

                if (m_netValues[net] != value)
                {
                    m_netValues[net] = value;
                    changed = true;
                }
            }
        }

        if (!changed)
        {
            converged = true;
            break;
        }
    }

    // --------------------------------------------------------
    // 3) 统计仍未确定的网络（用于报告）
    // --------------------------------------------------------
    for (int n = 0; n < netCount; ++n)
    {
        if (m_netValues[n] == LogicValue::X)
            ++m_unresolvedNets;
    }

    return converged;
}

// ------------------------------------------------------------
wxString Simulator::BuildReport(const std::vector<ComponentInstance>& components,
                                const NetGraph&                       graph) const
{
    wxString text;

    text << L"电路仿真报告\n";
    text << L"========================================\n\n";

    text << wxString::Format(L"仿真状态   : %s\n", m_running ? L"运行中" : L"已停止");
    text << wxString::Format(L"元件总数   : %d\n", static_cast<int>(components.size()));
    text << wxString::Format(L"网络总数   : %d\n", static_cast<int>(graph.nets.size()));
    text << wxString::Format(L"迭代次数   : %d\n", m_passes);
    text << wxString::Format(L"时钟相位   : %s\n", m_clockHigh ? L"高电平" : L"低电平");
    text << wxString::Format(L"未知网络数 : %d\n", m_unresolvedNets);

    if (m_hasConflict)
        text << L"\n【警告】存在多驱动网络（多个元件输出脚接在同一网络上），该网络恒为 X。\n";

    if (m_unresolvedNets > 0 && !m_hasConflict)
        text << L"\n【提示】部分网络为 X（未知），通常是因为：未接输入、"
                L"接入了不支持逻辑仿真的器件（电阻/电容/二极管/三极管/运放），"
                L"或电路存在组合环。\n";

    // ---- 网络值表 ----
    text << L"\n---------------- 网络逻辑值 ----------------\n";

    for (size_t n = 0; n < graph.nets.size(); ++n)
    {
        const Net& net = graph.nets[n];
        const int  index = static_cast<int>(n);

        text << wxString::Format(L"  %-6s = %s", net.name, LogicText(NetValue(index)));

        if (!net.pins.empty())
        {
            text << L"     (";

            for (size_t i = 0; i < net.pins.size(); ++i)
            {
                if (i > 0) text << L", ";
                text << net.pins[i].refdes << L"-" << net.pins[i].name;
            }

            text << L")";
        }

        text << L"\n";
    }

    // ---- 输入 / 输出端口一览 ----
    text << L"\n---------------- 端口逻辑值 ----------------\n";

    for (size_t c = 0; c < components.size(); ++c)
    {
        const ComponentInstance& comp = components[c];
        const SimKind kind = SimKindOf(comp.name);

        if (kind == SimKind::Source)
        {
            // 找出它的输出脚所在的网络
            int net = -1;

            for (size_t k = 0; k < comp.pins.size(); ++k)
            {
                if (comp.pins[k].dir == PinDir::Output)
                {
                    net = (c < graph.pinNet.size() && k < graph.pinNet[c].size())
                              ? graph.pinNet[c][k] : -1;
                    break;
                }
            }

            text << wxString::Format(L"  [输入] %-6s = %s   (可点击切换)\n",
                                     comp.refdes, LogicText(InputValue(static_cast<int>(c))));
        }
        else if (kind == SimKind::Clock)
        {
            text << wxString::Format(L"  [时钟] %-6s = %s\n",
                                     comp.refdes, m_clockHigh ? L"1" : L"0");
        }
        else if (kind == SimKind::Sink && comp.name == L"输出引脚")
        {
            int net = -1;

            for (size_t k = 0; k < comp.pins.size(); ++k)
            {
                if (comp.pins[k].dir == PinDir::Input)
                {
                    net = (c < graph.pinNet.size() && k < graph.pinNet[c].size())
                              ? graph.pinNet[c][k] : -1;
                    break;
                }
            }

            text << wxString::Format(L"  [输出] %-6s = %s\n",
                                     comp.refdes, LogicText(NetValue(net)));
        }
    }

    // ---- 不支持仿真的器件 ----
    wxString unsupported;

    for (const ComponentInstance& comp : components)
    {
        if (!IsSimulatable(comp.name))
            unsupported << L"  " << comp.refdes << L" (" << comp.name << L")\n";
    }

    if (!unsupported.IsEmpty())
    {
        text << L"\n---------------- 不支持逻辑仿真的器件 ----------------\n";
        text << L"（模拟/无源/半导体器件，其输出网络记为 X）\n";
        text << unsupported;
    }

    text << L"\n========================================\n";

    return text;
}

} // namespace se

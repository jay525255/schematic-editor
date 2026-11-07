// Steady-state evaluator plus a timed event-driven runtime. GUI uses the timed runtime.
#pragma once

#include <wx/string.h>

#include <vector>

#include "NetModel.h"
#include "DigitalSimulator.h"

namespace se
{

// ------------------------------------------------------------
// 逻辑值：0 / 1 / 未知
// ------------------------------------------------------------
enum class LogicValue : unsigned char
{
    X = 0,   // 未知（未驱动 / 冲突 / 不支持的器件）
    L = 1,   // 逻辑 0
    H = 2    // 逻辑 1
};

wxString LogicText(LogicValue v);       // "0" / "1" / "X"

// ------------------------------------------------------------
// 元件的仿真行为
// ------------------------------------------------------------
enum class SimKind
{
    None,          // 不参与逻辑仿真（无源/半导体/模拟器件）
    And, Or, Not, Nand, Nor, Xor, Xnor,
    HalfAdder,     // 输出 S, C
    FullAdder,     // 输出 S, Cout
    Source,        // 驱动一个常量：输入引脚 / 常量
    Clock,         // 驱动一个方波
    Sink           // 只读取：输出引脚 / 探针 / 网络标签
};

// 按元件名判定仿真行为（元件名来自元件库，随文件保存）
SimKind SimKindOf(const wxString& compName);

// 该元件是否支持逻辑仿真（用于报告里列出"不支持的器件"）
bool IsSimulatable(const wxString& compName);

// ------------------------------------------------------------
// 仿真器
// ------------------------------------------------------------
class Simulator
{
public:
    void Reset(int componentCount);
    bool StartRuntime(const std::vector<ComponentInstance>& components, const NetGraph& graph, wxString& error);
    bool AdvanceRuntime();
    int RawNetValue(int net) const { return m_temporal ? m_runtime.Value(net) : NetValue(net) == LogicValue::H ? 1 : NetValue(net) == LogicValue::L ? 0 : -1; }
    bool RuntimeReady() const { return m_temporal && m_runtime.Ready(); }
    std::uint64_t RuntimeTime() const { return m_runtime.Time(); }


    // 求值一次。返回 false 表示未收敛（电路中有组合环或多驱动）
    bool Evaluate(const std::vector<ComponentInstance>& components,
                  const NetGraph&                       graph);

    bool IsRunning() const { return m_running; }
    void SetRunning(bool on) { m_running = on; }

    // ---- 网络逻辑值（下标对应 NetGraph::nets）----
    LogicValue NetValue(int netIndex) const;
    int        NetValueCount() const { return static_cast<int>(m_temporal ? m_runtime.Values().size() : m_netValues.size()); }

    // ---- 输入端口 / 常量 的用户设定值 ----
    LogicValue InputValue(int compIndex) const;
    void       SetInputValue(int compIndex, LogicValue value);
    void       ToggleInputValue(int compIndex);

    // ---- 时钟 ----
    bool GetClockHigh() const { return m_clockHigh; }
    void SetClockHigh(bool high) { m_clockHigh = high; }
    void TickClock() { m_clockHigh = !m_clockHigh; }

    // ---- 诊断 ----
    int  GetUnresolvedNetCount() const { return m_unresolvedNets; }
    bool HasConflict() const { return m_hasConflict; }
    int  GetEvaluationPasses() const { return m_passes; }

    // 生成一份"仿真状态报告"（网络值 + 输出端口值 + 警告）
    wxString BuildReport(const std::vector<ComponentInstance>& components,
                         const NetGraph&                       graph) const;

private:
    DigitalSimulator m_runtime;
    bool m_temporal = false;
    std::vector<int> m_runtimeIds;
    std::vector<LogicValue> m_netValues;   // 与 nets 平行
    std::vector<LogicValue> m_inputValues; // 与 components 平行（仅 Source/Clock 有意义）

    bool m_running    = false;
    bool m_clockHigh  = false;
    bool m_hasConflict = false;
    int  m_unresolvedNets = 0;
    int  m_passes     = 0;
};

} // namespace se

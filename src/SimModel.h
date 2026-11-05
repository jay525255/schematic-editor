// ============================================================
//  SimModel.h  —— 逻辑仿真引擎（任务 6）
//
//  设计要点：
//    * 仿真与界面完全解耦：只吃「元件表 + 连通性结果」，
//      输出「每个网络的逻辑值」，不碰任何绘图代码。
//    * 求值用**定点迭代**：反复让每个元件按当前输入算输出，
//      直到所有网络的值不再变化（或达到上限）。
//      对课程规模的电路（几十~几百个门）来说，
//      比写一个完整的拓扑排序 + 事件队列简单得多，
//      而且天然容忍组合环（环路会停在 X 上并报告"未收敛"）。
//    * 电源/输入端口/时钟是"驱动源"，其余是可仿真的逻辑门；
//      电阻/电容/二极管等模拟器件不参与逻辑仿真，输出记为 X（未知）。
//
//  与任务 6 的对应：
//    「动态电路信号传播」= 每次求值把变化沿导线传播到稳定状态
//    「实时逻辑仿真」    = 用户切换输入端口 / 打一拍时钟后立即重算并显示
// ============================================================

#pragma once

#include <wx/string.h>

#include <vector>

#include "NetModel.h"

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

    // 求值一次。返回 false 表示未收敛（电路中有组合环或多驱动）
    bool Evaluate(const std::vector<ComponentInstance>& components,
                  const NetGraph&                       graph);

    bool IsRunning() const { return m_running; }
    void SetRunning(bool on) { m_running = on; }

    // ---- 网络逻辑值（下标对应 NetGraph::nets）----
    LogicValue NetValue(int netIndex) const;
    int        NetValueCount() const { return static_cast<int>(m_netValues.size()); }

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
    std::vector<LogicValue> m_netValues;   // 与 nets 平行
    std::vector<LogicValue> m_inputValues; // 与 components 平行（仅 Source/Clock 有意义）

    bool m_running    = false;
    bool m_clockHigh  = false;
    bool m_hasConflict = false;
    int  m_unresolvedNets = 0;
    int  m_passes     = 0;
};

} // namespace se

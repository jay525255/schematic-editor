#pragma once
#include "NetModel.h"
#include <cstdint>
#include <map>
#include <queue>
#include <vector>
namespace se {
// Digital values: X=-1, LOW=0, HIGH=1, conflicting drivers=2.
wxString DigitalModel(const ComponentInstance& component);
std::vector<int> EvaluateDigital(const ComponentInstance& component, const std::vector<int>& inputs);
wxString LogicValueText(int value);
class DigitalSimulator {
public:
    bool Reset(const std::vector<ComponentInstance>& components, const NetGraph& graph, wxString& error);
    bool SetInput(int componentId, int value);
    bool Advance(std::uint64_t ticks=1);
    int Value(int net) const;
    int InputValue(int componentId) const;
    std::uint64_t Time() const { return m_time; }
    std::uint64_t TransitionCount() const { return m_transitions; }
    const std::vector<int>& Values() const { return m_values; }
    const wxString& Error() const { return m_error; }
    bool Ready() const { return m_ready; }
// 该模型的输出是否依赖"上一次的值"（触发器 / 计数器 / ROM / RAM）
    static bool IsSequentialModel(const wxString& model);

private:
    struct Event { std::uint64_t at, sequence, generation; size_t component; std::vector<int> outputs; };
    struct Later { bool operator()(const Event& a,const Event& b) const { return a.at!=b.at?a.at>b.at:a.sequence>b.sequence; } };

    // 时序元件的"记忆"。组合门没有这一层，所以单独放在一个数组里，
    // 和 m_components 一一对应（下标相同）。
    struct SeqState
    {
        int              q          = 0;     // 触发器当前值 / 计数器当前计数
        std::vector<int> mem;                // ROM / RAM：16 个字，每字 0..15
        int              lastClock  = 0;     // 上一个采样到的时钟电平（0/1/-1）
        int              lastWrite  = 0;     // RAM：上一个采样到的写使能
        bool             seeded     = false; // 是否已经用初值驱动过输出
    };

    void Process();
    void Resolve();
    void ScheduleSources();
    std::vector<int> EvaluateSequential(size_t index, const std::vector<int>& inputs);
    void SeedSequential(size_t index);
    std::vector<ComponentInstance> m_components;
    NetGraph m_graph;
    std::vector<int> m_values;
    std::vector<std::vector<int>> m_drives, m_inputs;
    std::vector<std::uint64_t> m_generation;
    std::vector<SeqState> m_seq;
    std::map<int,int> m_external;
    std::priority_queue<Event,std::vector<Event>,Later> m_events;
    std::uint64_t m_time=0,m_sequence=0,m_transitions=0;
    bool m_ready=false;
    wxString m_error;
};
}

// ============================================================
//  WaveformWindow.h —— 仿真波形窗口
//
//  为什么做成"独立窗口"而不是画布里的一个面板：
//    * 波形是"时间轴"的东西，横着要长，和原理图抢横向空间没有好处；
//    * 独立窗口可以和主窗口并排（甚至拖到第二块屏），
//      改电路 → 看波形，两边同时可见，不用来回切。
//
//  数据从哪来：SimModel::Simulator 在每跑一步时记录一份各网络的逻辑值
//  （见 Simulator::RecordWave）。这里只负责"把已经记下来的东西画出来"，
//  不参与仿真，所以关掉窗口不会影响仿真，反过来也不会拖慢仿真。
// ============================================================

#pragma once

#include <wx/frame.h>
#include <wx/timer.h>

#include <functional>

namespace se
{

class Simulator;

class WaveformWindow : public wxFrame
{
public:
    // 用回调而不是指针：仿真器属于画布，画布可能被替换 / 重置，
    // 每帧现取一次才不会指向已经析构的对象。
    using Provider = std::function<const Simulator*()>;

    WaveformWindow(wxWindow* parent, Provider provider);

    // 仿真重启、单步、暂停后调一下（重算滚动范围并重画）
    void RefreshView();

private:
    class Canvas;

    void OnTimer(wxTimerEvent& event);

    Canvas*        m_canvas   = nullptr;
    wxTimer        m_timer;
    Provider       m_provider;
};

} // namespace se
#include "WaveformWindow.h"

#include <wx/dcbuffer.h>
#include <wx/sizer.h>
#include <wx/scrolwin.h>

#include <algorithm>

#include "SimModel.h"

namespace se
{

namespace
{

const int kRowHeight = 34;    // 一个信号占的高度
const int kLabelWidth = 132;  // 左边信号名一栏的宽度
const int kSampleWidth = 6;   // 一个采样点占的横向像素
const int kTopMargin = 22;    // 顶部时间刻度带

// 逻辑值 → 颜色（与画布上的着色保持一致）
wxColour ColourOf(int value)
{
    if (value == 0) return wxColour(90, 100, 120);    // 低：灰蓝
    if (value == 1) return wxColour(0, 150, 60);      // 高：绿
    if (value == 2) return wxColour(200, 50, 50);     // 冲突：红
    return wxColour(190, 150, 40);                    // X：琥珀
}

} // namespace

// ------------------------------------------------------------
//  自绘画布
// ------------------------------------------------------------
class WaveformWindow::Canvas : public wxScrolled<wxPanel>
{
public:
    Canvas(wxWindow* parent, Provider provider)
        : wxScrolled<wxPanel>(parent, wxID_ANY),
          m_provider(std::move(provider))
    {
        SetBackgroundStyle(wxBG_STYLE_PAINT);
        SetBackgroundColour(wxColour(252, 252, 250));
        SetScrollRate(12, 12);
        Bind(wxEVT_PAINT, &Canvas::OnPaint, this);
        Bind(wxEVT_LEFT_DOWN, [this](wxMouseEvent&) { SetFocus(); });
    }

    void Recompute()
    {
        const Simulator* sim = m_provider();

        int rows = sim ? sim->WaveCount() : 0;
        int samples = 0;
        if (sim && rows > 0) samples = static_cast<int>(sim->WaveSamples(0).size());

        const int width  = std::max(GetClientSize().x, kLabelWidth + samples * kSampleWidth + 40);
        const int height = kTopMargin + std::max(1, rows) * kRowHeight + 20;

        SetVirtualSize(width, height);
        Refresh();
    }

private:
    void OnPaint(wxPaintEvent&)
    {
        wxAutoBufferedPaintDC dc(this);
        DoPrepareDC(dc);

        dc.SetBackground(wxBrush(GetBackgroundColour()));
        dc.Clear();

        const Simulator* sim = m_provider();

        if (!sim || sim->WaveCount() == 0)
        {
            dc.SetTextForeground(wxColour(120, 120, 120));
            dc.SetFont(wxFontInfo(10));
            dc.DrawText(L"还没有波形：先按 F5 开始仿真（或 F6 单步），"
                        L"每跑一步这里就会自动记一条。", 16, 24);
            dc.DrawText(L"提示：波形记录最近 2000 步，跑太久会自动把最旧的丢掉。", 16, 48);
            return;
        }

        const int rows    = sim->WaveCount();
        const int samples = static_cast<int>(sim->WaveSamples(0).size());
        const int lastTick = static_cast<int>(sim->WaveFirstTick()) + samples - 1;

        // ---- 时间刻度（每 10 个采样点一条竖线，标上步数）----
        dc.SetPen(wxPen(wxColour(232, 232, 228), 1));
        for (int i = 0; i < samples; i += 10)
        {
            const int x = kLabelWidth + i * kSampleWidth;
            dc.DrawLine(x, kTopMargin - 6, x, kTopMargin + rows * kRowHeight);

            dc.SetTextForeground(wxColour(150, 150, 150));
            dc.SetFont(wxFontInfo(7));
            const int tick = static_cast<int>(sim->WaveFirstTick()) + i;
            dc.DrawText(wxString::Format("%d", tick), x + 2, 2);
            dc.SetPen(wxPen(wxColour(232, 232, 228), 1));
        }

        // ---- 每个网络一行 ----
        for (int row = 0; row < rows; ++row)
        {
            const int top = kTopMargin + row * kRowHeight;

            // 行分隔线
            dc.SetPen(wxPen(wxColour(238, 238, 234), 1));
            dc.DrawLine(0, top + kRowHeight - 1, GetVirtualSize().x, top + kRowHeight - 1);

            // 信号名
            dc.SetTextForeground(wxColour(40, 40, 40));
            dc.SetFont(wxFontInfo(9));
            const wxString label = sim->WaveLabel(row);
            dc.DrawText(label.IsEmpty() ? wxString::Format("N%d", row + 1) : label, 8, top + 9);

            // 当前值（最右端那个采样）
            const std::deque<signed char>& data = sim->WaveSamples(row);
            if (!data.empty())
            {
                const int   now   = data.back();
                const wxString text = now == 0 ? "0" : now == 1 ? "1" : now == 2 ? "ERR" : "X";
                dc.SetTextForeground(ColourOf(now));
                dc.SetFont(wxFontInfo(9).Bold());
                dc.DrawText(text, kLabelWidth - 42, top + 9);
            }

            // ---- 波形本身：阶梯线 ----
            const int yHigh = top + 7;
            const int yLow  = top + kRowHeight - 11;
            const int yMid  = (yHigh + yLow) / 2;

            dc.SetPen(wxPen(ColourOf(-1), 1, wxPENSTYLE_DOT));
            dc.DrawLine(kLabelWidth, yMid, kLabelWidth + samples * kSampleWidth, yMid);

            int previousY = -1;
            for (int i = 0; i < static_cast<int>(data.size()); ++i)
            {
                const int value = data[i];
                const int x0 = kLabelWidth + i * kSampleWidth;
                const int x1 = x0 + kSampleWidth;

                int y = yMid;
                if (value == 0) y = yLow;
                else if (value == 1) y = yHigh;

                if (value < 0 || value == 2)
                {
                    // X / 冲突：用点线画在中线，一眼能看出"这一段不确定"
                    dc.SetPen(wxPen(ColourOf(value), 1, wxPENSTYLE_DOT));
                    dc.DrawLine(x0, yMid, x1, yMid);
                    previousY = -1;
                    continue;
                }

                dc.SetPen(wxPen(ColourOf(value), 2));
                dc.DrawLine(x0, y, x1, y);

                if (previousY >= 0 && previousY != y)
                    dc.DrawLine(x0, previousY, x0, y);   // 边沿

                previousY = y;
            }
        }

        // ---- "当前时刻"竖线 ----
        const int xNow = kLabelWidth + samples * kSampleWidth;
        dc.SetPen(wxPen(wxColour(210, 90, 60), 1));
        dc.DrawLine(xNow, kTopMargin - 6, xNow, kTopMargin + rows * kRowHeight);

        dc.SetTextForeground(wxColour(210, 90, 60));
        dc.SetFont(wxFontInfo(7));
        dc.DrawText(wxString::Format("t=%d", lastTick), xNow - 26, 2);
    }

    Provider m_provider;
};

// ------------------------------------------------------------
//  窗口
// ------------------------------------------------------------
WaveformWindow::WaveformWindow(wxWindow* parent, Provider provider)
    : wxFrame(parent, wxID_ANY, L"仿真波形", wxDefaultPosition, wxSize(900, 520)),
      m_timer(this),
      m_provider(std::move(provider))
{
    SetBackgroundColour(wxColour(252, 252, 250));

    m_canvas = new Canvas(this, m_provider);

    wxBoxSizer* sizer = new wxBoxSizer(wxVERTICAL);
    sizer->Add(m_canvas, 1, wxEXPAND);
    SetSizer(sizer);

    CreateStatusBar();
    SetStatusText(L"每 100 ms 刷新一次 · 只显示最近 2000 步 · 关闭窗口不影响仿真");

    m_timer.Bind(wxEVT_TIMER, &WaveformWindow::OnTimer, this);
    m_timer.Start(100);

    Bind(wxEVT_CLOSE_WINDOW, [this](wxCloseEvent& event) {
        m_timer.Stop();
        // 只是隐藏，保留窗口对象：下次打开时波形还在
        event.Veto();
        Hide();
    });

    Layout();
    RefreshView();
}

void WaveformWindow::RefreshView()
{
    if (m_canvas) m_canvas->Recompute();
}

void WaveformWindow::OnTimer(wxTimerEvent&)
{
    if (IsShown()) RefreshView();
}

} // namespace se
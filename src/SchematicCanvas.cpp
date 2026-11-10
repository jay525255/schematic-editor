#include "SchematicCanvas.h"
#include "CircuitTools.h"
#include "EditingTools.h"
#include "KiCadExport.h"

#include <wx/dcbuffer.h>
#include <wx/graphics.h>

#include <algorithm>
#include <cmath>
#include <memory>

#include "Ids.h"
#include "SymbolPainter.h"

namespace se
{

// ============================================================
//  局部工具
// ============================================================
namespace
{

// 点到线段的距离
double DistanceToSegment(const wxPoint& p, const wxPoint& a, const wxPoint& b)
{
    const double dx = b.x - a.x;
    const double dy = b.y - a.y;
    const double len2 = dx * dx + dy * dy;

    if (len2 <= 0.0)
        return std::hypot(static_cast<double>(p.x - a.x), static_cast<double>(p.y - a.y));

    double t = ((p.x - a.x) * dx + (p.y - a.y) * dy) / len2;
    t = std::max(0.0, std::min(1.0, t));

    const double px = a.x + t * dx;
    const double py = a.y + t * dy;

    return std::hypot(p.x - px, p.y - py);
}

const wxColour kWireColour    (20, 130, 60);
const wxColour kWireHotColour (230, 120, 20);
const wxColour kWireSelColour (200, 40, 40);
const wxColour kDanglingColour(220, 60, 60);

// 仿真时导线按逻辑值着色：1 = 亮绿，0 = 深灰蓝，X = 琥珀
wxColour NetColourFor(LogicValue v)
{
    switch (v)
    {
        case LogicValue::H: return wxColour(0, 150, 60);
        case LogicValue::L: return wxColour(90, 100, 120);
        default:            return wxColour(190, 150, 40);
    }
}

} // namespace

// ============================================================
//  构造 / 事件绑定
// ============================================================

SchematicCanvas::SchematicCanvas(wxWindow* parent)
    : wxWindow(parent,
               wxID_ANY,
               wxDefaultPosition,
               wxDefaultSize,
               wxFULL_REPAINT_ON_RESIZE | wxWANTS_CHARS)
{
    m_simTimer.SetOwner(this);
    Bind(wxEVT_TIMER, &SchematicCanvas::OnSimulationTimer, this, m_simTimer.GetId());
    SetBackgroundStyle(wxBG_STYLE_PAINT);
    SetBackgroundColour(wxColour(250, 250, 252));
    SetCursor(wxCursor(wxCURSOR_CROSS));
    SetMinSize(wxSize(200, 150));

    Bind(wxEVT_PAINT,             &SchematicCanvas::OnPaint,          this);
    Bind(wxEVT_ERASE_BACKGROUND,  &SchematicCanvas::OnEraseBackground, this);
    Bind(wxEVT_SIZE,              &SchematicCanvas::OnSize,           this);
    Bind(wxEVT_MOTION,            &SchematicCanvas::OnMouseMove,      this);
    Bind(wxEVT_LEFT_DOWN,         &SchematicCanvas::OnLeftDown,       this);
    Bind(wxEVT_LEFT_UP,           &SchematicCanvas::OnLeftUp,         this);
    Bind(wxEVT_RIGHT_DOWN,        &SchematicCanvas::OnRightDown,      this);
    Bind(wxEVT_LEFT_DCLICK,       &SchematicCanvas::OnLeftDClick,     this);
    Bind(wxEVT_MIDDLE_DOWN,       &SchematicCanvas::OnMiddleDown,     this);
    Bind(wxEVT_MIDDLE_UP,         &SchematicCanvas::OnMiddleUp,       this);
    Bind(wxEVT_MOUSEWHEEL,        &SchematicCanvas::OnMouseWheel,     this);
    Bind(wxEVT_KEY_DOWN,          &SchematicCanvas::OnKeyDown,        this);
    Bind(wxEVT_LEAVE_WINDOW,      &SchematicCanvas::OnLeaveWindow,    this);
}

// ============================================================
//  坐标变换
// ============================================================

wxPoint2DDouble SchematicCanvas::ToScreen(const wxPoint2DDouble& world) const
{
    return wxPoint2DDouble(m_origin.m_x + world.m_x * m_zoom,
                           m_origin.m_y + world.m_y * m_zoom);
}

wxPoint2DDouble SchematicCanvas::ToWorld(const wxPoint2DDouble& screen) const
{
    return wxPoint2DDouble((screen.m_x - m_origin.m_x) / m_zoom,
                           (screen.m_y - m_origin.m_y) / m_zoom);
}

wxPoint SchematicCanvas::SnapToGrid(const wxPoint& world) const
{
    if (!m_snapEnabled || m_gridStep <= 0)
        return world;

    const int g = m_gridStep;

    return wxPoint(static_cast<int>(std::lround(static_cast<double>(world.x) / g)) * g,
                   static_cast<int>(std::lround(static_cast<double>(world.y) / g)) * g);
}

// ------------------------------------------------------------
// 吸附优先级：引脚外端 > 导线端点 > 网格
// 吸附半径按屏幕像素换算，保证任何缩放下手感一致
// ------------------------------------------------------------
bool SchematicCanvas::FindNearbyAttachPoint(const wxPoint& world, wxPoint& out,
                                            int* kind, wxString* label) const
{
    // 吸附半径按屏幕像素换算，保证任何缩放下手感一致。
    // 同时保证它永远不小于 1.5 个网格 —— 否则放大到 200% 时半径只剩 6 个
    // 世界单位，而网格是 10，鼠标吸附到最近格点后已经跑出半径，就"吸不住"了。
    const double radius = std::max(std::max(5.0, 12.0 / m_zoom), m_gridStep * 1.5);

    double   bestDist = radius;
    bool     found    = false;
    int      bestKind = 0;
    wxString bestLabel;

    // 1) 引脚
    for (const ComponentInstance& comp : m_components)
    {
        for (size_t k = 0; k < comp.pins.size(); ++k)
        {
            const wxPoint p = PinPosition(comp, static_cast<int>(k));
            const double d = std::hypot(static_cast<double>(world.x - p.x),
                                        static_cast<double>(world.y - p.y));

            if (d < bestDist)
            {
                bestDist  = d;
                out       = p;
                found     = true;
                bestKind  = 1;
                bestLabel = wxString::Format(L"引脚 %s-%s", comp.refdes, comp.pins[k].name);
            }
        }
    }

    // 2) 导线端点（引脚优先，所以只在没找到引脚时才比）
    if (!found)
    {
        for (const Wire& w : m_wires)
        {
            for (const wxPoint& p : { w.a, w.b })
            {
                const double d = std::hypot(static_cast<double>(world.x - p.x),
                                            static_cast<double>(world.y - p.y));

                if (d < bestDist)
                {
                    bestDist  = d;
                    out       = p;
                    found     = true;
                    bestKind  = 2;
                    bestLabel = L"导线端点";
                }
            }
        }
    }

    // 3) 导线"身上"（T 型连接）
    //
    // 这里故意用"吸附到网格之后"的那个点去判断，而不是鼠标的原始位置：
    // 网格点和一条横平竖直的导线要么恰好落在线上（距离 0），要么差整整
    // 一格以上。于是只有"格点正好压在线上"才算 T 型连接，画一条跨过别人
    // 导线的线时不会被悄悄连上去（交叉导线本来就不连通）。
    if (!found)
    {
        const wxPoint grid = SnapToGrid(world);

        for (const Wire& w : m_wires)
        {
            const int minX = std::min(w.a.x, w.b.x);
            const int maxX = std::max(w.a.x, w.b.x);
            const int minY = std::min(w.a.y, w.b.y);
            const int maxY = std::max(w.a.y, w.b.y);

            const bool inside = grid.x >= minX && grid.x <= maxX &&
                                grid.y >= minY && grid.y <= maxY;

            if (!inside)
                continue;

            // 点必须真的在线段上（横线看 y、竖线看 x）
            const bool onLine = (w.a.y == w.b.y) ? (grid.y == w.a.y) : (grid.x == w.a.x);

            if (!onLine)
                continue;

            out       = grid;
            found     = true;
            bestKind  = 3;
            bestLabel = L"导线上（T 型连接）";
            break;
        }
    }

    if (found)
    {
        if (kind)
            *kind = bestKind;

        if (label)
            *label = bestLabel;
    }

    return found;
}

wxPoint SchematicCanvas::SnapForWiring(const wxPoint& world) const
{
    wxPoint attach;

    if (FindNearbyAttachPoint(world, attach))
        return attach;

    return SnapToGrid(world);
}

// ============================================================
//  命中测试
// ============================================================

wxRect SchematicCanvas::BoxOf(const ComponentInstance& comp)
{
    return wxRect(comp.pos.x, comp.pos.y, comp.w, comp.h);
}

int SchematicCanvas::HitTestComponent(const wxPoint& world) const
{
    for (int i = static_cast<int>(m_components.size()) - 1; i >= 0; --i)
    {
        const ComponentInstance& comp = m_components[i];

        // 本体 + 引脚线：按引脚长度外扩一圈，点在引脚线上也算点中这个元件
        wxRect box = BoxOf(comp);
        box.Inflate(kPinLen, kPinLen);

        if (box.Contains(world))
            return i;

        // 位号文字（画在本体上方居中，见 SymbolPainter::PaintSymbolLabels）：
        // 点在这几个字上也要能选中，否则"点元件"这件事看起来时灵时不灵。
        if (!comp.refdes.IsEmpty())
        {
            const int    labelW = std::max(16, static_cast<int>(comp.refdes.length()) * 7);
            const wxRect labelBox(comp.pos.x + (comp.w - labelW) / 2,
                                  comp.pos.y - 18,
                                  labelW,
                                  16);

            if (labelBox.Contains(world))
                return i;
        }
    }

    return -1;
}

int SchematicCanvas::HitTestWire(const wxPoint& world) const
{
    const double tolerance = std::max(3.0, 6.0 / m_zoom);

    int    best     = -1;
    double bestDist = tolerance;

    for (size_t i = 0; i < m_wires.size(); ++i)
    {
        const double d = DistanceToSegment(world, m_wires[i].a, m_wires[i].b);

        if (d < bestDist)
        {
            bestDist = d;
            best     = static_cast<int>(i);
        }
    }

    return best;
}

// ------------------------------------------------------------
//  文本注记的外框与命中测试
//  文字宽高按"每字符约 0.62 个字号"粗估 —— 不需要精确，
//  只要够选中用就行（精确排版要 GetTextExtent，但它依赖 GC，不能在 const 里调）。
// ------------------------------------------------------------
wxRect SchematicCanvas::BoxOfText(const TextItem& item) const
{
    const int charCount = static_cast<int>(item.text.length());

    const int width  = std::max(12, static_cast<int>(charCount * item.pointSize * 0.62));
    const int height = std::max(12, static_cast<int>(item.pointSize * 1.6));

    return wxRect(item.pos.x, item.pos.y, width, height);
}

int SchematicCanvas::HitTestText(const wxPoint& world) const
{
    // 从后往前找，后放置的在上面
    for (int i = static_cast<int>(m_texts.size()) - 1; i >= 0; --i)
    {
        wxRect box = BoxOfText(m_texts[i]);
        box.Inflate(4, 4);

        if (box.Contains(world))
            return i;
    }

    return -1;
}

void SchematicCanvas::EnsureOriginInitialised()
{
    if (m_originInit)
        return;

    const wxSize size = GetClientSize();

    if (size.x <= 0 || size.y <= 0)
        return;

    m_origin     = wxPoint2DDouble(size.x / 2.0, size.y / 2.0);
    m_originInit = true;
}

// ============================================================
//  连通性 / 网络
// ============================================================

const NetGraph& SchematicCanvas::GetNetGraph()
{
    if (m_netsDirty)
    {
        m_graph    = NetBuilder::Build(m_components, m_wires);
        m_netsDirty = false;
    }

    return m_graph;
}

int SchematicCanvas::NetOfComponent(int compIndex)
{
    GetNetGraph();

    if (compIndex < 0 || compIndex >= static_cast<int>(m_graph.pinNet.size()))
        return -1;

    for (int netIdx : m_graph.pinNet[compIndex])
    {
        if (netIdx >= 0)
            return netIdx;
    }

    return -1;
}

void SchematicCanvas::UpdateHoverNet(const wxPoint& world)
{
    GetNetGraph();

    int netIdx = -1;

    // 已有选中对象时，优先高亮选中对象所在的网络
    if (!m_selWires.empty())
        netIdx = m_graph.NetOfWire(m_selWires.front());
    else if (!m_selComponents.empty())
        netIdx = NetOfComponent(m_selComponents.front());

    // 否则高亮鼠标悬停位置的网络
    if (netIdx < 0)
    {
        const int wireIndex = HitTestWire(world);

        if (wireIndex >= 0)
        {
            netIdx = m_graph.NetOfWire(wireIndex);
        }
        else
        {
            const int compIndex = HitTestComponent(world);

            if (compIndex >= 0)
                netIdx = NetOfComponent(compIndex);
        }
    }

    if (netIdx != m_highlightNet)
    {
        m_highlightNet = netIdx;
        Refresh();
    }
}

// ------------------------------------------------------------
int SchematicCanvas::GetSelectedNetIndex() const
{
    if (!m_selWires.empty())
        return m_graph.NetOfWire(m_selWires.front());

    if (!m_selComponents.empty())
    {
        // NetOfComponent 是非 const 的（会触发重算），这里用缓存版本
        const int index = m_selComponents.front();

        if (index >= 0 && index < static_cast<int>(m_graph.pinNet.size()))
        {
            for (int netIdx : m_graph.pinNet[index])
            {
                if (netIdx >= 0)
                    return netIdx;
            }
        }
    }

    return -1;
}

// ------------------------------------------------------------
//  只有“恰好选中一个”时才有意义，供属性表显示详情
// ------------------------------------------------------------
const ComponentInstance* SchematicCanvas::GetSelectedComponent() const
{
    if (m_selComponents.size() != 1)
        return nullptr;

    const int index = m_selComponents.front();

    if (index < 0 || index >= static_cast<int>(m_components.size()))
        return nullptr;

    return &m_components[index];
}

const Wire* SchematicCanvas::GetSelectedWire() const
{
    if (m_selWires.size() != 1 || !m_selComponents.empty() || !m_selTexts.empty())
        return nullptr;

    const int index = m_selWires.front();

    if (index < 0 || index >= static_cast<int>(m_wires.size()))
        return nullptr;

    return &m_wires[index];
}

const TextItem* SchematicCanvas::GetSelectedText() const
{
    if (m_selTexts.size() != 1 || !m_selComponents.empty() || !m_selWires.empty())
        return nullptr;

    const int index = m_selTexts.front();

    if (index < 0 || index >= static_cast<int>(m_texts.size()))
        return nullptr;

    return &m_texts[index];
}

int SchematicCanvas::GetSelectedComponentIndex() const
{
    if (m_selComponents.size() != 1)
        return -1;

    const int index = m_selComponents.front();

    if (index < 0 || index >= static_cast<int>(m_components.size()))
        return -1;

    return index;
}

bool SchematicCanvas::IsComponentSelected(int index) const
{
    return std::find(m_selComponents.begin(), m_selComponents.end(), index) != m_selComponents.end();
}

bool SchematicCanvas::IsWireSelected(int index) const
{
    return std::find(m_selWires.begin(), m_selWires.end(), index) != m_selWires.end();
}

bool SchematicCanvas::IsTextSelected(int index) const
{
    return std::find(m_selTexts.begin(), m_selTexts.end(), index) != m_selTexts.end();
}

// ------------------------------------------------------------
//  选中状态维护
// ------------------------------------------------------------
void SchematicCanvas::ClearSelectionInternal()
{
    m_selComponents.clear();
    m_selWires.clear();
    m_selTexts.clear();
}

void SchematicCanvas::SetSelection(int componentIndex, bool add)
{
    if (!add)
        ClearSelectionInternal();

    if (componentIndex < 0)
        return;

    if (!IsComponentSelected(componentIndex))
    {
        m_selComponents.push_back(componentIndex);
        std::sort(m_selComponents.begin(), m_selComponents.end());
    }
}

void SchematicCanvas::SetWireSelection(int wireIndex, bool add)
{
    if (!add)
        ClearSelectionInternal();

    if (wireIndex < 0)
        return;

    if (!IsWireSelected(wireIndex))
    {
        m_selWires.push_back(wireIndex);
        std::sort(m_selWires.begin(), m_selWires.end());
    }
}

void SchematicCanvas::SetTextSelection(int textIndex, bool add)
{
    if (!add)
        ClearSelectionInternal();

    if (textIndex < 0)
        return;

    if (!IsTextSelected(textIndex))
    {
        m_selTexts.push_back(textIndex);
        std::sort(m_selTexts.begin(), m_selTexts.end());
    }
}

void SchematicCanvas::NotifySelectionChanged()
{
    if (OnSelectionChanged)
        OnSelectionChanged();
}

void SchematicCanvas::SelectAll()
{
    ClearSelectionInternal();

    for (size_t i = 0; i < m_components.size(); ++i)
        m_selComponents.push_back(static_cast<int>(i));

    for (size_t i = 0; i < m_wires.size(); ++i)
        m_selWires.push_back(static_cast<int>(i));

    for (size_t i = 0; i < m_texts.size(); ++i)
        m_selTexts.push_back(static_cast<int>(i));

    UpdateHoverNet(m_cursorWorld);
    NotifySelectionChanged();

    if (OnStatusMessage)
        OnStatusMessage(wxString::Format(L"已全选：%d 个元件、%d 条线段、%d 段文本",
                                         GetSelectedComponentCount(),
                                         GetSelectedWireCount(),
                                         GetSelectedTextCount()));

    Refresh();
}

wxString SchematicCanvas::BuildNetlist()
{
    return NetBuilder::ToNetlist(m_components, GetNetGraph());
}

wxString SchematicCanvas::BuildNetlistProtel()
{
    return NetBuilder::ToProtel(m_components, GetNetGraph());
}

// ============================================================
//  逻辑仿真（任务 6）
// ============================================================

void SchematicCanvas::StartSimulation()
{
    if (!m_sim.RuntimeReady()) {
        m_sim.Reset(static_cast<int>(m_components.size()));
        wxString error;
        if (!m_sim.StartRuntime(m_components, GetNetGraph(), error)) {
            if (OnStatusMessage) OnStatusMessage(L"无法运行数字仿真：" + error);
            return;
        }
    }
    m_sim.SetRunning(true);
    m_simTimer.Start(100);
    if (OnStatusMessage) OnStatusMessage(L"数字仿真运行中：每步100ms，点击输入切换，空格暂停并单步");
    Refresh();
}

void SchematicCanvas::PauseSimulation()
{
    m_simTimer.Stop();
    m_sim.SetRunning(false);
    if (OnStatusMessage) OnStatusMessage(L"仿真已暂停；再次运行会从同一时间继续");
    Refresh();
}

void SchematicCanvas::StopSimulation()
{
    m_simTimer.Stop();
    m_sim.SetRunning(false);
    m_sim.Reset(static_cast<int>(m_components.size()));
    Refresh();
}

void SchematicCanvas::OnSimulationTimer(wxTimerEvent&)
{
    if (!m_sim.IsRunning()) return;
    if (!m_sim.AdvanceRuntime()) { StopSimulation(); return; }
    if (OnStatusMessage) OnStatusMessage(wxString::Format(L"数字仿真 t=%llu步", static_cast<unsigned long long>(m_sim.RuntimeTime())));
    Refresh();
}

void SchematicCanvas::StepSimulation()
{
    if (!m_sim.RuntimeReady()) StartSimulation();
    if (!m_sim.RuntimeReady()) return;
    PauseSimulation();
    m_sim.AdvanceRuntime();
    Refresh();
    if (OnStatusMessage) OnStatusMessage(wxString::Format(L"仿真已暂停 t=%llu步", static_cast<unsigned long long>(m_sim.RuntimeTime())));
}

void SchematicCanvas::RecomputeSimulation()
{
    // Runtime topology is immutable; edits invalidate it through PushUndo/Touch.
    Refresh();
}

void SchematicCanvas::TickClock() { StepSimulation(); }

int SchematicCanvas::ToggleInputAt(const wxPoint& world)
{
    if (!m_sim.RuntimeReady())
        return -1;

    const int index = HitTestComponent(world);

    if (index < 0)
        return -1;

    if (DigitalModel(m_components[index]) != "input")
        return -1;

    m_sim.ToggleInputValue(index);
    RecomputeSimulation();

    if (OnStatusMessage)
        OnStatusMessage(wxString::Format(L"%s 的输入值 → %s",
                                         m_components[index].refdes,
                                         LogicText(m_sim.InputValue(index))));

    return index;
}

wxString SchematicCanvas::BuildSimulationReport()
{
    return m_sim.BuildReport(m_components, GetNetGraph());
}

void SchematicCanvas::Touch()
{
    StopSimulation();
    m_modified = true;

    // Document edits invalidate the immutable timed net graph and pending events.
}

wxString SchematicCanvas::BuildErcReport()
{
    return NetBuilder::RunErc(m_components, m_wires, GetNetGraph());
}

// ============================================================
//  绘制
// ============================================================

void SchematicCanvas::OnEraseBackground(wxEraseEvent&)
{
    // 空实现：双缓冲绘制已覆盖整个客户区
}

void SchematicCanvas::OnSize(wxSizeEvent& event)
{
    const wxSize size = GetClientSize();

    if (!m_originInit)
    {
        m_originInit = true;
        m_origin     = wxPoint2DDouble(size.x / 2.0, size.y / 2.0);
    }
    else if (m_lastSize.x > 0 && m_lastSize.y > 0)
    {
        // 窗口尺寸变化时让“可见中心”保持不动，内容不会漂移
        m_origin.m_x += (size.x - m_lastSize.x) / 2.0;
        m_origin.m_y += (size.y - m_lastSize.y) / 2.0;
    }

    m_lastSize = size;

    Refresh();
    event.Skip();
}

void SchematicCanvas::OnPaint(wxPaintEvent&)
{
    EnsureOriginInitialised();
    GetNetGraph();          // 必要时重算连通性

    wxAutoBufferedPaintDC dc(this);

    dc.SetBackground(wxBrush(wxColour(250, 250, 252)));
    dc.Clear();

    wxGraphicsContext* raw = wxGraphicsContext::Create(dc);

    if (!raw)
        return;

    std::unique_ptr<wxGraphicsContext> gc(raw);
    gc->SetAntialiasMode(wxANTIALIAS_DEFAULT);

    DrawBackground(gc.get());
    DrawGrid(gc.get());
    DrawWires(gc.get());
    DrawComponents(gc.get());
    DrawTexts(gc.get());
    DrawSimValues(gc.get());
    DrawSelectionBand(gc.get());
    DrawGhost(gc.get());
    DrawWirePreview(gc.get());
    DrawCrosshair(gc.get());
}

// ------------------------------------------------------------
//  文本注记
// ------------------------------------------------------------
void SchematicCanvas::DrawTexts(wxGraphicsContext* gc)
{
    if (m_texts.empty())
        return;

    for (size_t i = 0; i < m_texts.size(); ++i)
    {
        const TextItem& item = m_texts[i];

        const wxPoint2DDouble pos = ToScreen(wxPoint2DDouble(item.pos.x, item.pos.y));

        // 字号随缩放等比放大，但不小于 6 磅（再小就糊了）
        const int pixelSize = std::max(6, static_cast<int>(std::lround(item.pointSize * m_zoom)));

        const bool selected = IsTextSelected(static_cast<int>(i));

        wxFont font(wxFontInfo(pixelSize).Family(wxFONTFAMILY_SWISS));

        gc->SetFont(font, selected ? wxColour(200, 40, 40) : wxColour(40, 45, 55));
        gc->DrawText(item.text, pos.m_x, pos.m_y);

        // 选中时画一个虚线框
        if (selected)
        {
            const wxRect box = BoxOfText(item);

            const wxPoint2DDouble tl = ToScreen(wxPoint2DDouble(box.x, box.y));
            const wxPoint2DDouble br = ToScreen(wxPoint2DDouble(box.GetRight(), box.GetBottom()));

            gc->SetBrush(*wxTRANSPARENT_BRUSH);
            gc->SetPen(wxPen(wxColour(200, 40, 40), 1, wxPENSTYLE_SHORT_DASH));
            gc->DrawRectangle(tl.m_x - 2, tl.m_y - 2,
                              (br.m_x - tl.m_x) + 4, (br.m_y - tl.m_y) + 4);
        }
    }
}

// ------------------------------------------------------------
//  仿真：在「输入引脚 / 常量 / 时钟 / 输出引脚 / 探针」下方标出逻辑值
// ------------------------------------------------------------
void SchematicCanvas::DrawSimValues(wxGraphicsContext* gc)
{
    if (!m_sim.RuntimeReady())
        return;

    const int fontSize = std::max(7, static_cast<int>(std::lround(9.0 * m_zoom)));

    for (size_t i = 0; i < m_components.size(); ++i)
    {
        const ComponentInstance& comp = m_components[i];
        const SimKind kind = SimKindOf(comp.name);

        if (kind == SimKind::None)
            continue;

        LogicValue value = LogicValue::X;
        bool       show  = false;

        if (kind == SimKind::Source)
        {
            value = m_sim.InputValue(static_cast<int>(i));
            show  = true;
        }
        else if (kind == SimKind::Clock)
        {
            if (i < m_graph.pinNet.size() && !m_graph.pinNet[i].empty())
                value = m_sim.NetValue(m_graph.pinNet[i][0]);
            show  = true;
        }
        else if (kind == SimKind::Sink)
        {
            // 取它唯一一个输入脚所在的网络值
            for (size_t k = 0; k < comp.pins.size(); ++k)
            {
                if (comp.pins[k].dir != PinDir::Input)
                    continue;

                const int net = (i < m_graph.pinNet.size() && k < m_graph.pinNet[i].size())
                                    ? m_graph.pinNet[i][k]
                                    : -1;

                value = m_sim.NetValue(net);
                show  = true;
                break;
            }
        }

        if (!show)
            continue;

        const wxPoint2DDouble tl = ToScreen(wxPoint2DDouble(comp.pos.x, comp.pos.y));
        const double boxW = comp.w * m_zoom;
        const double boxH = comp.h * m_zoom;

        const wxString label = LogicText(value);
        const wxColour colour = NetColourFor(value);

        wxFont font(wxFontInfo(fontSize).Family(wxFONTFAMILY_SWISS));
        font.SetWeight(wxFONTWEIGHT_BOLD);
        gc->SetFont(font, colour);

        wxDouble tw = 0, th = 0, td = 0, tel = 0;
        gc->GetTextExtent(label, &tw, &th, &td, &tel);

        const double bx = tl.m_x + boxW / 2.0 - (tw + 8) / 2.0;
        const double by = tl.m_y + boxH + 2;

        gc->SetBrush(wxBrush(wxColour(255, 255, 255, 235)));
        gc->SetPen(wxPen(colour, 1));
        gc->DrawRectangle(bx, by, tw + 8, th + 3);

        gc->DrawText(label, bx + 4, by + 1.5);
    }
}

// ------------------------------------------------------------
//  框选橡皮筋
// ------------------------------------------------------------
void SchematicCanvas::DrawSelectionBand(wxGraphicsContext* gc)
{
    if (!m_bandSelecting)
        return;

    const wxPoint2DDouble a = ToScreen(wxPoint2DDouble(m_bandStart.x, m_bandStart.y));
    const wxPoint2DDouble b = ToScreen(wxPoint2DDouble(m_bandEnd.x, m_bandEnd.y));

    const double x = std::min(a.m_x, b.m_x);
    const double y = std::min(a.m_y, b.m_y);
    const double w = std::fabs(b.m_x - a.m_x);
    const double h = std::fabs(b.m_y - a.m_y);

    gc->SetBrush(wxBrush(wxColour(40, 110, 220, 40)));
    gc->SetPen(wxPen(wxColour(40, 110, 220), 1, wxPENSTYLE_SHORT_DASH));
    gc->DrawRectangle(x, y, w, h);
}

// ------------------------------------------------------------
void SchematicCanvas::DrawBackground(wxGraphicsContext* gc)
{
    const wxSize size = GetClientSize();

    // 世界坐标原点
    const wxPoint2DDouble origin = ToScreen(wxPoint2DDouble(0, 0));

    gc->SetPen(wxPen(wxColour(200, 205, 215), 1));
    gc->StrokeLine(origin.m_x - 6, origin.m_y, origin.m_x + 6, origin.m_y);
    gc->StrokeLine(origin.m_x, origin.m_y - 6, origin.m_x, origin.m_y + 6);

    if (!m_components.empty() || !m_wires.empty())
        return;

    gc->SetFont(wxFont(wxFontInfo(9).Family(wxFONTFAMILY_SWISS)), wxColour(150, 155, 165));

    const wxString line1 = L"从左侧「元件库」选元件，在画布上单击放置；按 W 切到导线工具画线";
    const wxString line2 = L"（滚轮缩放 · 中键拖动平移 · Delete 删除 · Esc 结束画线 · 右键放弃待放置）";

    wxDouble lw = 0, lh = 0, ld = 0, ll = 0;
    gc->GetTextExtent(line1, &lw, &lh, &ld, &ll);

    const double cx = size.x / 2.0;
    const double cy = size.y / 2.0;

    gc->DrawText(line1, cx - lw / 2.0, cy - lh * 1.2);
    gc->DrawText(line2, cx - lw / 2.0, cy + lh * 0.2);
}

// ------------------------------------------------------------
void SchematicCanvas::DrawGrid(wxGraphicsContext* gc)
{
    if (!m_gridVisible)
        return;

    const wxSize size = GetClientSize();

    const double minorPx = m_gridStep * m_zoom;
    const double majorPx = minorPx * 5.0;

    if (majorPx < 5.0)
        return;

    const wxPoint2DDouble topLeft     = ToWorld(wxPoint2DDouble(0, 0));
    const wxPoint2DDouble bottomRight = ToWorld(wxPoint2DDouble(size.x, size.y));

    const int maxLines = 2000;

    // ---- 主网格 ----
    {
        const double step = m_gridStep * 5.0;

        gc->SetPen(wxPen(wxColour(222, 227, 236), 1));

        const double x0 = std::floor(topLeft.m_x / step) * step;
        const double y0 = std::floor(topLeft.m_y / step) * step;

        int lines = 0;

        for (double x = x0; x <= bottomRight.m_x && lines < maxLines; x += step, ++lines)
        {
            const double sx = ToScreen(wxPoint2DDouble(x, 0)).m_x;
            gc->StrokeLine(sx, 0, sx, size.y);
        }

        lines = 0;

        for (double y = y0; y <= bottomRight.m_y && lines < maxLines; y += step, ++lines)
        {
            const double sy = ToScreen(wxPoint2DDouble(0, y)).m_y;
            gc->StrokeLine(0, sy, size.x, sy);
        }
    }

    // ---- 次网格 ----
    if (minorPx < 6.0)
        return;

    gc->SetPen(wxPen(wxColour(238, 241, 247), 1));

    const double step = m_gridStep;

    const double x0 = std::floor(topLeft.m_x / step) * step;
    const double y0 = std::floor(topLeft.m_y / step) * step;

    int lines = 0;

    for (double x = x0; x <= bottomRight.m_x && lines < maxLines; x += step, ++lines)
    {
        const double sx = ToScreen(wxPoint2DDouble(x, 0)).m_x;
        gc->StrokeLine(sx, 0, sx, size.y);
    }

    lines = 0;

    for (double y = y0; y <= bottomRight.m_y && lines < maxLines; y += step, ++lines)
    {
        const double sy = ToScreen(wxPoint2DDouble(0, y)).m_y;
        gc->StrokeLine(0, sy, size.x, sy);
    }
}

// ------------------------------------------------------------
//  导线 / 连接点 / 悬空端点
// ------------------------------------------------------------

// 返回经过某个连接点的导线下标（优先取端点重合的），找不到返回 -1。
// 只用于仿真时给连接点选颜色，因此不追求严格的重合判定。
static int WireAtJunction(const std::vector<Wire>& wires, const wxPoint& p)
{
    for (size_t i = 0; i < wires.size(); ++i)
    {
        const wxPoint& a = wires[i].a;
        const wxPoint& b = wires[i].b;

        if (a == p || b == p)
            return static_cast<int>(i);
    }

    for (size_t i = 0; i < wires.size(); ++i)
    {
        const wxPoint& a = wires[i].a;
        const wxPoint& b = wires[i].b;

        if (a.x == b.x && p.x == a.x &&
            p.y >= std::min(a.y, b.y) && p.y <= std::max(a.y, b.y))
            return static_cast<int>(i);

        if (a.y == b.y && p.y == a.y &&
            p.x >= std::min(a.x, b.x) && p.x <= std::max(a.x, b.x))
            return static_cast<int>(i);
    }

    return -1;
}

void SchematicCanvas::DrawWires(wxGraphicsContext* gc)
{
    if (m_wires.empty())
        return;

    // ---- 导线本体 ----
    for (size_t i = 0; i < m_wires.size(); ++i)
    {
        const Wire& w = m_wires[i];

        const wxPoint2DDouble a = ToScreen(wxPoint2DDouble(w.a.x, w.a.y));
        const wxPoint2DDouble b = ToScreen(wxPoint2DDouble(w.b.x, w.b.y));

        const bool selected = IsWireSelected(static_cast<int>(i));
        const bool inNet    = (m_highlightNet >= 0 && m_graph.NetOfWire(static_cast<int>(i)) == m_highlightNet);

        wxColour colour = kWireColour;
        double   width  = std::max(1.0, 2.0 * m_zoom);

        const bool simulating = m_sim.RuntimeReady();

        // 仿真中：导线按所在网络的逻辑值着色。
        // 此时逻辑值是最重要的信息，优先级高于"网络高亮"，
        // 否则高亮色会把仿真结果盖掉；高亮只保留加粗的视觉效果。
        if (simulating)
            colour = NetColourFor(m_sim.NetValue(m_graph.NetOfWire(static_cast<int>(i))));

        if (inNet)
        {
            if (!simulating)
                colour = kWireHotColour;

            width = std::max(2.0, 3.0 * m_zoom);
        }

        if (selected)
        {
            colour = kWireSelColour;
            width  = std::max(2.0, 3.0 * m_zoom);
        }

        gc->SetPen(wxPen(colour, width));
        gc->StrokeLine(a.m_x, a.m_y, b.m_x, b.m_y);
    }

    // ---- 连接点（实心圆）----
    const double jr = std::max(2.0, 3.5 * m_zoom);

    gc->SetPen(*wxTRANSPARENT_PEN);
    gc->SetBrush(wxBrush(kWireColour));

    for (const wxPoint& j : m_graph.junctions)
    {
        const wxPoint2DDouble s = ToScreen(wxPoint2DDouble(j.x, j.y));

        // 仿真中连接点跟着所在网络一起变色，避免绿线上挂一堆灰点
        if (m_sim.RuntimeReady())
        {
            const int wireIndex = WireAtJunction(m_wires, j);

            if (wireIndex >= 0)
                gc->SetBrush(wxBrush(NetColourFor(
                    m_sim.NetValue(m_graph.NetOfWire(wireIndex)))));
        }

        gc->DrawEllipse(s.m_x - jr, s.m_y - jr, jr * 2, jr * 2);
    }

    // ---- 悬空端点（空心小方块，ERC 的视觉提示）----
    const double dr = std::max(2.0, 3.0 * m_zoom);

    gc->SetBrush(*wxTRANSPARENT_BRUSH);
    gc->SetPen(wxPen(kDanglingColour, std::max(1.0, 1.4 * m_zoom)));

    for (size_t i = 0; i < m_wires.size(); ++i)
    {
        const Wire& w = m_wires[i];

        for (const wxPoint& p : { w.a, w.b })
        {
            if (IsEndpointConnected(m_components, m_wires, p, static_cast<int>(i)))
                continue;

            const wxPoint2DDouble s = ToScreen(wxPoint2DDouble(p.x, p.y));
            gc->DrawRectangle(s.m_x - dr, s.m_y - dr, dr * 2, dr * 2);
        }
    }
}

// ------------------------------------------------------------
//  元件
// ------------------------------------------------------------
void SchematicCanvas::DrawComponents(wxGraphicsContext* gc)
{
    wxFont font(wxFontInfo(8).Family(wxFONTFAMILY_SWISS));
    wxFont smallFont(wxFontInfo(7).Family(wxFONTFAMILY_SWISS));
    wxFont nameFont(wxFontInfo(6).Family(wxFONTFAMILY_SWISS));

    const bool showText    = m_zoom >= 0.45;
    const bool showPinName = m_zoom >= 0.75;

    for (size_t i = 0; i < m_components.size(); ++i)
    {
        const ComponentInstance& comp = m_components[i];

        const wxPoint2DDouble tl = ToScreen(wxPoint2DDouble(comp.pos.x, comp.pos.y));
        const double w = comp.w * m_zoom;
        const double h = comp.h * m_zoom;

        const bool selected = IsComponentSelected(static_cast<int>(i));
        const bool inNet    = (m_highlightNet >= 0 && NetOfComponent(static_cast<int>(i)) == m_highlightNet);

        // 形状与元件库预览共用同一个绘制器，保证“预览 = 放下去的样子”
        const SymbolPlacement place { tl, m_zoom };

        // ---- 引脚（逐引脚着色：仿真值 / 网络高亮）----
        for (size_t k = 0; k < comp.pins.size(); ++k)
        {
            if (comp.pins[k].noConnect) {
                const auto pt=ToScreen(wxPoint2DDouble(PinPosition(comp,static_cast<int>(k))));
                gc->SetPen(wxPen(wxColour(60,90,150),1));
                gc->StrokeLine(pt.m_x-3,pt.m_y-3,pt.m_x+3,pt.m_y+3);
                gc->StrokeLine(pt.m_x-3,pt.m_y+3,pt.m_x+3,pt.m_y-3);
            }
            if (comp.pins[k].hidden) continue;
            const wxPoint anchorW = PinAnchor(comp, static_cast<int>(k));
            const wxPoint outerW  = PinPosition(comp, static_cast<int>(k));

            const wxPoint2DDouble anchor = ToScreen(wxPoint2DDouble(anchorW.x, anchorW.y));
            const wxPoint2DDouble outer  = ToScreen(wxPoint2DDouble(outerW.x, outerW.y));

            const int netIdx = (i < m_graph.pinNet.size() && k < m_graph.pinNet[i].size())
                                   ? m_graph.pinNet[i][k]
                                   : -1;

            const bool pinHot = (m_highlightNet >= 0 && netIdx == m_highlightNet);

            // 引脚颜色：仿真中跟随所在网络的逻辑值（和导线保持一致），
            // 否则用普通色 / 网络高亮色。
            wxColour pinColour(90, 100, 120);
            double   pinWidth = 1.5;

            if (m_sim.RuntimeReady() && netIdx >= 0)
            {
                pinColour = NetColourFor(m_sim.NetValue(netIdx));
                pinWidth  = 2.0;
            }
            else if (pinHot)
            {
                pinColour = kWireHotColour;
                pinWidth  = 2.4;
            }

            gc->SetPen(wxPen(pinColour, std::max(1.0, pinWidth * m_zoom)));
            gc->StrokeLine(anchor.m_x, anchor.m_y, outer.m_x, outer.m_y);

            if (pinHot)
            {
                gc->SetPen(*wxTRANSPARENT_PEN);
                gc->SetBrush(wxBrush(pinColour));
                gc->DrawEllipse(outer.m_x - 3.0, outer.m_y - 3.0, 6.0, 6.0);
            }
        }

        // ---- 本体：Logisim 风格符号 ----
        wxColour fill = wxColour(255, 252, 235);

        if (inNet)
            fill = wxColour(255, 238, 210);

        if (selected)
            fill = wxColour(255, 244, 200);

        const wxColour outline = selected ? wxColour(205, 115, 0) : wxColour(52, 62, 84);

        PaintSymbolBody(gc, comp, place, outline, fill, selected ? 2.1 : 1.4);

        // ---- 选中虚框 ----
        if (selected)
        {
            gc->SetPen(wxPen(wxColour(40, 110, 220), 1, wxPENSTYLE_SHORT_DASH));
            gc->SetBrush(*wxTRANSPARENT_BRUSH);
            gc->DrawRectangle(tl.m_x - kPinLen * m_zoom - 2, tl.m_y - 2,
                              w + 2 * kPinLen * m_zoom + 4, h + 4);
        }

        // ---- 位号 / 元件名 / 引脚名（引脚名放在引脚线外侧）----
        PaintSymbolLabels(gc, comp, place,
                          smallFont, smallFont, nameFont,
                          wxColour(110, 120, 135),   // 引脚名
                          wxColour(40, 90, 170),     // 位号
                          wxColour(96, 102, 114),    // 元件名
                          showPinName, showText);
    }
}

// ------------------------------------------------------------
//  待放置元件的“幽灵”预览
// ------------------------------------------------------------
void SchematicCanvas::DrawGhost(wxGraphicsContext* gc)
{
    if (!m_pending || !m_hasCursor || m_tool != ToolSelect)
        return;

    const int w = m_pending->w;
    const int h = m_pending->h;

    const wxPoint center  = SnapToGrid(m_cursorWorld);
    const wxPoint topLeft = SnapToGrid(wxPoint(center.x - w / 2, center.y - h / 2));

    const wxPoint2DDouble tl = ToScreen(wxPoint2DDouble(topLeft.x, topLeft.y));

    const double sw = w * m_zoom;
    const double sh = h * m_zoom;

    // 幽灵也用真实形状画（虚线 + 半透明），这样"放下去会是什么样"一眼可见
    ComponentInstance ghost;

    ghost.name    = m_pending->name;
    ghost.refdes  = m_pending->refPrefix.IsEmpty() ? wxString(L"U?")
                                                  : m_pending->refPrefix + L"?";
    ghost.pos     = topLeft;
    ghost.w       = w;
    ghost.h       = h;
    ghost.pins    = MakePins(m_pending->name, m_pending->kind,
                             m_pending->inputs, m_pending->outputs);

    const SymbolPlacement ghostPlace { tl, m_zoom };

    PaintSymbolBody(gc, ghost, ghostPlace,
                    wxColour(40, 110, 220),
                    wxColour(210, 235, 255, 120), 1.2, true);

    // 幽灵上只写位号占位符（U?），不写元件类型名——形状已经说明是什么了
    gc->SetFont(wxFont(wxFontInfo(7).Family(wxFONTFAMILY_SWISS)), wxColour(40, 110, 220));

    wxDouble tw = 0, th = 0, td = 0, tel = 0;
    gc->GetTextExtent(ghost.refdes, &tw, &th, &td, &tel);
    gc->DrawText(ghost.refdes, tl.m_x + sw / 2.0 - tw / 2.0, tl.m_y - th - 1);
}

// ------------------------------------------------------------
//  导线绘制预览（正交 L 形）+ 网络名提示
// ------------------------------------------------------------
void SchematicCanvas::DrawWirePreview(wxGraphicsContext* gc)
{
    if (m_wireDrawing)
    {
        const wxPoint2DDouble a = ToScreen(wxPoint2DDouble(m_wireAnchor.x, m_wireAnchor.y));
        const wxPoint2DDouble c = ToScreen(wxPoint2DDouble(m_wireCursor.x, m_wireCursor.y));

        gc->SetPen(wxPen(kWireColour, std::max(2.0, 2.4 * m_zoom), wxPENSTYLE_SHORT_DASH));
        gc->StrokeLine(a.m_x, a.m_y, c.m_x, a.m_y);   // 先横
        gc->StrokeLine(c.m_x, a.m_y, c.m_x, c.m_y);   // 后竖

        gc->SetPen(*wxTRANSPARENT_PEN);
        gc->SetBrush(wxBrush(kWireColour));
        gc->DrawEllipse(a.m_x - 3.0, a.m_y - 3.0, 6.0, 6.0);
    }

    // 最后画吸附标记，保证它在所有图形之上
    DrawSnapMarker(gc);

    // 高亮网络时在光标旁显示网络名
    if (m_highlightNet >= 0 && m_highlightNet < static_cast<int>(m_graph.nets.size()) && m_hasCursor)
    {
        const Net& net = m_graph.nets[m_highlightNet];

        const wxString label = wxString::Format(L"%s (%d 引脚)", net.name, static_cast<int>(net.pins.size()));

        gc->SetFont(wxFont(wxFontInfo(8).Family(wxFONTFAMILY_SWISS)), kWireHotColour);

        wxDouble tw = 0, th = 0, td = 0, tel = 0;
        gc->GetTextExtent(label, &tw, &th, &td, &tel);

        const double x = m_cursorScreenX + 14.0;
        const double y = m_cursorScreenY + 14.0;

        gc->SetBrush(wxBrush(wxColour(255, 255, 255, 220)));
        gc->SetPen(wxPen(kWireHotColour, 1));
        gc->DrawRectangle(x - 4, y - 2, tw + 8, th + 4);

        gc->DrawText(label, x, y);
    }
}

// ------------------------------------------------------------
//  吸附标记：导线工具靠近引脚 / 导线端点时画一个醒目的准星 + 名字
//  目的只有一个：让用户清楚地看到“这一下会接到哪里”，
//  而不是靠猜自己有没有对准。
// ------------------------------------------------------------
void SchematicCanvas::DrawSnapMarker(wxGraphicsContext* gc)
{
    if (!m_snapActive || m_tool != ToolWire)
        return;

    const wxPoint2DDouble p = ToScreen(wxPoint2DDouble(m_snapPoint.x, m_snapPoint.y));

    // 引脚亮绿、导线端点橙、导线身上（T 型）蓝 —— 颜色本身就在告诉用户接的是什么
    const wxColour accent = (m_snapKind == 1) ? wxColour(0, 190, 90)
                            : (m_snapKind == 3) ? wxColour(20, 110, 220)
                                                : wxColour(230, 130, 20);

    // 1) 外面一圈虚线光环（半径随缩放稍微变化，但保持像素量级）
    const double r = std::max(11.0, 11.0 * m_zoom);

    gc->SetPen(wxPen(accent, std::max(1.0, 1.5 * m_zoom), wxPENSTYLE_DOT));
    gc->SetBrush(*wxTRANSPARENT_BRUSH);
    gc->DrawEllipse(p.m_x - r, p.m_y - r, r * 2, r * 2);

    // 2) 十字准星（四段短线，不压住中心）
    gc->SetPen(wxPen(accent, std::max(1.0, 1.4 * m_zoom)));
    gc->StrokeLine(p.m_x - r - 5, p.m_y, p.m_x - r + 1, p.m_y);
    gc->StrokeLine(p.m_x + r - 1, p.m_y, p.m_x + r + 5, p.m_y);
    gc->StrokeLine(p.m_x, p.m_y - r - 5, p.m_x, p.m_y - r + 1);
    gc->StrokeLine(p.m_x, p.m_y + r - 1, p.m_x, p.m_y + r + 5);

    // 3) 中心实心点 —— "咬住了"
    gc->SetPen(*wxTRANSPARENT_PEN);
    gc->SetBrush(wxBrush(accent));
    gc->DrawEllipse(p.m_x - 3.5, p.m_y - 3.5, 7.0, 7.0);

    // 4) 名字：引脚 xx-yy / 导线端点
    if (!m_snapLabel.IsEmpty())
    {
        const wxFont labelFont(wxFontInfo(9).Family(wxFONTFAMILY_SWISS).Bold());
        gc->SetFont(labelFont, accent);

        wxDouble tw = 0, th = 0, td = 0, tel = 0;
        gc->GetTextExtent(m_snapLabel, &tw, &th, &td, &tel);

        // 摆在光环右上方，别压住元件本体
        double x = p.m_x + r + 6;
        double y = p.m_y - r - th - 2;
        if (y < 2)
            y = p.m_y + r + 4;

        gc->SetBrush(wxBrush(wxColour(255, 255, 255, 240)));
        gc->SetPen(wxPen(accent, std::max(1.0, 1.5 * m_zoom)));
        gc->DrawRectangle(x - 4, y - 2, tw + 8, th + 4);

        gc->SetFont(labelFont, accent);
        gc->DrawText(m_snapLabel, x, y);
    }
}

// ------------------------------------------------------------
void SchematicCanvas::DrawCrosshair(wxGraphicsContext* gc)
{
    if (!m_crosshair || !m_hasCursor)
        return;

    const wxSize size = GetClientSize();

    gc->SetPen(wxPen(wxColour(120, 170, 230), 1, wxPENSTYLE_DOT));
    gc->StrokeLine(0, m_cursorScreenY, size.x, m_cursorScreenY);
    gc->StrokeLine(m_cursorScreenX, 0, m_cursorScreenX, size.y);
}

// ============================================================
//  鼠标 / 键盘
// ============================================================

void SchematicCanvas::OnMouseMove(wxMouseEvent& event)
{
    if (m_panning)
    {
        const wxPoint delta = event.GetPosition() - m_panStartScreen;

        m_origin.m_x = m_panStartOrigin.m_x + delta.x;
        m_origin.m_y = m_panStartOrigin.m_y + delta.y;

        Refresh();
    }

    const wxPoint2DDouble world = ToWorld(wxPoint2DDouble(event.GetX(), event.GetY()));

    const wxPoint raw(static_cast<int>(std::lround(world.m_x)),
                      static_cast<int>(std::lround(world.m_y)));

    m_cursorScreenX = event.GetX();
    m_cursorScreenY = event.GetY();
    m_hasCursor     = true;

    if (m_dragging)
    {
        UpdateDrag(raw);
    }
    else if (m_bandSelecting)
    {
        UpdateBandSelect(raw);
    }
    else if (m_wireDrawing)
    {
        m_wireCursor  = SnapForWiring(raw);
        m_cursorWorld = m_wireCursor;
        Refresh();
    }
    else
    {
        m_cursorWorld = SnapToGrid(raw);

        if (m_tool == ToolSelect)
            UpdateHoverNet(raw);
    }

    // 导线工具下始终算一遍“现在有没有吸附上”，
    // 让用户在还没落笔的时候就能看到准星咬住了哪个引脚
    m_snapActive = false;
    m_snapKind   = 0;

    if (m_tool == ToolWire)
    {
        wxPoint  attach;
        int      kind = 0;
        wxString label;

        if (FindNearbyAttachPoint(raw, attach, &kind, &label))
        {
            m_snapActive = true;
            m_snapKind   = kind;
            m_snapPoint  = attach;
            m_snapLabel  = label;
        }
    }

    if (OnCursorMoved)
        OnCursorMoved(m_cursorWorld);

    if (m_pending && m_tool == ToolSelect)
        Refresh();

    // 导线工具：吸附光环要跟着鼠标动，必须重画
    if (m_tool == ToolWire)
        Refresh();
}

void SchematicCanvas::OnLeftDown(wxMouseEvent& event)
{
    SetFocus();

    const wxPoint2DDouble world = ToWorld(wxPoint2DDouble(event.GetX(), event.GetY()));
    const wxPoint point(static_cast<int>(std::lround(world.m_x)),
                        static_cast<int>(std::lround(world.m_y)));

    const bool add = event.ControlDown() || event.ShiftDown();

    // ---- 导线工具 ----
    if (m_tool == ToolWire)
    {
        HandleWireClick(point);
        return;
    }

    // ---- 文本工具：弹输入框，放一段文字 ----
    if (m_tool == ToolText)
    {
        const wxString text = wxGetTextFromUser(L"输入要放置的文字：", L"放置文本",
                                                wxEmptyString, this);

        if (!text.IsEmpty())
            PlaceText(text, SnapToGrid(point));

        return;
    }

    // ---- 仿真中：点到「输入引脚 / 常量」就切换它的逻辑值 ----
    if (m_sim.RuntimeReady() && ToggleInputAt(point) >= 0)
    {
        NotifySelectionChanged();
        return;
    }

    // ---- 点到已有元件：选中并开始拖动 ----
    const int compIndex = HitTestComponent(point);

    if (compIndex >= 0)
    {
        if (add)
        {
            // Ctrl/Shift 点击：切换该元件的选中状态
            if (IsComponentSelected(compIndex))
            {
                m_selComponents.erase(
                    std::remove(m_selComponents.begin(), m_selComponents.end(), compIndex),
                    m_selComponents.end());
            }
            else
            {
                SetSelection(compIndex, true);
            }
        }
        else if (!IsComponentSelected(compIndex))
        {
            SetSelection(compIndex, false);
        }

        UpdateHoverNet(point);
        NotifySelectionChanged();
        Refresh();

        // 按住已选中的元件 → 准备拖动（Shift/Ctrl 时只做加减选，不拖动）
        if (!add && IsComponentSelected(compIndex))
            BeginDrag(point);

        return;
    }

    // ---- 点到已有导线 ----
    const int wireIndex = HitTestWire(point);

    if (wireIndex >= 0)
    {
        if (add && IsWireSelected(wireIndex))
        {
            m_selWires.erase(std::remove(m_selWires.begin(), m_selWires.end(), wireIndex),
                             m_selWires.end());
        }
        else
        {
            SetWireSelection(wireIndex, add);
        }

        UpdateHoverNet(point);
        NotifySelectionChanged();
        Refresh();
        return;
    }

    // ---- 点到已有文本注记 ----
    const int textIndex = HitTestText(point);

    if (textIndex >= 0)
    {
        if (add && IsTextSelected(textIndex))
        {
            m_selTexts.erase(std::remove(m_selTexts.begin(), m_selTexts.end(), textIndex),
                             m_selTexts.end());
        }
        else
        {
            SetTextSelection(textIndex, add);
        }

        UpdateHoverNet(point);
        NotifySelectionChanged();
        Refresh();
        return;
    }

    // ---- 放置待放置元件 ----
    if (m_pending)
    {
        PlaceComponent(point);
        return;
    }

    // ---- 空白处：开始框选 ----
    BeginBandSelect(point);
}

// ------------------------------------------------------------
//  左键抬起：结束拖动 / 框选
// ------------------------------------------------------------
void SchematicCanvas::OnLeftUp(wxMouseEvent&)
{
    if (m_dragging)
    {
        EndDrag();
        return;
    }

    if (m_bandSelecting)
        EndBandSelect();
}

void SchematicCanvas::OnRightDown(wxMouseEvent& event)
{
    // 右键结束当前导线
    if (m_wireDrawing)
    {
        FinishWire();
        return;
    }

    // 右键放弃“待放置”的元件。
    // Esc 的取消功能已经去掉了，这里是唯一的“选错了不放了”的出口。
    if (m_pending)
    {
        m_pending = nullptr;

        if (OnPlaceFinished)
            OnPlaceFinished();

        if (OnStatusMessage)
            OnStatusMessage(L"已取消放置");

        Refresh();
        return;
    }

    // ------------------------------------------------------------
    //  右键上下文菜单（任务 4 补充）
    //
    //  为什么要有：删除/复制/旋转这些操作原来只藏在菜单栏里，
    //  用户找不到——"元件删不掉"的反馈里有一半是找不到入口。
    //  这里把最常用的几条放在鼠标底下。
    // ------------------------------------------------------------
    const wxPoint2DDouble worldD = ToWorld(wxPoint2DDouble(event.GetX(), event.GetY()));
    const wxPoint        world(static_cast<int>(std::lround(worldD.m_x)),
                               static_cast<int>(std::lround(worldD.m_y)));

    // 右键点在某个对象上、而它当前没被选中 → 先把选择切到它上面，
    // 这样"右键 → 删除"直接作用于鼠标下面那个东西，不用先左键点一次。
    const int hitComp = HitTestComponent(world);
    const int hitWire = (hitComp < 0) ? HitTestWire(world) : -1;
    const int hitText = (hitComp < 0 && hitWire < 0) ? HitTestText(world) : -1;

    bool selectionChanged = false;

    if (hitComp >= 0 && !IsComponentSelected(hitComp))
    {
        ClearSelectionInternal();
        m_selComponents.push_back(hitComp);
        selectionChanged = true;
    }
    else if (hitWire >= 0 && !IsWireSelected(hitWire))
    {
        ClearSelectionInternal();
        m_selWires.push_back(hitWire);
        selectionChanged = true;
    }
    else if (hitText >= 0 && !IsTextSelected(hitText))
    {
        ClearSelectionInternal();
        m_selTexts.push_back(hitText);
        selectionChanged = true;
    }

    if (selectionChanged)
    {
        NotifySelectionChanged();
        Refresh();
    }

    const bool hasSel = HasSelection();

    wxMenu menu;
    wxMenuItem* del = menu.Append(ID_EDIT_DELETE, L"删除\tDelete");
    del->Enable(hasSel);

    wxMenuItem* cut = menu.Append(wxID_CUT, L"剪切\tCtrl+X");
    cut->Enable(hasSel);

    wxMenuItem* copy = menu.Append(wxID_COPY, L"复制\tCtrl+C");
    copy->Enable(hasSel);

    wxMenuItem* paste = menu.Append(wxID_PASTE, L"粘贴\tCtrl+V");
    paste->Enable(HasClipboard());

    menu.AppendSeparator();

    wxMenuItem* rotCw = menu.Append(ID_EDIT_ROTATE_CW, L"旋转 90°（顺时针）\tCtrl+R");
    rotCw->Enable(hasSel);

    wxMenuItem* rotCcw = menu.Append(ID_EDIT_ROTATE_CCW, L"旋转 90°（逆时针）\tCtrl+Shift+R");
    rotCcw->Enable(hasSel);

    wxMenuItem* flipH = menu.Append(ID_EDIT_FLIP_H, L"水平翻转（左右）\tCtrl+H");
    flipH->Enable(hasSel);

    wxMenuItem* flipV = menu.Append(ID_EDIT_FLIP_V, L"垂直翻转（上下）\tCtrl+Shift+H");
    flipV->Enable(hasSel);

    menu.AppendSeparator();

    menu.Append(ID_EDIT_SELECT_ALL, L"全选\tCtrl+A");

    wxMenuItem* deselect = menu.Append(ID_EDIT_DESELECT_ALL, L"取消选择\tCtrl+Shift+A");
    deselect->Enable(hasSel);

    PopupMenu(&menu);
}

void SchematicCanvas::OnLeftDClick(wxMouseEvent&)
{
    if (m_wireDrawing)
        FinishWire();
}

void SchematicCanvas::OnMiddleDown(wxMouseEvent& event)
{
    m_panning        = true;
    m_panStartScreen = event.GetPosition();
    m_panStartOrigin = m_origin;

    SetCursor(wxCursor(wxCURSOR_SIZING));
}

void SchematicCanvas::OnMiddleUp(wxMouseEvent&)
{
    m_panning = false;
    SetCursor(wxCursor(wxCURSOR_CROSS));
}

void SchematicCanvas::OnMouseWheel(wxMouseEvent& event)
{
    const wxPoint2DDouble anchor(event.GetX(), event.GetY());

    const double factor = (event.GetWheelRotation() > 0) ? 1.15 : (1.0 / 1.15);

    SetZoomAt(m_zoom * factor, anchor);
}

void SchematicCanvas::OnLeaveWindow(wxMouseEvent& event)
{
    m_hasCursor = false;

    if (m_highlightNet != -1)
        m_highlightNet = -1;

    Refresh();
    event.Skip();
}

void SchematicCanvas::OnKeyDown(wxKeyEvent& event)
{
    if (!event.HasAnyModifiers() && event.GetKeyCode() == 'W') {
        wxCommandEvent command(wxEVT_MENU, ID_PLACE_WIRE);
        wxPostEvent(wxGetTopLevelParent(this), command);
        return;
    }

    switch (event.GetKeyCode())
    {
        case WXK_DELETE:
            DeleteSelection();
            return;

        case WXK_ESCAPE:
            // Esc 只保留“结束画线”这一件事。
            // 原来那条“Esc 取消选择 / 取消待放置”已经删掉：
            // 元件放完会自动回到选择状态，不需要再按 Esc 收尾。
            if (m_wireDrawing)
            {
                FinishWire();
                return;
            }
            break;

        case WXK_RETURN:
        case WXK_NUMPAD_ENTER:
            if (m_wireDrawing)
            {
                FinishWire();
                return;
            }
            break;

        case WXK_ADD:
        case WXK_NUMPAD_ADD:
            ZoomIn();
            return;

        case WXK_SUBTRACT:
        case WXK_NUMPAD_SUBTRACT:
            ZoomOut();
            return;

        case WXK_HOME:
            ZoomFit();
            return;

        case WXK_SPACE:
            // 仿真中：空格打一拍时钟
            if (m_sim.RuntimeReady())
            {
                TickClock();
                return;
            }
            break;

        case WXK_LEFT:
        case WXK_RIGHT:
        case WXK_UP:
        case WXK_DOWN:
        {
            const int step = event.ShiftDown() ? 80 : 20;

            if (event.GetKeyCode() == WXK_LEFT)  m_origin.m_x += step;
            if (event.GetKeyCode() == WXK_RIGHT) m_origin.m_x -= step;
            if (event.GetKeyCode() == WXK_UP)    m_origin.m_y += step;
            if (event.GetKeyCode() == WXK_DOWN)  m_origin.m_y -= step;

            Refresh();
            return;
        }

        default:
            break;
    }

    event.Skip();
}

// ============================================================
//  撤销 / 重做（快照式）
// ============================================================

void SchematicCanvas::PushUndo(const wxString& label)
{
    StopSimulation();
    Snapshot snapshot;
    snapshot.components    = m_components;
    snapshot.wires         = m_wires;
    snapshot.texts         = m_texts;
    snapshot.nextComponentId = m_nextComponentId;
    snapshot.nextWireId      = m_nextWireId;
    snapshot.refCount        = m_refCount;
    snapshot.label           = label;

    m_undoStack.push_back(std::move(snapshot));

    // 只保留最近 100 步
    if (m_undoStack.size() > 100)
        m_undoStack.erase(m_undoStack.begin());

    // 新的操作让“重做”失效
    m_redoStack.clear();
}

void SchematicCanvas::ApplySnapshot(const Snapshot& snapshot)
{
    StopSimulation();
    m_components    = snapshot.components;
    m_wires         = snapshot.wires;
    m_texts         = snapshot.texts;
    m_nextComponentId = snapshot.nextComponentId;
    m_nextWireId      = snapshot.nextWireId;
    m_refCount        = snapshot.refCount;

    ClearSelectionInternal();
    m_wireDrawing  = false;
    m_dragging     = false;
    m_bandSelecting = false;
    m_highlightNet = -1;
    m_netsDirty    = true;
    m_modified     = true;

    GetNetGraph();
    UpdateHoverNet(m_cursorWorld);
    NotifySelectionChanged();

    if (OnStatusMessage)
        OnStatusMessage(wxString::Format(L"已恢复：%s（元件 %d、导线 %d）",
                                         snapshot.label,
                                         GetComponentCount(),
                                         GetWireCount()));

    Refresh();
}

bool SchematicCanvas::Undo()
{
    if (m_undoStack.empty())
        return false;

    // 先把“当前状态”压进重做栈
    Snapshot current;
    current.components    = m_components;
    current.wires         = m_wires;
    current.texts         = m_texts;
    current.nextComponentId = m_nextComponentId;
    current.nextWireId      = m_nextWireId;
    current.refCount        = m_refCount;
    current.label           = GetUndoLabel();
    m_redoStack.push_back(std::move(current));

    const Snapshot snapshot = m_undoStack.back();
    m_undoStack.pop_back();

    ApplySnapshot(snapshot);
    return true;
}

bool SchematicCanvas::Redo()
{
    if (m_redoStack.empty())
        return false;

    Snapshot current;
    current.components    = m_components;
    current.wires         = m_wires;
    current.texts         = m_texts;
    current.nextComponentId = m_nextComponentId;
    current.nextWireId      = m_nextWireId;
    current.refCount        = m_refCount;
    current.label           = GetRedoLabel();
    m_undoStack.push_back(std::move(current));

    const Snapshot snapshot = m_redoStack.back();
    m_redoStack.pop_back();

    ApplySnapshot(snapshot);
    return true;
}

wxString SchematicCanvas::GetUndoLabel() const
{
    if (m_undoStack.empty())
        return wxEmptyString;

    return m_undoStack.back().label;
}

wxString SchematicCanvas::GetRedoLabel() const
{
    if (m_redoStack.empty())
        return wxEmptyString;

    return m_redoStack.back().label;
}

// ============================================================
//  编辑操作
// ============================================================

// ------------------------------------------------------------
//  放置一段文本注记
// ------------------------------------------------------------
void SchematicCanvas::PlaceText(const wxString& text, const wxPoint& worldPos)
{
    if (text.IsEmpty())
        return;

    TextItem item;
    item.text      = text;
    item.pos       = SnapToGrid(worldPos);
    item.pointSize = 12;

    PushUndo(wxString::Format(L"放置文本「%s」",
                              text.length() > 12 ? text.Left(12) + L"…" : text));

    m_texts.push_back(item);

    SetTextSelection(static_cast<int>(m_texts.size()) - 1, false);
    Touch();

    if (OnStatusMessage)
        OnStatusMessage(wxString::Format(L"已放置文本，共 %d 段", GetTextCount()));

    NotifySelectionChanged();
    Refresh();
}

void SchematicCanvas::PlaceComponent(const wxPoint& world)
{
    if (!m_pending)
        return;

    ComponentInstance comp;
    comp.id   = ++m_nextComponentId;
    comp.name = m_pending->name;
    comp.w    = m_pending->w;
    comp.h    = m_pending->h;
    comp.pins = MakePins(m_pending->name, m_pending->kind,
                         m_pending->inputs, m_pending->outputs);

    // PCB 网表需要的两项，从元件定义带过来
    comp.footprint = m_pending->footprint;
    comp.value     = m_pending->value;
    comp.logicModel = m_pending->logicModel;
    comp.truthTable = m_pending->truthTable;
    comp.delayTicks = m_pending->delayTicks;
    comp.clockPeriodTicks = m_pending->clockPeriodTicks;
    if (DigitalModel(comp) == "input" && comp.value != "0" && comp.value != "1" && comp.value != "X") comp.value = "0";


    // 以光标为中心；中心与左上角都对齐到网格，
    // 这样引脚（PinAnchor 内部也对齐到 10）一定落在网格上，画线不会产生小折角
    const wxPoint center  = SnapToGrid(world);
    const wxPoint topLeft = SnapToGrid(wxPoint(center.x - comp.w / 2, center.y - comp.h / 2));

    comp.pos = topLeft;

    int& counter = m_refCount[comp.name];
    ++counter;

    if (m_pending->refPrefix.StartsWith(L"#"))
        comp.refdes = comp.name;                       // 电源符号不编号
    else
        comp.refdes = NextReference(m_components, m_pending->refPrefix);

    PushUndo(wxString::Format(L"放置 %s", comp.refdes));

    m_components.push_back(comp);

    SetSelection(static_cast<int>(m_components.size()) - 1, false);
    m_netsDirty = true;
    Touch();

    // ---- 放完一个就退出“待放置”状态 ----
    //  以前要按 Esc 才能取消，现在放完立刻回到普通的选择状态：
    //  幽灵不再跟着光标，也不会误放第二个。
    m_pending = nullptr;

    if (OnPlaceFinished)
        OnPlaceFinished();   // 主窗口会顺手清掉元件库里高亮的那一项

    if (OnStatusMessage)
        OnStatusMessage(wxString::Format(L"已放置 %s（位号 %s，%d 个引脚）— 已回到选择状态",
                                         comp.name, comp.refdes,
                                         static_cast<int>(comp.pins.size())));

    UpdateHoverNet(comp.pos);
    NotifySelectionChanged();

    Refresh();
}

// ------------------------------------------------------------
//  拖动移动
// ------------------------------------------------------------
void SchematicCanvas::BeginDrag(const wxPoint& world)
{
    if (m_selComponents.empty()) return;
    StopSimulation();
    m_dragging = true;
    m_dragMoved = false;
    m_dragStartWorld = world;
    m_dragBaseComponents = m_components;
    m_dragBaseWires = m_wires;
    SetCursor(wxCursor(wxCURSOR_SIZING));
}

void SchematicCanvas::UpdateDrag(const wxPoint& world)
{
    if (!m_dragging) return;
    const wxPoint delta = SnapToGrid(world - m_dragStartWorld);
    if (delta == wxPoint(0, 0) && !m_dragMoved) return;
    if (!m_dragMoved) { PushUndo(L"移动元件及连接线"); m_dragMoved = true; }
    m_components = m_dragBaseComponents;
    m_wires = m_dragBaseWires;
    if (!MoveConnectedComponents(m_components, m_wires, m_selComponents, delta) && OnStatusMessage)
        OnStatusMessage(L"移动已取消：当前位置会改变网络连接，请换一个位置");
    for (const auto& wire : m_wires) m_nextWireId = std::max(m_nextWireId, wire.id);
    m_selWires.clear();
    m_netsDirty = true;
    m_cursorWorld = world;
    GetNetGraph();
    if (OnCursorMoved) OnCursorMoved(world);
    Refresh();
}

void SchematicCanvas::EndDrag()
{
    if (!m_dragging) return;
    m_dragging = false;
    SetCursor(wxCursor(wxCURSOR_CROSS));
    if (m_dragMoved) { Touch(); NotifySelectionChanged(); }
    m_dragMoved = false;
    m_dragBaseComponents.clear();
    m_dragBaseWires.clear();
    Refresh();
}

// ------------------------------------------------------------
//  框选（橡皮筋）
// ------------------------------------------------------------
void SchematicCanvas::BeginBandSelect(const wxPoint& world)
{
    m_bandSelecting = true;
    m_bandAdditive  = false;      // 由 OnLeftDown 决定是否累加，这里默认替换
    m_bandStart     = world;
    m_bandEnd       = world;

    if (!m_selComponents.empty() || !m_selWires.empty() || !m_selTexts.empty())
    {
        ClearSelectionInternal();
        UpdateHoverNet(world);
        NotifySelectionChanged();
    }

    Refresh();
}

void SchematicCanvas::UpdateBandSelect(const wxPoint& world)
{
    if (!m_bandSelecting)
        return;

    m_bandEnd = world;
    Refresh();
}

void SchematicCanvas::EndBandSelect()
{
    if (!m_bandSelecting)
        return;

    m_bandSelecting = false;

    const wxRect band(std::min(m_bandStart.x, m_bandEnd.x),
                      std::min(m_bandStart.y, m_bandEnd.y),
                      std::abs(m_bandEnd.x - m_bandStart.x),
                      std::abs(m_bandEnd.y - m_bandStart.y));

    // 太小就当成“空白处单击”→ 取消选中
    if (band.width < 3 && band.height < 3)
    {
        ClearSelectionInternal();
        UpdateHoverNet(m_bandEnd);
        NotifySelectionChanged();
        Refresh();
        return;
    }

    ClearSelectionInternal();

    for (size_t i = 0; i < m_components.size(); ++i)
    {
        if (band.Intersects(BoxOf(m_components[i])))
            m_selComponents.push_back(static_cast<int>(i));
    }

    for (size_t i = 0; i < m_wires.size(); ++i)
    {
        const wxRect wireBox(std::min(m_wires[i].a.x, m_wires[i].b.x),
                             std::min(m_wires[i].a.y, m_wires[i].b.y),
                             std::abs(m_wires[i].b.x - m_wires[i].a.x) + 1,
                             std::abs(m_wires[i].b.y - m_wires[i].a.y) + 1);

        if (band.Intersects(wireBox))
            m_selWires.push_back(static_cast<int>(i));
    }

    for (size_t i = 0; i < m_texts.size(); ++i)
    {
        if (band.Intersects(BoxOfText(m_texts[i])))
            m_selTexts.push_back(static_cast<int>(i));
    }

    UpdateHoverNet(m_bandEnd);

    if (OnStatusMessage)
        OnStatusMessage(wxString::Format(L"框选到 %d 个元件、%d 条线段、%d 段文本",
                                         GetSelectedComponentCount(),
                                         GetSelectedWireCount(),
                                         GetSelectedTextCount()));

    NotifySelectionChanged();
    Refresh();
}

// ------------------------------------------------------------
void SchematicCanvas::AddWire(const wxPoint& a, const wxPoint& b)
{
    if (a == b)
        return;

    Wire wire;
    wire.id = ++m_nextWireId;
    wire.a  = a;
    wire.b  = b;

    m_wires.push_back(wire);
    m_netsDirty = true;
    Touch();
}

void SchematicCanvas::HandleWireClick(const wxPoint& world)
{
    // 吸附：引脚 > 导线端点 > 导线身上（T 型）> 网格。
    // 一旦吸到了连接点，就把落点直接改成那个点 —— 这样导线端头一定
    // 正好落在引脚/端点上，不会留下 1 格的短桩，也就不会"看着接上了、
    // 其实网络是断的"。
    wxPoint  attachPoint;
    int      attachKind = 0;
    const bool onAttach = FindNearbyAttachPoint(world, attachPoint, &attachKind);

    const wxPoint p = onAttach ? attachPoint : SnapForWiring(world);

    if (!m_wireDrawing)
    {
        m_wireDrawing = true;
        m_wireAnchor  = p;
        m_wireCursor  = p;

        if (OnStatusMessage)
            OnStatusMessage(onAttach
                                ? wxString::Format(
                                      L"导线起点已接到%s — 再点一下放折点，右键 / Esc 结束",
                                      attachKind == 1 ? L"引脚"
                                      : attachKind == 2 ? L"导线端点"
                                                        : L"导线上")
                                : wxString::Format(
                                      L"导线起点 (%d, %d) — 再次单击放折点，右键 / Esc / 回车结束",
                                      p.x, p.y));

        Refresh();
        return;
    }

    if (p == m_wireAnchor)
    {
        FinishWire();
        return;
    }

    // 这一下点在了连接点上？是的话画完就自动收尾并切回“选择”——不用再点右键。
    const bool landed = onAttach;

    // 一次单击可能产生 1~2 条线段，压一次快照（撤销时整步回退）
    PushUndo(wxString::Format(L"绘制导线到 (%d, %d)", p.x, p.y));

    // 正交折线：先水平后垂直（和预览一致）
    if (p.x != m_wireAnchor.x)
    {
        AddWire(m_wireAnchor, wxPoint(p.x, m_wireAnchor.y));
        m_wireAnchor = wxPoint(p.x, m_wireAnchor.y);
    }

    if (p.y != m_wireAnchor.y)
    {
        AddWire(m_wireAnchor, wxPoint(m_wireAnchor.x, p.y));
        m_wireAnchor = wxPoint(m_wireAnchor.x, p.y);
    }

    m_wireCursor = m_wireAnchor;

    if (OnStatusMessage)
        OnStatusMessage(wxString::Format(L"导线延伸到 (%d, %d)", m_wireAnchor.x, m_wireAnchor.y));

    GetNetGraph();
    UpdateHoverNet(m_wireAnchor);

    if (OnSelectionChanged)
        OnSelectionChanged();

    Refresh();

    // 接到引脚 / 导线端点 / 导线身上 → 这条线就算画完了
    if (landed)
    {
        if (OnStatusMessage)
            OnStatusMessage(attachKind == 1 ? L"导线已接到引脚，自动回到选择工具"
                            : attachKind == 2
                                ? L"导线已接到导线端点，自动回到选择工具"
                                : L"导线已接到导线上（T 型连接），自动回到选择工具");

        FinishWire(true);
    }
}

void SchematicCanvas::FinishWire(bool returnToSelect)
{
    if (!m_wireDrawing)
        return;

    m_wireDrawing = false;
    m_netsDirty   = true;
    m_snapActive  = false;

    GetNetGraph();

    if (OnStatusMessage)
        OnStatusMessage(wxString::Format(L"导线绘制结束，当前共 %d 条线段，%d 个网络",
                                         static_cast<int>(m_wires.size()),
                                         static_cast<int>(m_graph.nets.size())));

    if (OnSelectionChanged)
        OnSelectionChanged();

    Refresh();

    // 画完一段线就回到“选择”，省掉一次右键 / 再点一次工具的力气。
    // 从 SetTool 里调过来时传 false，否则会来回切工具。
    if (returnToSelect && OnWireFinished)
        OnWireFinished();
}

void SchematicCanvas::DeleteSelection(const wxString& label)
{
    if (!HasSelection())
    {
        // 以前这里直接 return，界面上一点反应都没有，看着像"删除坏了"。
        // 现在给一句明确提示，用户就知道是"没选中"而不是"功能失效"。
        if (OnStatusMessage)
            OnStatusMessage(L"没有选中对象 — 先用鼠标点一下要删的元件 / 导线 / 文本，或按住左键拖出选框");

        return;
    }

    const int componentCount = GetSelectedComponentCount();
    const int wireCount      = GetSelectedWireCount();
    const int textCount      = GetSelectedTextCount();

    PushUndo(label.IsEmpty()
                 ? wxString::Format(L"删除 %d 元件 / %d 线段 / %d 文本",
                                    componentCount, wireCount, textCount)
                 : label);

    // 从后往前删，避免下标失效
    for (int k = static_cast<int>(m_selComponents.size()) - 1; k >= 0; --k)
        m_components.erase(m_components.begin() + m_selComponents[k]);

    for (int k = static_cast<int>(m_selWires.size()) - 1; k >= 0; --k)
        m_wires.erase(m_wires.begin() + m_selWires[k]);

    for (int k = static_cast<int>(m_selTexts.size()) - 1; k >= 0; --k)
        m_texts.erase(m_texts.begin() + m_selTexts[k]);

    ClearSelectionInternal();
    m_netsDirty    = true;
    m_highlightNet = -1;
    Touch();

    GetNetGraph();

    if (OnStatusMessage)
        OnStatusMessage(wxString::Format(L"已删除 %d 个元件、%d 条线段、%d 段文本",
                                         componentCount, wireCount, textCount));

    NotifySelectionChanged();

    Refresh();
}

// ============================================================
//  剪贴板：复制 / 剪切 / 粘贴（任务 4）
// ============================================================

// 从原件位号推一个新位号：U1 复制出来 → U2、U3…
wxString SchematicCanvas::NextRefdes(const wxString& protoRefdes, const wxString& compName)
{
    // 前缀 = 末尾数字之前的那些字符：U12 → "U", R3 → "R"
    int split = static_cast<int>(protoRefdes.length());

    while (split > 0 && protoRefdes[split - 1] >= L'0' && protoRefdes[split - 1] <= L'9')
        --split;

    const wxString prefix = protoRefdes.Left(split);

    // 电源符号这类"位号就是名字"的元件（VCC / GND）不参与计数，原样保留；
    // 以 '#' 开头的位号也不自动编号（KiCad 里 #PWR 之类）
    if (prefix.IsEmpty())
        return protoRefdes;

    return NextReference(m_components, prefix);
}

void SchematicCanvas::CopySelection()
{
    if (!HasSelection())
    {
        if (OnStatusMessage)
            OnStatusMessage(L"没有选中任何对象 — 先框选或用 Ctrl+单击选中要复制的元件");
        return;
    }

    m_clip.components.clear();
    m_clip.wires.clear();
    m_clip.texts.clear();

    // 包围盒：粘贴时拿它的左上角当锚点，贴出来的相对位置和复制时一致
    bool has  = false;
    int  minX = 0, minY = 0, maxX = 0, maxY = 0;

    auto grow = [&](int x, int y)
    {
        if (!has)
        {
            minX = maxX = x;
            minY = maxY = y;
            has  = true;
            return;
        }

        minX = std::min(minX, x);
        maxX = std::max(maxX, x);
        minY = std::min(minY, y);
        maxY = std::max(maxY, y);
    };

    for (int index : m_selComponents)
    {
        if (index < 0 || index >= static_cast<int>(m_components.size()))
            continue;

        const ComponentInstance& comp = m_components[index];

        m_clip.components.push_back(comp);

        grow(comp.pos.x, comp.pos.y);
        grow(comp.pos.x + comp.w, comp.pos.y + comp.h);
    }

    for (int index : m_selWires)
    {
        if (index < 0 || index >= static_cast<int>(m_wires.size()))
            continue;

        const Wire& wire = m_wires[index];

        m_clip.wires.push_back(wire);

        grow(wire.a.x, wire.a.y);
        grow(wire.b.x, wire.b.y);
    }

    for (int index : m_selTexts)
    {
        if (index < 0 || index >= static_cast<int>(m_texts.size()))
            continue;

        const TextItem& item = m_texts[index];

        m_clip.texts.push_back(item);

        grow(item.pos.x, item.pos.y);
    }

    m_clip.anchor = has ? wxPoint(minX, minY) : wxPoint(0, 0);

    if (OnStatusMessage)
        OnStatusMessage(wxString::Format(L"已复制 %d 个元件、%d 条线段、%d 段文本（Ctrl+V 粘贴）",
                                         static_cast<int>(m_clip.components.size()),
                                         static_cast<int>(m_clip.wires.size()),
                                         static_cast<int>(m_clip.texts.size())));
}

void SchematicCanvas::CutSelection()
{
    if (!HasSelection())
    {
        if (OnStatusMessage)
            OnStatusMessage(L"没有选中任何对象 — 剪切前先选中");
        return;
    }

    const int componentCount = GetSelectedComponentCount();
    const int wireCount      = GetSelectedWireCount();
    const int textCount      = GetSelectedTextCount();

    CopySelection();
    DeleteSelection(L"剪切");

    if (OnStatusMessage)
        OnStatusMessage(wxString::Format(L"已剪切 %d 个元件、%d 条线段、%d 段文本（Ctrl+V 粘贴回来）",
                                         componentCount, wireCount, textCount));
}

void SchematicCanvas::PasteClipboard()
{
    if (!CanPaste())
    {
        if (OnStatusMessage)
            OnStatusMessage(L"剪贴板是空的 — 先选中对象再按 Ctrl+C 复制");
        return;
    }

    // 粘贴位置：让剪贴板包围盒的左上角落在光标处（网格对齐），
    // 这样"贴着鼠标放"，和大多数画图软件一致。光标不在画布上时往右下挪两格。
    const wxPoint target = m_hasCursor
                               ? SnapToGrid(m_cursorWorld)
                               : m_clip.anchor + wxPoint(m_gridStep * 2, m_gridStep * 2);

    const wxPoint delta(target.x - m_clip.anchor.x, target.y - m_clip.anchor.y);

    PushUndo(L"粘贴");

    ClearSelectionInternal();

    for (const ComponentInstance& proto : m_clip.components)
    {
        ComponentInstance comp = proto;

        comp.id     = ++m_nextComponentId;
        comp.pos    = wxPoint(comp.pos.x + delta.x, comp.pos.y + delta.y);
        comp.refdes = NextRefdes(proto.refdes, proto.name);

        m_components.push_back(comp);
        m_selComponents.push_back(static_cast<int>(m_components.size()) - 1);
    }

    for (const Wire& proto : m_clip.wires)
    {
        Wire wire = proto;

        wire.id = ++m_nextWireId;
        wire.a  = wxPoint(wire.a.x + delta.x, wire.a.y + delta.y);
        wire.b  = wxPoint(wire.b.x + delta.x, wire.b.y + delta.y);

        m_wires.push_back(wire);
        m_selWires.push_back(static_cast<int>(m_wires.size()) - 1);
    }

    for (const TextItem& proto : m_clip.texts)
    {
        TextItem item = proto;

        item.pos = wxPoint(item.pos.x + delta.x, item.pos.y + delta.y);

        m_texts.push_back(item);
        m_selTexts.push_back(static_cast<int>(m_texts.size()) - 1);
    }

    m_netsDirty    = true;
    m_highlightNet = -1;
    Touch();

    GetNetGraph();

    if (OnStatusMessage)
        OnStatusMessage(wxString::Format(L"已粘贴 %d 个元件、%d 条线段、%d 段文本（位号已重新编号）",
                                         static_cast<int>(m_clip.components.size()),
                                         static_cast<int>(m_clip.wires.size()),
                                         static_cast<int>(m_clip.texts.size())));

    NotifySelectionChanged();

    Refresh();
}

// ============================================================
//  旋转 / 翻转（任务 4）
// ============================================================
void SchematicCanvas::RotateSelection(int quartersCW)
{
    const int step = (quartersCW >= 0) ? 1 : 3;   // 逆时针 90° = 顺时针 270°

    TransformSelection([step](ComponentInstance& comp) { RotateComponent(comp, step); },
                       (quartersCW >= 0) ? wxString(L"顺时针旋转 90°") : wxString(L"逆时针旋转 90°"));
}

void SchematicCanvas::FlipSelection(bool horizontal)
{
    TransformSelection([horizontal](ComponentInstance& comp) { MirrorComponent(comp, horizontal); },
                       horizontal ? wxString(L"水平翻转") : wxString(L"垂直翻转"));
}

// ============================================================
//  属性表编辑（任务 4）
//
//  属性面板只认识“键 + 字符串”，真正的校验和落盘都在这里：
//    · 位号要非空、不重号；
//    · 坐标吸附到网格，并让贴在引脚上的导线端点跟着走（能保持正交才跟）；
//    · 封装非空的元件才允许改封装（电源 / 网络标签不写网表）。
//  每次真正改动前压一份快照，撤销栈里就是“修改位号”这种可读的名字。
// ============================================================
bool SchematicCanvas::ApplyPropertyEdit(const wxString& key, const wxString& text,
                                        wxString* error)
{
    auto Reject = [error](const wxString& message) -> bool
    {
        if (error)
            *error = message;

        return false;
    };

    // 去掉首尾空白（用户可能在输入框里多敲了空格）
    wxString value = text;
    value.Trim(true).Trim(false);

    // ---------------- 元件 ----------------
    if (key.StartsWith(L"comp."))
    {
        const int index = GetSelectedComponentIndex();

        if (index < 0 || index >= static_cast<int>(m_components.size()))
            return Reject(L"没有选中元件");

        ComponentInstance& comp = m_components[index];
        const wxString     field = key.Mid(5);

        if (field == L"refdes")
        {
            if (value.IsEmpty())
                return Reject(L"位号不能为空");

            if (value.Find(L' ') != wxNOT_FOUND)
                return Reject(L"位号里不能有空格");

            if (value == comp.refdes)
                return true;

            for (size_t i = 0; i < m_components.size(); ++i)
            {
                if (static_cast<int>(i) != index && m_components[i].refdes == value)
                    return Reject(wxString::Format(L"位号 %s 已经被别的元件用了", value));
            }

            PushUndo(L"修改位号");
            comp.refdes = value;
            for (auto& prop : comp.properties) if (prop.first == "Reference") prop.second = value;

            // 让自动编号计数器跟上，免得以后新放一个元件又和这里撞号。
            // 例：把 U1 改成 U7，之后新放的应该是 U8 而不是 U2。
            {
                size_t digit = value.Len();

                while (digit > 0 && wxIsdigit(value[digit - 1]))
                    --digit;

                if (digit > 0 && digit < value.Len())
                {
                    long number = 0;
                    value.Mid(digit).ToLong(&number);

                    int& counter = m_refCount[comp.name];

                    if (number > counter)
                        counter = static_cast<int>(number);
                }
            }

            m_netsDirty = true;      // 网表里引脚名带位号，得重算
            Touch();
            GetNetGraph();
            Refresh();

            if (OnStatusMessage)
                OnStatusMessage(wxString::Format(L"位号已改为 %s", comp.refdes));

            return true;
        }

        if (field == L"x" || field == L"y")
        {
            long number = 0;

            if (!value.ToLong(&number))
                return Reject(L"坐标必须是整数");

            wxPoint target = comp.pos;

            if (field == L"x")
                target.x = static_cast<int>(number);
            else
                target.y = static_cast<int>(number);

            target = SnapToGrid(target);

            if (target == comp.pos)
                return true;

            const wxPoint delta = target - comp.pos;
            auto components = m_components; auto wires = m_wires;
            if (!MoveConnectedComponents(components, wires, { index }, delta))
                return Reject(L"移动会改变网络连接，已取消；请换一个位置");
            PushUndo(L"移动元件");
            m_components = std::move(components); m_wires = std::move(wires);
            for (const auto& wire : m_wires) m_nextWireId = std::max(m_nextWireId, wire.id);
            m_selWires.clear();
            m_netsDirty = true;
            Touch();
            GetNetGraph();
            Refresh();
            if (OnStatusMessage) OnStatusMessage(L"元件坐标与连接线已一起更新");

            return true;
        }

        if (field == L"value")
        {
            if (value == comp.value)
                return true;

            if (!comp.netLabel.empty() && (value.empty() || value.Contains("${") || value.Contains("..")))
                return Reject(L"网络名称不能为空或含未解析的变量/总线表达式");
            PushUndo(L"修改元件值");
            if (!comp.netLabel.empty()) comp.netLabel = value;
            comp.value = value;
            for (auto& prop : comp.properties) if (prop.first == "Value") prop.second = value;
            m_netsDirty = true; // Label values determine electrical net identity.
            Touch();
            GetNetGraph();
            Refresh();

            if (OnStatusMessage)
                OnStatusMessage(wxString::Format(L"%s 的元件值改为「%s」",
                                                 comp.refdes,
                                                 comp.value.IsEmpty() ? comp.name : comp.value));

            return true;
        }

        if (field.StartsWith("param.")) {
            long index=-1;
            if (!field.Mid(6).ToLong(&index) || index<0 || index>=static_cast<long>(comp.properties.size())) return Reject(L"元件参数不存在");
            auto& prop=comp.properties[static_cast<size_t>(index)];
            if (prop.first=="Reference" || prop.first=="Value" || prop.first=="Footprint") return Reject(L"请通过对应属性行修改");
            if (prop.second==value) return true;
            PushUndo(L"修改导入元件参数"); prop.second=value; Touch(); Refresh(); return true;
        }
        if (field == "logicModel" || field == "truthTable" || field == "delayTicks" || field == "clockPeriodTicks") {
            ComponentInstance candidate = comp;
            if (field == "logicModel") candidate.logicModel = value;
            else if (field == "truthTable") candidate.truthTable = value;
            else {
                long n = 0; if (!value.ToLong(&n) || n < 1 || n > 10000) return Reject(L"时间参数须为正整数");
                if (field == "delayTicks") candidate.delayTicks = static_cast<int>(n);
                else candidate.clockPeriodTicks = static_cast<int>(n);
            }
            auto validation = candidate; validation.id = 1;
            if (DigitalModel(validation) == "input" && validation.value != "0" && validation.value != "1" && validation.value != "X") validation.value = "0";
            // Allow entering a truth table before switching the model to truth.
            NetGraph graph; graph.nets.resize(validation.pins.size()); graph.pinNet.emplace_back();
            for (size_t k = 0; k < validation.pins.size(); ++k) graph.pinNet[0].push_back(static_cast<int>(k));
            DigitalSimulator simulator; wxString reason;
            if (!simulator.Reset({validation}, graph, reason)) return Reject(reason);
            PushUndo(L"修改数字仿真参数"); comp = candidate; Touch(); Refresh(); return true;
        }
        if (field == L"footprint")
        {
            // 电源 / 地 / 网络标签没有封装概念，改了也不会进 PCB 网表
            if (!IsPcbComponent(comp))
                return Reject(L"该元件是非物理元件（电源 / 网络标签），不写入 PCB 网表，封装不可编辑");

            if (value == comp.footprint)
                return true;

            PushUndo(L"修改封装");
            comp.footprint = value;
            for (auto& prop : comp.properties) if (prop.first == "Footprint") prop.second = value;
            Touch();
            Refresh();

            if (OnStatusMessage)
                OnStatusMessage(wxString::Format(L"%s 的封装改为「%s」", comp.refdes, comp.footprint));

            return true;
        }

        return Reject(wxString::Format(L"不认识的元件属性：%s", field));
    }

    // ---------------- 文本注记 ----------------
    if (key.StartsWith(L"text."))
    {
        if (m_selTexts.size() != 1)
            return Reject(L"没有选中文本");

        const int index = m_selTexts.front();

        if (index < 0 || index >= static_cast<int>(m_texts.size()))
            return Reject(L"没有选中文本");

        TextItem&      item  = m_texts[index];
        const wxString field = key.Mid(5);

        if (field == L"content")
        {
            if (value.IsEmpty())
                return Reject(L"文本内容不能为空");

            if (value == item.text)
                return true;

            PushUndo(L"修改文本");
            item.text = value;
        }
        else if (field == L"x" || field == L"y")
        {
            long number = 0;

            if (!value.ToLong(&number))
                return Reject(L"坐标必须是整数");

            wxPoint target = item.pos;

            if (field == L"x")
                target.x = static_cast<int>(number);
            else
                target.y = static_cast<int>(number);

            target = SnapToGrid(target);

            if (target == item.pos)
                return true;

            PushUndo(L"移动文本");
            item.pos = target;
        }
        else if (field == L"size")
        {
            long number = 0;

            if (!value.ToLong(&number))
                return Reject(L"字号必须是整数");

            int pointSize = static_cast<int>(number);

            if (pointSize < 6)
                pointSize = 6;

            if (pointSize > 72)
                pointSize = 72;

            if (pointSize == item.pointSize)
                return true;

            PushUndo(L"修改字号");
            item.pointSize = pointSize;
        }
        else
        {
            return Reject(wxString::Format(L"不认识的文本属性：%s", field));
        }

        Touch();
        Refresh();

        if (OnStatusMessage)
            OnStatusMessage(L"文本注记已更新");

        return true;
    }

    return Reject(wxString::Format(L"不认识的属性键：%s", key));
}

void SchematicCanvas::TransformSelection(const std::function<void(ComponentInstance&)>& apply,
                                         const wxString& label)
{
    if (m_selComponents.empty())
    {
        if (OnStatusMessage)
            OnStatusMessage(L"先选中要变换的元件（旋转 / 翻转只作用于元件，导线不动）");
        return;
    }

    auto after = m_components;
    for (int ci : m_selComponents)
        if (ci >= 0 && ci < static_cast<int>(after.size())) apply(after[ci]);
    auto wires = m_wires;
    if (!ReconnectChangedComponents(m_components, after, wires)) {
        if (OnStatusMessage) OnStatusMessage(L"变换已取消：当前位置会导致网络短接或断开，请先移到空白区域");
        return;
    }
    PushUndo(label);
    m_components = std::move(after);
    m_wires = std::move(wires);
    for (const auto& wire : m_wires) m_nextWireId = std::max(m_nextWireId, wire.id);
    m_selWires.clear();
    m_netsDirty = true; m_highlightNet = -1; Touch(); GetNetGraph();
    if (OnStatusMessage) OnStatusMessage(label + L"：元件与连接线已一起更新");

    NotifySelectionChanged();

    Refresh();
}

void SchematicCanvas::DeselectAll()
{
    if (!HasSelection())
        return;

    ClearSelectionInternal();
    m_highlightNet = -1;

    NotifySelectionChanged();

    Refresh();
}

void SchematicCanvas::ClearAll(bool markModified)
{
    // 清空也算一次可撤销的操作（新建文档时由 MainFrame 传 false 并另行清栈）
    if (markModified && (!m_components.empty() || !m_wires.empty() || !m_texts.empty()))
        PushUndo(L"清空画布");

    StopSimulation();
    m_components.clear();
    m_wires.clear();
    m_texts.clear();
    m_refCount.clear();
    m_nextComponentId = 0;
    m_nextWireId      = 0;
    m_pending         = nullptr;
    m_wireDrawing     = false;
    m_dragging        = false;
    m_bandSelecting   = false;
    m_highlightNet    = -1;
    m_netsDirty       = true;
    m_zoom            = 1.0;
    m_originInit      = false;
    m_modified        = markModified;

    m_sim.SetRunning(false);
    m_sim.Reset(0);

    ClearSelectionInternal();

    EnsureOriginInitialised();
    GetNetGraph();

    NotifySelectionChanged();

    NotifyViewChanged();
}

// ------------------------------------------------------------
//  新建文档：清内容 + 清撤销栈
// ------------------------------------------------------------
void SchematicCanvas::ResetDocument()
{
    m_undoStack.clear();
    m_redoStack.clear();
    ClearAll(false);
}

// ============================================================
//  文档装载（任务 5）
// ============================================================

void SchematicCanvas::SetDocument(const std::vector<ComponentInstance>& components,
                                  const std::vector<Wire>&              wires,
                                  const std::vector<TextItem>&          texts,
                                  int    gridStep,
                                  bool   gridVisible,
                                  bool   snapEnabled,
                                  double zoom)
{
    StopSimulation();
    m_components  = components;
    m_wires       = wires;
    m_texts       = texts;
    m_gridStep    = std::max(1, gridStep);
    m_gridVisible = gridVisible;
    m_snapEnabled = snapEnabled;
    m_zoom        = (zoom >= 0.01) ? std::min(8.0, zoom) : 1.0;

    // ---- 重建编号计数器与位号计数 ----
    m_nextComponentId = 0;
    m_nextWireId      = 0;
    m_refCount.clear();

    for (const ComponentInstance& comp : m_components)
    {
        m_nextComponentId = std::max(m_nextComponentId, comp.id);

        // 从位号尾部数字恢复计数：U3 → 3
        int split = static_cast<int>(comp.refdes.length());

        while (split > 0 && comp.refdes[split - 1] >= L'0' && comp.refdes[split - 1] <= L'9')
            --split;

        long number = 0;

        if (split < static_cast<int>(comp.refdes.length()) &&
            comp.refdes.Mid(split).ToLong(&number))
        {
            int& counter = m_refCount[comp.name];

            if (static_cast<int>(number) > counter)
                counter = static_cast<int>(number);
        }
    }

    for (const Wire& wire : m_wires)
        m_nextWireId = std::max(m_nextWireId, wire.id);

    // ---- 重置视图与选中状态 ----
    ClearSelectionInternal();
    m_wireDrawing   = false;
    m_dragging      = false;
    m_bandSelecting = false;
    m_pending       = nullptr;
    m_highlightNet  = -1;
    m_netsDirty     = true;
    m_originInit    = false;

    // 换了一份文档，历史记录没有意义了
    m_undoStack.clear();
    m_redoStack.clear();

    // 仿真状态一并复位
    m_sim.SetRunning(false);
    m_sim.Reset(static_cast<int>(m_components.size()));

    EnsureOriginInitialised();
    GetNetGraph();
    CenterOnContent();

    m_modified = false;

    NotifySelectionChanged();

    NotifyViewChanged();
    Refresh();
}

// ============================================================
//  视图操作
// ============================================================

void SchematicCanvas::SetZoomAt(double newZoom, const wxPoint2DDouble& screenAnchor)
{
    newZoom = std::max(0.01, std::min(8.0, newZoom));

    if (std::fabs(newZoom - m_zoom) < 1e-9)
        return;

    const wxPoint2DDouble world = ToWorld(screenAnchor);

    m_zoom = newZoom;

    m_origin.m_x = screenAnchor.m_x - world.m_x * m_zoom;
    m_origin.m_y = screenAnchor.m_y - world.m_y * m_zoom;

    NotifyViewChanged();
    Refresh();
}

void SchematicCanvas::ZoomIn()
{
    const wxSize size = GetClientSize();
    SetZoomAt(m_zoom * 1.25, wxPoint2DDouble(size.x / 2.0, size.y / 2.0));
}

void SchematicCanvas::ZoomOut()
{
    const wxSize size = GetClientSize();
    SetZoomAt(m_zoom / 1.25, wxPoint2DDouble(size.x / 2.0, size.y / 2.0));
}

void SchematicCanvas::ZoomActual()
{
    const wxSize size = GetClientSize();
    SetZoomAt(1.0, wxPoint2DDouble(size.x / 2.0, size.y / 2.0));
}

bool SchematicCanvas::ComputeContentBounds(wxRect& bbox) const
{
    bool hasBounds = false;

    for (const ComponentInstance& comp : m_components)
    {
        const wxRect r = BoxOf(comp);

        if (!hasBounds) { bbox = r;      hasBounds = true; }
        else            { bbox.Union(r); }
    }

    for (const Wire& w : m_wires)
    {
        const wxRect r(std::min(w.a.x, w.b.x), std::min(w.a.y, w.b.y),
                       std::abs(w.b.x - w.a.x) + 1, std::abs(w.b.y - w.a.y) + 1);

        if (!hasBounds) { bbox = r;      hasBounds = true; }
        else            { bbox.Union(r); }
    }

    for (const TextItem& t : m_texts)
    {
        const wxRect r = BoxOfText(t);

        if (!hasBounds) { bbox = r;      hasBounds = true; }
        else            { bbox.Union(r); }
    }

    return hasBounds;
}

void SchematicCanvas::CenterOnContent()
{
    const wxSize size = GetClientSize();

    wxRect bbox;

    if (!ComputeContentBounds(bbox))
    {
        m_origin = wxPoint2DDouble(size.x / 2.0, size.y / 2.0);
        return;
    }

    m_origin.m_x = size.x / 2.0 - (bbox.x + bbox.width / 2.0) * m_zoom;
    m_origin.m_y = size.y / 2.0 - (bbox.y + bbox.height / 2.0) * m_zoom;
}

void SchematicCanvas::ZoomFit()
{
    EnsureOriginInitialised();

    const wxSize size = GetClientSize();

    wxRect bbox;

    if (!ComputeContentBounds(bbox))
    {
        m_zoom   = 1.0;
        m_origin = wxPoint2DDouble(size.x / 2.0, size.y / 2.0);

        NotifyViewChanged();
        Refresh();
        return;
    }

    bbox.Inflate(kPinLen * 3, kPinLen * 3);

    const double zx = static_cast<double>(size.x) / std::max(1, bbox.width);
    const double zy = static_cast<double>(size.y) / std::max(1, bbox.height);

    m_zoom = std::max(0.01, std::min(4.0, std::min(zx, zy)));

    m_origin.m_x = size.x / 2.0 - (bbox.x + bbox.width / 2.0) * m_zoom;
    m_origin.m_y = size.y / 2.0 - (bbox.y + bbox.height / 2.0) * m_zoom;

    NotifyViewChanged();
    Refresh();
}

void SchematicCanvas::SetPendingComponent(const ComponentDef* def)
{
    m_pending = def;

    if (def && m_tool != ToolSelect)
        m_tool = ToolSelect;

    Refresh();
}

void SchematicCanvas::SetTool(Tool tool)
{
    if (m_tool == ToolWire && tool != ToolWire)
        FinishWire(false);      // 工具已经要换了，别再回切一次

    m_tool = tool;

    if (tool != ToolSelect)
        m_pending = nullptr;

    if (tool != ToolWire)
        m_snapActive = false;

    Refresh();
}

void SchematicCanvas::SetGridVisible(bool on)
{
    if (m_gridVisible == on)
        return;

    m_gridVisible = on;
    NotifyViewChanged();
    Refresh();
}

void SchematicCanvas::SetSnapEnabled(bool on)
{
    if (m_snapEnabled == on)
        return;

    m_snapEnabled = on;
    NotifyViewChanged();
}

void SchematicCanvas::SetCrosshairVisible(bool on)
{
    if (m_crosshair == on)
        return;

    m_crosshair = on;
    NotifyViewChanged();
    Refresh();
}

void SchematicCanvas::NotifyViewChanged()
{
    if (OnViewChanged)
        OnViewChanged();
}

} // namespace se





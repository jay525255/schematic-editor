#include "SymbolPainter.h"

#include <wx/brush.h>
#include <wx/pen.h>

#include <algorithm>
#include <cmath>
#include <functional>

namespace se
{

namespace
{

// 四分之一椭圆的三次贝塞尔系数（让圆弧看起来是圆的）
constexpr double kBezierK = 0.5522847498;

// ------------------------------------------------------------
// 形状判定
// ------------------------------------------------------------
struct ShapeSpec
{
    SymbolShape shape  = SymbolShape::Box;
    bool        negate = false;   // 输出端要不要画反相小圈
};

ShapeSpec SpecOf(const wxString& name, ComponentKind kind)
{
    ShapeSpec spec;

    // ---- 逻辑门：复合门要排在单门前面（“与非门”里也有“与门”）----
    if (name.Contains(L"与非门")) { spec.shape = SymbolShape::And; spec.negate = true; return spec; }
    if (name.Contains(L"或非门")) { spec.shape = SymbolShape::Or;  spec.negate = true; return spec; }
    if (name.Contains(L"同或门")) { spec.shape = SymbolShape::Xor; spec.negate = true; return spec; }
    if (name.Contains(L"异或门")) { spec.shape = SymbolShape::Xor; return spec; }
    if (name.Contains(L"与门"))   { spec.shape = SymbolShape::And; return spec; }
    if (name.Contains(L"或门"))   { spec.shape = SymbolShape::Or;  return spec; }
    if (name.Contains(L"非门"))   { spec.shape = SymbolShape::NotGate; spec.negate = true; return spec; }

    // ---- 其他数字器件 ----
    if (name.Contains(L"半加") || name.Contains(L"全加"))  { spec.shape = SymbolShape::Box;      return spec; }
    if (name.Contains(L"运放") || name.Contains(L"运算放大")) { spec.shape = SymbolShape::OpAmp;   return spec; }
    if (name.Contains(L"晶体管"))                          { spec.shape = SymbolShape::Transistor; return spec; }

    // ---- 分立器件 ----
    if (name == L"电阻")   { spec.shape = SymbolShape::Resistor;  return spec; }
    if (name == L"电容")   { spec.shape = SymbolShape::Capacitor; return spec; }
    if (name == L"电感")   { spec.shape = SymbolShape::Inductor;  return spec; }
    if (name == L"二极管") { spec.shape = SymbolShape::Diode;     return spec; }
    if (name == L"LED")    { spec.shape = SymbolShape::Led;       return spec; }

    // ---- 连线与电源 ----
    if (name.Contains(L"时钟")) { spec.shape = SymbolShape::Clock; return spec; }
    if (name.Contains(L"探针")) { spec.shape = SymbolShape::Probe; return spec; }
    if (name.Contains(L"标签")) { spec.shape = SymbolShape::Label; return spec; }

    if (kind == ComponentKind::Power ||
        name == L"VCC" || name == L"GND")
    {
        spec.shape = SymbolShape::Power;
        return spec;
    }

    if (kind == ComponentKind::Port)
    {
        spec.shape = SymbolShape::Pin;
        return spec;
    }

    // 用户自定义的逻辑元件多半也是门（名字里带“门”的已在上面拦掉），
    // 剩下的按与门画，比纯矩形更像逻辑符号
    if (kind == ComponentKind::Logic)
    {
        spec.shape = SymbolShape::And;
        return spec;
    }

    spec.shape = SymbolShape::Box;
    return spec;
}

// ------------------------------------------------------------
// ComponentInstance 里没有 kind 字段（.ses 也只存引脚），
// 所以这里从引脚摆放位置反推形态——和 NetModel::MakePins 的规则互为逆运算。
// ------------------------------------------------------------
ComponentKind KindOfInstance(const ComponentInstance& comp)
{
    bool hasLeft = false, hasRight = false, hasBottom = false;

    for (const Pin& pin : comp.pins)
    {
        switch (pin.side)
        {
            case Side::Left:   hasLeft   = true; break;
            case Side::Right:  hasRight  = true; break;
            case Side::Bottom: hasBottom = true; break;
            default: break;
        }
    }

    if (hasBottom)
        return ComponentKind::Power;

    if (comp.pins.size() <= 1)
        return ComponentKind::Port;

    // 两端器件（电阻/电容/电感/二极管）都是 Passive 方向
    if (comp.pins.size() == 2 && hasLeft && hasRight &&
        comp.pins[0].dir == PinDir::Passive && comp.pins[1].dir == PinDir::Passive)
        return ComponentKind::Passive;

    return ComponentKind::Logic;
}

// ------------------------------------------------------------
// 绘图小工具
// ------------------------------------------------------------
typedef std::function<wxPoint2DDouble(double, double)> Mapper;

Mapper MapperOf(const SymbolPlacement& place)
{
    return [&place](double x, double y) { return place.Map(x, y); };
}

void Stroke(wxGraphicsContext* gc, const Mapper& m, double x1, double y1, double x2, double y2)
{
    const wxPoint2DDouble a = m(x1, y1);
    const wxPoint2DDouble b = m(x2, y2);

    gc->StrokeLine(a.m_x, a.m_y, b.m_x, b.m_y);
}

bool IsGndLike(const wxString& name)
{
    return name.Contains(L"GND") || name.Contains(L"地") || name.Contains(L"GROUND");
}

} // namespace

// ============================================================
SymbolShape ShapeOfComponent(const wxString& name, ComponentKind kind)
{
    return SpecOf(name, kind).shape;
}

// ============================================================
void PaintSymbolBody(wxGraphicsContext* gc,
                     const ComponentInstance& comp,
                     const SymbolPlacement& place,
                     const wxColour& outline,
                     const wxColour& fill,
                     double penWidth,
                     bool dashed)
{
    const ShapeSpec spec = SpecOf(comp.name, KindOfInstance(comp));

    const double w = comp.w;
    const double h = comp.h;
    const double s = place.scale;

    const Mapper m = MapperOf(place);

    // 反相小圈半径（世界单位）
    const double r = std::min(h * 0.14, w * 0.11);

    // 有反相圈的门，本体要缩短 2r，把圈留在框内，引脚才不会压到圈上
    const double bodyW = spec.negate ? std::max(8.0, w - 2 * r) : w;

    gc->SetPen(wxPen(outline, std::max(1.0, penWidth * s),
                     dashed ? wxPENSTYLE_SHORT_DASH : wxPENSTYLE_SOLID));
    gc->SetBrush(wxBrush(fill));

    auto bubble = [&](double cx, double cy)
    {
        const wxPoint2DDouble c = m(cx, cy);

        gc->SetBrush(wxBrush(fill));
        gc->SetPen(wxPen(outline, std::max(1.0, penWidth * s)));
        gc->DrawEllipse(c.m_x - r * s, c.m_y - r * s, 2 * r * s, 2 * r * s);
    };

    switch (spec.shape)
    {
        // ----------------------------------------------------
        // 与门系列（D 形）
        // ----------------------------------------------------
        case SymbolShape::And:
        {
            const double straight = bodyW * 0.55;
            const double rx       = bodyW - straight;

            wxGraphicsPath path = gc->CreatePath();

            path.MoveToPoint(m(0, 0));
            path.AddLineToPoint(m(straight, 0));
            path.AddCurveToPoint(m(straight + rx * kBezierK, 0),
                                 m(bodyW, h / 2 - (h / 2) * kBezierK),
                                 m(bodyW, h / 2));
            path.AddCurveToPoint(m(bodyW, h / 2 + (h / 2) * kBezierK),
                                 m(straight + rx * kBezierK, h),
                                 m(straight, h));
            path.AddLineToPoint(m(0, h));
            path.CloseSubpath();

            gc->DrawPath(path);

            if (spec.negate)
                bubble(bodyW + r, h / 2);

            break;
        }

        // ----------------------------------------------------
        // 或门 / 异或门（盾形）
        // ----------------------------------------------------
        case SymbolShape::Or:
        case SymbolShape::Xor:
        {
            // 异或门的主体右移一点，给左侧那条额外弧让位置
            const double x0 = (spec.shape == SymbolShape::Xor) ? w * 0.16 : 0.0;
            const double bw = std::max(8.0, bodyW - x0);

            wxGraphicsPath path = gc->CreatePath();

            path.MoveToPoint(m(x0, 0));
            path.AddCurveToPoint(m(x0 + bw * 0.42, h * 0.02),
                                 m(x0 + bw * 0.84, h * 0.24),
                                 m(x0 + bw, h / 2));
            path.AddCurveToPoint(m(x0 + bw * 0.84, h * 0.76),
                                 m(x0 + bw * 0.42, h * 0.98),
                                 m(x0, h));
            path.AddCurveToPoint(m(x0 + bw * 0.30, h * 0.72),
                                 m(x0 + bw * 0.30, h * 0.28),
                                 m(x0, 0));
            path.CloseSubpath();

            gc->DrawPath(path);

            if (spec.shape == SymbolShape::Xor)
            {
                // 额外那条弧：贴着本体左侧再画一条“平行”的凹弧。
                // 弧度必须明显小于本体左边缘的弧度，否则中段会被本体的填充盖住，
                // 只剩上下两截，看起来像个尖角。
                const double ax = x0 - w * 0.16;

                wxGraphicsPath arc = gc->CreatePath();

                arc.MoveToPoint(m(ax, 0));
                arc.AddCurveToPoint(m(ax + bw * 0.16, h * 0.30),
                                    m(ax + bw * 0.16, h * 0.70),
                                    m(ax, h));

                gc->SetBrush(*wxTRANSPARENT_BRUSH);
                gc->StrokePath(arc);
            }

            if (spec.negate)
                bubble(x0 + bw + r, h / 2);

            break;
        }

        // ----------------------------------------------------
        // 非门（三角形）
        // ----------------------------------------------------
        case SymbolShape::NotGate:
        {
            wxGraphicsPath path = gc->CreatePath();

            path.MoveToPoint(m(0, 0));
            path.AddLineToPoint(m(bodyW, h / 2));
            path.AddLineToPoint(m(0, h));
            path.CloseSubpath();

            gc->DrawPath(path);

            if (spec.negate)
                bubble(bodyW + r, h / 2);

            break;
        }

        // ----------------------------------------------------
        // 运放（三角形 + 加减号）
        // ----------------------------------------------------
        case SymbolShape::OpAmp:
        {
            gc->SetBrush(wxBrush(fill));
            gc->SetPen(wxPen(outline, std::max(1.0, penWidth * s)));

            wxGraphicsPath path = gc->CreatePath();

            path.MoveToPoint(m(0, 0));
            path.AddLineToPoint(m(w, h / 2));
            path.AddLineToPoint(m(0, h));
            path.CloseSubpath();

            gc->DrawPath(path);

            const double cy1 = h * 0.30;
            const double cy2 = h * 0.70;

            Stroke(gc, m, w * 0.14, cy1, w * 0.34, cy1);
            Stroke(gc, m, w * 0.24, cy1 - h * 0.09, w * 0.24, cy1 + h * 0.09);
            Stroke(gc, m, w * 0.14, cy2, w * 0.34, cy2);

            break;
        }

        // ----------------------------------------------------
        // 矩形框（加法器）
        // ----------------------------------------------------
        case SymbolShape::Box:
        {
            const wxPoint2DDouble tl = m(0, 0);

            gc->DrawRectangle(tl.m_x, tl.m_y, w * s, h * s);

            // 左上角的 “+” 记号（Logisim 的加法器就是个带加号的方框）。
            // 记号要够大：缩到 18 像素的树图标里，太小就完全看不见了。
            const double cx = w * 0.28;
            const double cy = h * 0.30;

            Stroke(gc, m, cx - w * 0.16, cy, cx + w * 0.16, cy);
            Stroke(gc, m, cx, cy - h * 0.16, cx, cy + h * 0.16);

            break;
        }

        // ----------------------------------------------------
        // 端口 / 标签（五边形，尖端朝导线一侧）
        // ----------------------------------------------------
        case SymbolShape::Pin:
        {
            const double tip = std::min(h * 0.5, w * 0.35);

            const bool tipLeft = !comp.pins.empty() && comp.pins.front().side == Side::Left;

            wxGraphicsPath path = gc->CreatePath();

            if (tipLeft)
            {
                path.MoveToPoint(m(0, h / 2));
                path.AddLineToPoint(m(tip, 0));
                path.AddLineToPoint(m(w, 0));
                path.AddLineToPoint(m(w, h));
                path.AddLineToPoint(m(tip, h));
            }
            else
            {
                path.MoveToPoint(m(0, 0));
                path.AddLineToPoint(m(w - tip, 0));
                path.AddLineToPoint(m(w, h / 2));
                path.AddLineToPoint(m(w - tip, h));
                path.AddLineToPoint(m(0, h));
            }

            path.CloseSubpath();
            gc->DrawPath(path);

            break;
        }

        // ----------------------------------------------------
        // 网络标签（旗形），和端口区分开
        // ----------------------------------------------------
        case SymbolShape::Label:
        {
            const double tip = std::min(w * 0.26, h * 0.5);

            wxGraphicsPath path = gc->CreatePath();

            path.MoveToPoint(m(0, h / 2));
            path.AddLineToPoint(m(tip, 0));
            path.AddLineToPoint(m(w, 0));
            path.AddLineToPoint(m(w, h));
            path.AddLineToPoint(m(tip, h));
            path.CloseSubpath();

            gc->DrawPath(path);

            break;
        }

        // ----------------------------------------------------
        // 时钟（方框 + 方波）
        // ----------------------------------------------------
        case SymbolShape::Clock:
        {
            const wxPoint2DDouble tl = m(0, 0);

            gc->DrawRectangle(tl.m_x, tl.m_y, w * s, h * s);

            const double yLo = h * 0.66;
            const double yHi = h * 0.34;

            Stroke(gc, m, w * 0.18, yLo, w * 0.18, yHi);
            Stroke(gc, m, w * 0.18, yHi, w * 0.46, yHi);
            Stroke(gc, m, w * 0.46, yHi, w * 0.46, yLo);
            Stroke(gc, m, w * 0.46, yLo, w * 0.74, yLo);
            Stroke(gc, m, w * 0.74, yLo, w * 0.74, yHi);

            break;
        }

        // ----------------------------------------------------
        // 探针（实心小圆）
        // ----------------------------------------------------
        case SymbolShape::Probe:
        {
            const wxPoint2DDouble c = m(w / 2, h / 2);
            const double rad = std::min(w, h) * 0.32;

            gc->SetBrush(wxBrush(outline));
            gc->DrawEllipse(c.m_x - rad * s, c.m_y - rad * s, 2 * rad * s, 2 * rad * s);

            break;
        }

        // ----------------------------------------------------
        // 电源 / 地
        // ----------------------------------------------------
        case SymbolShape::Power:
        {
            const bool gnd = IsGndLike(comp.name);

            if (gnd)
            {
                Stroke(gc, m, w / 2, h, w / 2, h * 0.55);

                Stroke(gc, m, w * 0.15, h * 0.55, w * 0.85, h * 0.55);
                Stroke(gc, m, w * 0.28, h * 0.70, w * 0.72, h * 0.70);
                Stroke(gc, m, w * 0.41, h * 0.85, w * 0.59, h * 0.85);
            }
            else
            {
                Stroke(gc, m, w / 2, h, w / 2, h * 0.55);
                Stroke(gc, m, w * 0.18, h * 0.55, w * 0.82, h * 0.55);
            }

            break;
        }

        // ----------------------------------------------------
        // 电阻（锯齿）
        // ----------------------------------------------------
        case SymbolShape::Resistor:
        {
            const double y  = h / 2;
            const double a  = std::min(h * 0.16, w * 0.5);
            const double x0 = w * 0.12;
            const double x1 = w * 0.88;

            Stroke(gc, m, 0, y, x0, y);

            const int zigs = 6;

            double prevX = x0;
            double prevY = y;

            for (int i = 1; i <= zigs; ++i)
            {
                const double x  = x0 + (x1 - x0) * i / zigs;
                const double yy = (i == zigs) ? y : (y + ((i % 2) ? -a : a));

                Stroke(gc, m, prevX, prevY, x, yy);

                prevX = x;
                prevY = yy;
            }

            Stroke(gc, m, x1, y, w, y);

            break;
        }

        // ----------------------------------------------------
        // 电容（两块极板）
        // ----------------------------------------------------
        case SymbolShape::Capacitor:
        {
            const double y = h / 2;
            const double p = h * 0.22;

            Stroke(gc, m, 0, y, w * 0.42, y);
            Stroke(gc, m, w * 0.42, y - p, w * 0.42, y + p);
            Stroke(gc, m, w * 0.58, y - p, w * 0.58, y + p);
            Stroke(gc, m, w * 0.58, y, w, y);

            break;
        }

        // ----------------------------------------------------
        // 电感（三个半圆）
        // ----------------------------------------------------
        case SymbolShape::Inductor:
        {
            const double y   = h / 2;
            const double amp = h * 0.18;
            const double x0  = w * 0.15;
            const double x1  = w * 0.85;
            const int    n   = 3;
            const double seg = (x1 - x0) / n;

            Stroke(gc, m, 0, y, x0, y);

            gc->SetBrush(*wxTRANSPARENT_BRUSH);

            for (int i = 0; i < n; ++i)
            {
                const double xa = x0 + seg * i;
                const double xb = xa + seg;

                wxGraphicsPath arc = gc->CreatePath();

                arc.MoveToPoint(m(xa, y));
                arc.AddCurveToPoint(m(xa + seg * 0.36, y - amp),
                                    m(xb - seg * 0.36, y - amp),
                                    m(xb, y));

                gc->StrokePath(arc);
            }

            Stroke(gc, m, x1, y, w, y);

            break;
        }

        // ----------------------------------------------------
        // 二极管 / LED
        // ----------------------------------------------------
        case SymbolShape::Diode:
        case SymbolShape::Led:
        {
            const double y   = h / 2;
            const double amp = h * 0.16;

            Stroke(gc, m, 0, y, w * 0.38, y);

            wxGraphicsPath tri = gc->CreatePath();

            tri.MoveToPoint(m(w * 0.38, y - amp));
            tri.AddLineToPoint(m(w * 0.38, y + amp));
            tri.AddLineToPoint(m(w * 0.63, y));
            tri.CloseSubpath();

            gc->SetBrush(wxBrush(fill));
            gc->SetPen(wxPen(outline, std::max(1.0, penWidth * s)));
            gc->DrawPath(tri);

            Stroke(gc, m, w * 0.63, y - amp, w * 0.63, y + amp);
            Stroke(gc, m, w * 0.63, y, w, y);

            if (spec.shape == SymbolShape::Led)
            {
                // 两个发光箭头
                Stroke(gc, m, w * 0.55, y - amp * 1.6, w * 0.78, y - amp * 2.6);
                Stroke(gc, m, w * 0.78, y - amp * 2.6, w * 0.70, y - amp * 2.4);
                Stroke(gc, m, w * 0.78, y - amp * 2.6, w * 0.76, y - amp * 2.1);

                Stroke(gc, m, w * 0.72, y - amp * 1.4, w * 0.95, y - amp * 2.4);
                Stroke(gc, m, w * 0.95, y - amp * 2.4, w * 0.87, y - amp * 2.2);
                Stroke(gc, m, w * 0.95, y - amp * 2.4, w * 0.93, y - amp * 1.9);
            }

            break;
        }

        // ----------------------------------------------------
        // 三极管（圆圈 + 基极 + 两个箭头）
        // ----------------------------------------------------
        case SymbolShape::Transistor:
        {
            const double cx = w * 0.45;
            const double cy = h / 2;
            const double rad = std::min(w, h) * 0.40;

            const wxPoint2DDouble c = m(cx, cy);

            gc->SetBrush(wxBrush(fill));
            gc->SetPen(wxPen(outline, std::max(1.0, penWidth * s)));
            gc->DrawEllipse(c.m_x - rad * s, c.m_y - rad * s, 2 * rad * s, 2 * rad * s);

            // 基极竖条
            Stroke(gc, m, w * 0.36, cy - h * 0.22, w * 0.36, cy + h * 0.22);

            // 基极引线
            Stroke(gc, m, 0, cy, w * 0.36, cy);

            // 集电极 / 发射极
            Stroke(gc, m, w * 0.36, cy - h * 0.12, w * 0.66, cy - h * 0.30);
            Stroke(gc, m, w * 0.36, cy + h * 0.12, w * 0.66, cy + h * 0.30);

            // 发射极箭头（NPN 朝外）
            Stroke(gc, m, w * 0.52, cy + h * 0.21, w * 0.60, cy + h * 0.16);
            Stroke(gc, m, w * 0.52, cy + h * 0.21, w * 0.57, cy + h * 0.30);

            break;
        }

        default:
            break;
    }
}

// ============================================================
void PaintSymbolPins(wxGraphicsContext* gc,
                     const ComponentInstance& comp,
                     const SymbolPlacement& place,
                     const wxColour& colour,
                     double penWidth,
                     bool endDots,
                     int hotPinIndex,
                     const wxColour& hotColour)
{
    const double s = place.scale;

    for (size_t k = 0; k < comp.pins.size(); ++k)
    {
        const wxPoint anchorW = PinAnchor(comp, static_cast<int>(k));
        const wxPoint outerW  = PinPosition(comp, static_cast<int>(k));

        const wxPoint2DDouble a =
            place.Map(anchorW.x - comp.pos.x, anchorW.y - comp.pos.y);
        const wxPoint2DDouble b =
            place.Map(outerW.x - comp.pos.x, outerW.y - comp.pos.y);

        const bool hot = (hotPinIndex == static_cast<int>(k));
        const wxColour wireColour = hot ? hotColour : colour;

        gc->SetPen(wxPen(wireColour, std::max(1.0, (hot ? penWidth * 1.6 : penWidth) * s)));
        gc->StrokeLine(a.m_x, a.m_y, b.m_x, b.m_y);

        if (!endDots)
            continue;

        const double rad = 2.6 * s;

        gc->SetPen(*wxTRANSPARENT_PEN);
        gc->SetBrush(wxBrush(wireColour));
        gc->DrawEllipse(b.m_x - rad, b.m_y - rad, 2 * rad, 2 * rad);
    }
}

// ============================================================
void PaintSymbolLabels(wxGraphicsContext* gc,
                       const ComponentInstance& comp,
                       const SymbolPlacement& place,
                       const wxFont& pinFont,
                       const wxFont& refFont,
                       const wxFont& nameFont,
                       const wxColour& pinColour,
                       const wxColour& refColour,
                       const wxColour& nameColour,
                       bool showPinNames,
                       bool showText)
{
    const double w = comp.w;
    const double h = comp.h;

    // ---- 引脚名：放在引脚线外侧，绝不压住符号本体 ----
    if (showPinNames)
    {
        for (size_t k = 0; k < comp.pins.size(); ++k)
        {
            const Pin& pin = comp.pins[k];

            // 电源符号的引脚名就是元件名（VCC / GND），画两遍纯属重复
            if (!comp.name.IsEmpty() && pin.name.CmpNoCase(comp.name) == 0)
                continue;

            const wxPoint outerW = PinPosition(comp, static_cast<int>(k));
            const wxPoint2DDouble o =
                place.Map(outerW.x - comp.pos.x, outerW.y - comp.pos.y);

            // 先设字体再量尺寸（wxGDI+ 的老规矩）
            gc->SetFont(pinFont, pinColour);

            wxDouble tw = 0, th = 0, td = 0, tel = 0;
            gc->GetTextExtent(pin.name, &tw, &th, &td, &tel);

            switch (pin.side)
            {
                case Side::Left:
                    gc->DrawText(pin.name, o.m_x - tw - 2, o.m_y - th / 2);
                    break;

                case Side::Right:
                    gc->DrawText(pin.name, o.m_x + 2, o.m_y - th / 2);
                    break;

                case Side::Top:
                    gc->DrawText(pin.name, o.m_x - tw / 2, o.m_y - th - 2);
                    break;

                case Side::Bottom:
                default:
                    gc->DrawText(pin.name, o.m_x - tw / 2, o.m_y + 2);
                    break;
            }
        }
    }

    if (!showText)
        return;

    const wxPoint2DDouble tl = place.Map(0, 0);

    wxDouble tw = 0, th = 0, td = 0, tel = 0;
    const double bodyW = w * place.scale;

    // ---- 位号（上方居中）----
    //   只写位号（U1 / R2 / OUT1），不写“与门 / 电阻”这类类型名：
    //   符号本身已经把“是什么元件”表达清楚了，再补一行字只会压住图形。
    if (!comp.refdes.IsEmpty())
    {
        gc->SetFont(refFont, refColour);
        gc->GetTextExtent(comp.refdes, &tw, &th, &td, &tel);

        gc->DrawText(comp.refdes, tl.m_x + bodyW / 2 - tw / 2, tl.m_y - th - 1);
    }
}

} // namespace se






// ============================================================
//  SymbolPainter.h  —— 元件符号绘制（Logisim 风格）
//
//  为什么单独抽一个文件：
//    * 绘图区（SchematicCanvas）画的是“放好的元件”；
//    * 元件库面板要画“还没放的元件预览”和 16×16 的树图标；
//    * 三处必须长得一模一样，否则用户会觉得“预览和放下去的不是一个东西”。
//  所以形状只在这里定义一次，三处都调这里。
//
//  坐标约定：所有形状都按“元件外框”的局部坐标写，
//  即 (0,0) 是外框左上角、(comp.w, comp.h) 是右下角，
//  再由 SymbolPlacement（屏幕原点 + 缩放）映射到屏幕。
//  引脚位置不在这里算——直接复用 NetModel 的 PinAnchor/PinPosition，
//  保证“画出来的形状”和“导线的接点”永远对齐。
// ============================================================

#pragma once

#include <wx/graphics.h>

#include <wx/colour.h>
#include <wx/font.h>
#include <wx/string.h>

#include "NetModel.h"

namespace se
{

// ------------------------------------------------------------
// 符号种类：决定画什么形状
// ------------------------------------------------------------
enum class SymbolShape
{
    Box,         // 矩形框（加法器等带内部记号的）
    And,         // 与门（D 形）
    Or,          // 或门（盾形）
    Xor,         // 异或门（盾形 + 左侧额外弧）
    NotGate,     // 非门（三角形）
    OpAmp,       // 运放（三角形 + 加减号）
    Pin,         // 端口（五边形，尖端朝导线）
    Clock,       // 时钟（方波）
    Probe,       // 探针（实心圆）
    Label,       // 网络标签（旗形）
    Power,       // 电源 / 地
    Resistor,    // 电阻（锯齿）
    Capacitor,   // 电容（两板）
    Inductor,    // 电感（线圈）
    Diode,       // 二极管（三角 + 横杠）
    Led,         // 发光二极管（二极管 + 箭头）
    Transistor   // 三极管（圆圈 + 箭头）
};

// 按元件名判断该画什么（自定义元件也能落到合适的形状上）
SymbolShape ShapeOfComponent(const wxString& name, ComponentKind kind);

// 元件外框左上角在屏幕上的位置 + 世界→屏幕缩放
struct SymbolPlacement
{
    wxPoint2DDouble origin { 0.0, 0.0 };
    double          scale  = 1.0;

    wxPoint2DDouble Map(double x, double y) const
    {
        return wxPoint2DDouble(origin.m_x + x * scale, origin.m_y + y * scale);
    }
};

// 元件本体（描边 + 填充）
//   dashed = true 用于"待放置"的幽灵预览
void PaintSymbolBody(wxGraphicsContext* gc,
                     const ComponentInstance& comp,
                     const SymbolPlacement& place,
                     const wxColour& outline,
                     const wxColour& fill,
                     double penWidth,
                     bool dashed = false);

// 引脚短线（锚点 → 外端）；endDots 为 true 时在外端画小圆点
void PaintSymbolPins(wxGraphicsContext* gc,
                     const ComponentInstance& comp,
                     const SymbolPlacement& place,
                     const wxColour& colour,
                     double penWidth,
                     bool endDots,
                     int hotPinIndex = -1,
                     const wxColour& hotColour = wxColour(230, 120, 20));

// 引脚名 / 位号 / 元件名
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
                       bool showText);

} // namespace se


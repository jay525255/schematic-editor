// ============================================================
//  NetModel.h  —— 原理图领域模型（引脚级网络模型）
//
//  这是整个编辑器的“内核”，绘图区只负责显示，本层负责语义：
//
//    ComponentInstance ── 元件实例，含若干 Pin
//    Wire              ── 一条正交导线（水平或垂直）
//    Net               ── 电气连通的引脚集合（由连通性算法算出）
//
//  连通规则（与 KiCad / Logisim 一致）：
//    * 导线两端点重合          → 连通
//    * 导线端点落在另一条导线上 → 连通（T 型连接，画连接点）
//    * 引脚落在导线上           → 连通
//    * 两条导线十字交叉         → **不连通**（除非放连接点）
//
//  网表结构与导出格式也定义在本层（任务 5 的基础）。
// ============================================================

#pragma once

#include <wx/gdicmn.h>
#include <wx/string.h>

#include <vector>

namespace se
{

// ------------------------------------------------------------
// 元件的电气"形态"，决定引脚怎么摆
// ------------------------------------------------------------
enum class ComponentKind
{
    Logic,    // 逻辑门：输入在左，输出在右
    Passive,  // 无源器件：左 1、右 2
    Power,    // 电源/地：底部单引脚
    Port      // 输入/输出端口：单引脚
};

// 引脚方向
enum class PinDir
{
    Input,
    Output,
    Bidir,
    Passive
};

// 引脚所在的一侧
enum class Side
{
    Left,
    Right,
    Top,
    Bottom
};

// ------------------------------------------------------------
// 引脚
// ------------------------------------------------------------
struct Pin
{
    wxString name;                 // "A" / "Y" / "1" / "GND"
    PinDir   dir   = PinDir::Input;
    Side     side  = Side::Left;
    int      slot  = 0;            // 同侧第几个（从 0 开始）
    int      count = 1;            // 同侧共几个（用于均匀分布）
    int      number = 0;           // 引脚序号（1,2,3…）—— PCB 网表按序号引用引脚
};

// ------------------------------------------------------------
// 元件实例
// ------------------------------------------------------------
struct ComponentInstance
{
    int              id     = 0;   // 实例唯一 id
    wxString         name;         // 元件名，例如 "与门"
    wxString         refdes;       // 位号，例如 "U1"
    wxPoint          pos;          // 元件框左上角（世界坐标）
    int              w      = 60;
    int              h      = 40;
    std::vector<Pin> pins;

    // ---- PCB 网表需要的两项 ----
    //  footprint 为空表示"不是物理元件"（电源符号、网络标签等），
    //  这类对象只参与网络命名，不写进 PCB 网表的元件表与节点表。
    wxString         footprint;    // 封装名，例如 "DIP-14"
    wxString         value;        // 元件值/型号，例如 "AND2"

    // ---- 旋转 / 镜像（任务 4）----
    //  只记录"作者坐标系 → 屏幕坐标系"的净变换，共 8 种
    //  （正方形对称群 D4）：0..3 = 顺时针转 0°/90°/180°/270°，
    //  4..7 = 先水平镜像，再顺时针转 0°/90°/180°/270°。
    //
    //  注意：pins 里的 side/slot/count 存的是**变换之后**的值，
    //  所以 PinAnchor / PinPosition / 连通性求解完全不用管这个字段；
    //  只有绘制符号（SymbolPainter）要靠它把形状转到同样朝向。
    int              xform  = 0;

    // Explicit digital behavior and discrete timing. Legacy files omit these fields.
    wxString         logicModel;
    wxString         truthTable;
    int              delayTicks = 1;
    int              clockPeriodTicks = 10;

};

// ------------------------------------------------------------
// 导线（一条轴对齐线段；折线 = 多条首尾相接的导线）
// ------------------------------------------------------------
struct Wire
{
    int     id = 0;
    wxPoint a;
    wxPoint b;

    bool IsAxisAligned() const { return a.x == b.x || a.y == b.y; }
    int  Length() const;
};

// ------------------------------------------------------------
// 文本注记（"放置文本"放置的东西）
//
//  纯图面元素，不参与电气连通性计算 —— 只是为了在原理图上
//  写注释、标题、说明用的，和 NetBuilder 完全没有关系。
// ------------------------------------------------------------
struct TextItem
{
    wxString text;
    wxPoint  pos;                 // 世界坐标：文字锚点（左上角）
    int      pointSize = 12;      // 字号（磅），绘制时随缩放等比放大
};

// ------------------------------------------------------------
// 网络中的一个引脚引用
// ------------------------------------------------------------
struct PinRef
{
    int      compIndex = -1;
    int      pinIndex  = -1;
    wxString refdes;
    wxString name;
    wxPoint  pos;
};

// ------------------------------------------------------------
// 网络
// ------------------------------------------------------------
struct Net
{
    int                  index = 0;    // 从 1 开始
    wxString             name;         // N1 / GND / VCC
    std::vector<PinRef>  pins;
    std::vector<wxPoint> nodes;        // 该网络的所有连接点
    bool                 isPower = false;
};

// ------------------------------------------------------------
// 连通性计算结果
// ------------------------------------------------------------
struct NetGraph
{
    std::vector<Net>     nets;
    std::vector<int>     wireNet;   // 与 wires 一一对应，-1 = 悬空导线
    std::vector<std::vector<int>> pinNet;  // [元件下标][引脚下标] → 网络下标，-1 = 未连接
    std::vector<wxPoint> junctions; // 需要画实心连接点的位置

    int NetOfWire(int wireIndex) const
    {
        if (wireIndex < 0 || wireIndex >= static_cast<int>(wireNet.size()))
            return -1;

        return wireNet[wireIndex];
    }
};

// ------------------------------------------------------------
// 几何工具
// ------------------------------------------------------------
const int kPinLen = 10;   // 引脚线长度（世界坐标）

// 引脚根部（在元件框边上）与引脚外端（导线该连到这里）
wxPoint PinAnchor(const ComponentInstance& comp, int pinIndex);
wxPoint PinPosition(const ComponentInstance& comp, int pinIndex);
Side    PinSide(const ComponentInstance& comp, int pinIndex);

bool PointOnSegment(const wxPoint& p, const wxPoint& a, const wxPoint& b);

// 某个导线端点是否"接到了东西"（引脚 / 别的导线）。
// 绘图区用它画悬空标记，ERC 用它报悬空导线，共用同一套判定。
bool IsEndpointConnected(const std::vector<ComponentInstance>& components,
                         const std::vector<Wire>&              wires,
                         const wxPoint&                        point,
                         int                                   selfWireIndex);

// 由“形态 + 引脚数”生成引脚表
std::vector<Pin> MakePins(const wxString& compName,
                          ComponentKind   kind,
                          int             inputs,
                          int             outputs);

// ------------------------------------------------------------
// 旋转 / 镜像（任务 4）
//
//  两步走：
//    1) 把每个引脚的锚点整体做一次刚体变换（90° 时外框 w/h 互换）；
//    2) 从变换后的锚点反推"落在哪条边、这一边排第几"，
//       写回 side / slot / count。
//  这样"引脚换边、同侧顺序颠倒"全自动正确，不用为每种元件单独写规则。
//  xform 同时更新，绘制时照着它转形状（见 SymbolPainter）。
// ------------------------------------------------------------
void RotateComponent(ComponentInstance& comp, int quartersCW);   // +1 = 顺时针 90°
void MirrorComponent(ComponentInstance& comp, bool horizontal);  // 水平 / 垂直翻转

// xform → 人话（属性面板显示用）
wxString DescribeXform(int xform);

// ------------------------------------------------------------
// 连通性求解 + 网表输出 + 电气规则检查
// ------------------------------------------------------------
class NetBuilder
{
public:
    static NetGraph Build(const std::vector<ComponentInstance>& components,
                          const std::vector<Wire>&              wires);

    // 网表文本（网络清单 + 元件清单，面向上人阅读）
    static wxString ToNetlist(const std::vector<ComponentInstance>& components,
                              const NetGraph&                       graph);

    // 标准 PCB 网表（Protel / Altium 格式，可被 Altium / KiCad 直接导入）
    //
    //    [
    //    U1
    //    DIP-14
    //    AND2
    //    ]
    //    (
    //    N3
    //    U1-3
    //    U2-1
    //    )
    //
    //  [ ] 段：位号 / 封装 / 元件值
    //  ( ) 段：网络名 + 节点（位号-引脚号）
    //  封装为空的元件（电源符号、网络标签）不写入，它们只负责网络命名。
    static wxString ToProtel(const std::vector<ComponentInstance>& components,
                             const NetGraph&                       graph);

    // 电气规则检查（ERC）：悬空引脚 / 单端导线 / 无引脚网络
    static wxString RunErc(const std::vector<ComponentInstance>& components,
                           const std::vector<Wire>&              wires,
                           const NetGraph&                       graph);
};

} // namespace se

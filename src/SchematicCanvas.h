// ============================================================
//  SchematicCanvas.h  —— 绘图区
//
//  文档内容由领域模型（NetModel.h）承载：
//      std::vector<ComponentInstance>  元件实例
//      std::vector<Wire>               导线
//  本类只负责：坐标变换、绘制、鼠标/键盘交互。
//
//  已实现：
//    * 世界坐标 / 屏幕坐标双向变换（缩放、平移）
//    * 网格（次网格 + 主网格）、原点标记
//    * 元件放置、选中（支持多选）、拖动移动、删除，位号自动编号
//    * 框选（橡皮筋）
//    * 撤销 / 重做（快照式，见下方说明）
//    * 导线绘制（点击式，正交折线，自动吸附到引脚/导线端点）
//    * 连接点（junction）自动判定与绘制
//    * 连通性求解 + 网络高亮（鼠标悬停即高亮整条网络）
//    * 网表生成 / 电气规则检查（委托 NetBuilder）
//    * 复制 / 剪切 / 粘贴（粘贴时位号自动重新编号）
//    * 元件旋转 / 水平翻转 / 垂直翻转（转完引脚换边、导线端点尽量跟随）
//
//  尚未实现：
//    * 元件属性（位号 / 封装 / 型号）在属性表里直接改
// ============================================================

#pragma once

#include <wx/wx.h>
#include <wx/geometry.h>

#include <functional>
#include <map>
#include <vector>

#include "ComponentCatalog.h"
#include "NetModel.h"
#include "SimModel.h"

namespace se
{

class SchematicCanvas : public wxWindow
{
public:
    // 工具模式
    enum Tool
    {
        ToolSelect = 0,   // 选择 / 放置 / 拖动
        ToolWire,         // 画导线
        ToolText          // 放文本注记
    };

    explicit SchematicCanvas(wxWindow* parent);

    // ---- 元件库联动 ----------------------------------------
    void                SetPendingComponent(const ComponentDef* def);
    const ComponentDef* GetPendingComponent() const { return m_pending; }

    // ---- 工具 ----------------------------------------------
    void SetTool(Tool tool);
    Tool GetTool() const { return m_tool; }

    // ---- 视图操作 ------------------------------------------
    void ZoomIn();
    void ZoomOut();
    void ZoomFit();
    void ZoomActual();

    bool IsGridVisible() const { return m_gridVisible; }
    bool IsSnapEnabled() const { return m_snapEnabled; }
    bool IsCrosshairVisible() const { return m_crosshair; }

    void SetGridVisible(bool on);
    void SetSnapEnabled(bool on);
    void SetCrosshairVisible(bool on);

    // ---- 状态查询 ------------------------------------------
    double GetZoom() const { return m_zoom; }
    int    GetGridStep() const { return m_gridStep; }
    int    GetComponentCount() const { return static_cast<int>(m_components.size()); }
    int    GetWireCount() const { return static_cast<int>(m_wires.size()); }

    bool IsDrawingWire() const { return m_wireDrawing; }
    bool IsDragging() const { return m_dragging; }

    // ---- 选中状态（支持多选）-------------------------------
    bool HasSelection() const
    {
        return !m_selComponents.empty() || !m_selWires.empty() || !m_selTexts.empty();
    }
    int  GetSelectedComponentCount() const { return static_cast<int>(m_selComponents.size()); }
    int  GetSelectedWireCount() const { return static_cast<int>(m_selWires.size()); }
    int  GetSelectedTextCount() const { return static_cast<int>(m_selTexts.size()); }
    int  GetSelectionCount() const
    {
        return GetSelectedComponentCount() + GetSelectedWireCount() + GetSelectedTextCount();
    }

    // 仅在“恰好选中一个”时有效，否则返回 nullptr
    const ComponentInstance* GetSelectedComponent() const;
    const Wire*              GetSelectedWire() const;
    const TextItem*          GetSelectedText() const;
    int                      GetSelectedComponentIndex() const;

    const std::vector<int>& GetSelectedComponents() const { return m_selComponents; }
    const std::vector<int>& GetSelectedWires() const { return m_selWires; }
    const std::vector<int>& GetSelectedTexts() const { return m_selTexts; }

    int  GetSelectedNetIndex() const;
    int  GetHighlightNetIndex() const { return m_highlightNet; }

    bool IsComponentSelected(int index) const;
    bool IsWireSelected(int index) const;
    bool IsTextSelected(int index) const;

    // ---- 文档访问 / 派生数据 --------------------------------
    const std::vector<ComponentInstance>& GetComponents() const { return m_components; }
    const std::vector<Wire>&              GetWires() const { return m_wires; }
    const std::vector<TextItem>&          GetTexts() const { return m_texts; }
    int                                   GetTextCount() const { return static_cast<int>(m_texts.size()); }

    // 放置一段文本注记（由文本工具调用；会压撤销快照）
    void PlaceText(const wxString& text, const wxPoint& worldPos);

    const NetGraph& GetNetGraph();          // 需要时自动重算
    void            MarkNetGraphDirty() { m_netsDirty = true; }

    wxString BuildNetlist();          // 可读报告（网络清单 + 元件清单）
    wxString BuildNetlistProtel();    // 标准 PCB 网表（Protel/Altium 格式）
    wxString BuildErcReport();

    // ---- 逻辑仿真（任务 6）--------------------------------
    bool IsSimulating() const { return m_sim.IsRunning(); }

    void StartSimulation();           // 开始仿真（初始化输入并求值）
    void StopSimulation();            // 停止仿真（恢复普通着色）
    void RecomputeSimulation();       // 电路变动后重算一次
    void TickClock();                 // 打一拍时钟

    // 点击某个位置：若命中"输入引脚/常量"，切换它的值，返回该元件下标；否则 -1
    int  ToggleInputAt(const wxPoint& world);

    wxString BuildSimulationReport();

    const Simulator& GetSimulator() const { return m_sim; }

    // ---- 文档装载 / 修改状态（任务 5）----------------------
    void SetDocument(const std::vector<ComponentInstance>& components,
                     const std::vector<Wire>&              wires,
                     const std::vector<TextItem>&          texts,
                     int    gridStep,
                     bool   gridVisible,
                     bool   snapEnabled,
                     double zoom);

    bool IsModified() const { return m_modified; }
    void MarkSaved() { m_modified = false; }

    // ---- 编辑 ----------------------------------------------
    void SelectAll();
    // label 为空时按“删除 N 元件 / …”自动命名撤销步骤；
    // 剪切时传“剪切”，这样撤销列表里看到的是用户真正做过的操作
    void DeleteSelection(const wxString& label = wxEmptyString);
    void DeselectAll();
    void ClearAll(bool markModified = false);
    void ResetDocument();          // 新建：清内容 + 清撤销栈
    void FinishWire(bool returnToSelect = true);

    // ---- 剪贴板（任务 4）----------------------------------
    //  复制的是"当时选中的那些对象"的一份拷贝，和文档本身脱钩：
    //  复制之后再删原件、再撤销，剪贴板内容都不受影响。
    //  粘贴时整体平移、位号重新编号，然后自动选中新贴出来的对象。
    bool CanPaste() const { return !m_clip.Empty(); }
    bool HasClipboard() const { return !m_clip.Empty(); }
    void CopySelection();
    void CutSelection();
    void PasteClipboard();

    // ---- 旋转 / 翻转（任务 4）-----------------------------
    //  quartersCW：+1 = 顺时针 90°，-1 = 逆时针 90°
    void RotateSelection(int quartersCW);
    void FlipSelection(bool horizontal);

    // ---- 撤销 / 重做（快照式）-------------------------------
    //  每次“会改变文档的操作”之前先压一份完整快照。
    //  之所以不用 Command 模式：本工程元件量级只有几百，
    //  快照的开销可以忽略，而实现简单、不会漏掉任何一种操作。
    //  详见 README 第 6 节“多方案对比”。
    bool     CanUndo() const { return !m_undoStack.empty(); }
    bool     CanRedo() const { return !m_redoStack.empty(); }
    bool     Undo();
    bool     Redo();
    wxString GetUndoLabel() const;
    wxString GetRedoLabel() const;
    int      GetUndoDepth() const { return static_cast<int>(m_undoStack.size()); }
    int      GetRedoDepth() const { return static_cast<int>(m_redoStack.size()); }

    // ---- 回调（由 MainFrame 挂接，实现面板间联动）-----------
    std::function<void(const wxPoint&)>  OnCursorMoved;
    std::function<void()>                OnSelectionChanged;
    std::function<void()>                OnViewChanged;
    std::function<void(const wxString&)> OnStatusMessage;

    // 放完一个元件后触发（画布已经把“待放置”清掉了），
    // 主窗口用它把元件库里高亮的那一项也一起取消掉
    std::function<void()>                OnPlaceFinished;

    // 一段导线画完（收尾）时触发：主窗口把工具栏切回“选择”，
    // 这样画完线不用再点右键 / 再点一次工具
    std::function<void()>                OnWireFinished;

private:
    // ---- 撤销快照 ------------------------------------------
    struct Snapshot
    {
        std::vector<ComponentInstance> components;
        std::vector<Wire>              wires;
        std::vector<TextItem>          texts;
        int                            nextComponentId = 0;
        int                            nextWireId      = 0;
        std::map<wxString, int>        refCount;
        wxString                       label;
    };

    // ---- 剪贴板内容（任务 4）-------------------------------
    //  存的是"对象的一份拷贝"，不是指针：复制之后原件被删掉 / 被撤销，
    //  剪贴板里仍然是当初复制的那一份，粘贴照样能用。
    struct Clipboard
    {
        std::vector<ComponentInstance> components;
        std::vector<Wire>              wires;
        std::vector<TextItem>          texts;
        wxPoint                        anchor { 0, 0 };   // 复制时包围盒左上角（世界坐标）

        bool Empty() const
        {
            return components.empty() && wires.empty() && texts.empty();
        }
    };

    Clipboard m_clip;

    // 粘贴时给元件重新编号：U1 → 现有最大 U 号 + 1
    wxString NextRefdes(const wxString& protoRefdes, const wxString& compName);

    // 旋转 / 翻转的公共实现：apply 把单个元件变换到新姿态，
    // 之后统一处理“贴在引脚上的导线端点”要不要跟着走
    void TransformSelection(const std::function<void(ComponentInstance&)>& apply,
                            const wxString& label);

    // 事件
    void OnPaint(wxPaintEvent& event);
    void OnEraseBackground(wxEraseEvent& event);
    void OnSize(wxSizeEvent& event);
    void OnMouseMove(wxMouseEvent& event);
    void OnLeftDown(wxMouseEvent& event);
    void OnLeftUp(wxMouseEvent& event);
    void OnRightDown(wxMouseEvent& event);
    void OnLeftDClick(wxMouseEvent& event);
    void OnMiddleDown(wxMouseEvent& event);
    void OnMiddleUp(wxMouseEvent& event);
    void OnMouseWheel(wxMouseEvent& event);
    void OnKeyDown(wxKeyEvent& event);
    void OnLeaveWindow(wxMouseEvent& event);

    // 坐标变换
    wxPoint2DDouble ToScreen(const wxPoint2DDouble& world) const;
    wxPoint2DDouble ToWorld(const wxPoint2DDouble& screen) const;
    wxPoint         SnapToGrid(const wxPoint& world) const;

    // 吸附：优先吸到引脚 / 导线端点，其次吸到网格
    wxPoint SnapForWiring(const wxPoint& world) const;
    bool    FindNearbyAttachPoint(const wxPoint& world, wxPoint& out,
                                  int* kind = nullptr, wxString* label = nullptr) const;

    // 几何
    static wxRect BoxOf(const ComponentInstance& comp);
    wxRect        BoxOfText(const TextItem& item) const;   // 世界坐标下的文字外框
    int           HitTestComponent(const wxPoint& world) const;
    int           HitTestWire(const wxPoint& world) const;
    int           HitTestText(const wxPoint& world) const;

    // 选中辅助
    void SetSelection(int componentIndex, bool add);
    void SetWireSelection(int wireIndex, bool add);
    void SetTextSelection(int textIndex, bool add);
    void ClearSelectionInternal();

    // 编辑
    void PlaceComponent(const wxPoint& world);
    void HandleWireClick(const wxPoint& world);
    void AddWire(const wxPoint& a, const wxPoint& b);
    void BeginDrag(const wxPoint& world);
    void UpdateDrag(const wxPoint& world);
    void EndDrag();
    void BeginBandSelect(const wxPoint& world);
    void UpdateBandSelect(const wxPoint& world);
    void EndBandSelect();
    void Touch();

    // 撤销
    void PushUndo(const wxString& label);
    void ApplySnapshot(const Snapshot& snapshot);

    // 绘制
    void DrawBackground(wxGraphicsContext* gc);
    void DrawGrid(wxGraphicsContext* gc);
    void DrawWires(wxGraphicsContext* gc);
    void DrawComponents(wxGraphicsContext* gc);
    void DrawTexts(wxGraphicsContext* gc);
    void DrawSimValues(wxGraphicsContext* gc);      // 仿真：在端口旁显示逻辑值
    void DrawSelectionBand(wxGraphicsContext* gc);
    void DrawGhost(wxGraphicsContext* gc);
    void DrawWirePreview(wxGraphicsContext* gc);
    void DrawSnapMarker(wxGraphicsContext* gc);
    void DrawCrosshair(wxGraphicsContext* gc);

    void SetZoomAt(double newZoom, const wxPoint2DDouble& screenAnchor);
    void EnsureOriginInitialised();
    void NotifyViewChanged();
    void NotifySelectionChanged();
    void UpdateHoverNet(const wxPoint& world);
    int  NetOfComponent(int compIndex);

    // 内容包围盒（元件 + 导线），空文档返回 false
    bool ComputeContentBounds(wxRect& bbox) const;
    void CenterOnContent();

    // ---- 文档修改状态 --------------------------------------
    bool m_modified = false;

    // ---- 视图状态 ------------------------------------------
    double          m_zoom   = 1.0;
    wxPoint2DDouble m_origin = wxPoint2DDouble(0, 0);
    bool            m_originInit = false;
    wxSize          m_lastSize   = wxSize(0, 0);
    int             m_gridStep   = 10;

    bool m_gridVisible = true;
    bool m_snapEnabled = true;
    bool m_crosshair   = false;

    Tool                m_tool    = ToolSelect;
    const ComponentDef* m_pending = nullptr;

    // ---- 文档内容 ------------------------------------------
    std::vector<ComponentInstance> m_components;
    std::vector<Wire>              m_wires;
    std::vector<TextItem>          m_texts;
    int                            m_nextComponentId = 0;
    int                            m_nextWireId      = 0;
    std::map<wxString, int>        m_refCount;

    // ---- 选中状态（多选，存下标，升序）---------------------
    std::vector<int> m_selComponents;
    std::vector<int> m_selWires;
    std::vector<int> m_selTexts;

    // ---- 拖动移动 ------------------------------------------
    //  除了移动元件本身，接在它引脚上的导线端点也要跟着走（橡皮筋连线），
    //  否则一拖动网络就断了。
    struct DragWireEnd
    {
        int     wireIndex = -1;
        int     end       = 0;      // 0 = a 端，1 = b 端
        wxPoint original;           // 拖动前的端点位置（贴着引脚）

        // ---- 为了让拐角始终保持 90°，光跟着走还不够 ----
        //  attached: pin ---W---> Q ---W2---> F
        //  拖动 pin 之后不能简单平移 Q，否则 W 和 W2 里总有一条会变斜。
        //  正确做法是让 Q（拐角）沿着一个轴跟着 pin 走：
        //     W 原本是横的 → Q = (F.x, 新 pin.y)
        //     W 原本是竖的 → Q = (新 pin.x, F.y)
        //  这样 W 和 W2 都还是正交的。
        wxPoint fixedOther;         // 本条导线另一端（拖动前）的位置 = 拐角
        bool    horizontalFirst = false; // 本条导线（接引脚的那条）原本是不是横的
        int     partnerIndex = -1;  // 拐角上接着的另一条导线（-1 = 没有，拖动时要现拆一段）
        int     partnerEnd   = 0;   // 那条导线的哪一端在拐角上
        wxPoint partnerFar;         // 那条导线另一端的固定点 F
        bool    partnerVertical = false; // 那条导线原本是不是竖的（决定拐角往哪个方向让）
    };

    bool                 m_dragging   = false;
    bool                 m_dragMoved  = false;
    wxPoint              m_dragStartWorld;
    std::vector<wxPoint>     m_dragOriginalPos;   // 与 m_selComponents 平行
    std::vector<DragWireEnd> m_dragWireEnds;
    std::vector<int>         m_dragSplitWires;    // 拖动时为了保直角临时拆出来的线段

    // ---- 框选（橡皮筋）-------------------------------------
    bool    m_bandSelecting = false;
    bool    m_bandAdditive  = false;
    wxPoint m_bandStart;
    wxPoint m_bandEnd;

    // ---- 撤销 / 重做 ---------------------------------------
    std::vector<Snapshot> m_undoStack;
    std::vector<Snapshot> m_redoStack;

    // ---- 导线绘制状态 --------------------------------------
    bool    m_wireDrawing = false;
    wxPoint m_wireAnchor;       // 当前段起点（世界坐标）
    wxPoint m_wireCursor;       // 吸附后的光标位置

    // ---- 吸附提示（靠近引脚 / 导线端点时给明显的视觉反馈）----
    bool     m_snapActive = false;
    int      m_snapKind   = 0;     // 1 = 引脚，2 = 导线端点
    wxPoint  m_snapPoint;          // 吸附到的世界坐标
    wxString m_snapLabel;          // 例如 "引脚 U1-Y"

    // ---- 连通性缓存 ----------------------------------------
    NetGraph m_graph;
    bool     m_netsDirty   = true;
    int      m_highlightNet = -1;

    // ---- 逻辑仿真（任务 6）---------------------------------
    Simulator m_sim;

    // ---- 交互状态 ------------------------------------------
    bool            m_panning = false;
    wxPoint         m_panStartScreen;
    wxPoint2DDouble m_panStartOrigin;
    wxPoint         m_cursorWorld;
    int             m_cursorScreenX = -1;
    int             m_cursorScreenY = -1;
    bool            m_hasCursor = false;
};

} // namespace se

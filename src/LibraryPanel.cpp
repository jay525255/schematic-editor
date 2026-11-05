#include "LibraryPanel.h"

#include <wx/artprov.h>
#include <wx/dcbuffer.h>
#include <wx/statline.h>

#include <algorithm>
#include <memory>
#include <vector>

#include "Ids.h"
#include "SymbolPainter.h"

namespace se
{

namespace
{

// 元件图标的像素尺寸（Logisim 的调色板图标大致就是这个量级）
constexpr int kIconSize = 18;

// ------------------------------------------------------------
// 给树节点挂的数据：指向元件定义（生命周期 = 元件库缓存）
// ------------------------------------------------------------
class ComponentItemData : public wxTreeItemData
{
public:
    explicit ComponentItemData(const ComponentDef* def) : m_def(def) {}

    const ComponentDef* Get() const { return m_def; }

private:
    const ComponentDef* m_def;
};

const wxColour kUserColour(235, 145, 25);   // 自定义元件用橙色标出来（树图标描边 + 预览角标）

// ------------------------------------------------------------
// 由元件定义造一个"临时实例"，好让符号绘制器把预览画得和画布上一模一样
// ------------------------------------------------------------
ComponentInstance MakePreviewInstance(const ComponentDef& def)
{
    ComponentInstance inst;

    inst.name    = def.name;
    inst.refdes  = def.refPrefix.IsEmpty() ? wxString(L"U1")
                                          : def.refPrefix + L"1";
    inst.pos     = wxPoint(0, 0);
    inst.w       = def.w;
    inst.h       = def.h;
    inst.footprint = def.footprint;
    inst.value     = def.value;

    inst.pins = MakePins(def.name, def.kind, def.inputs, def.outputs);

    return inst;
}

// ------------------------------------------------------------
// 树图标（对齐 Logisim 调色板图标的观感）：
//   纯黑粗线条、不加颜色区分、形状尽量铺满图标格；
//   反相小圈、异或门那条弧在图标尺寸下也要看得清。
//   不画引脚——18 像素里画了引脚就糊成一团，靠轮廓辨认足够。
// ------------------------------------------------------------
wxBitmap MakeTreeIcon(const ComponentDef& def, int size)
{
    wxBitmap bitmap(size, size);

    {
        wxMemoryDC dc(bitmap);
        dc.SetBackground(*wxWHITE_BRUSH);
        dc.Clear();

        const ComponentInstance inst = MakePreviewInstance(def);

        // 留白只给半个像素，让形状尽量大
        const double margin = 0.6;
        const double scale  = std::min((size - 2 * margin) / std::max(1, inst.w),
                                       (size - 2 * margin) / std::max(1, inst.h));

        const double usedW = inst.w * scale;
        const double usedH = inst.h * scale;

        const SymbolPlacement place {
            wxPoint2DDouble((size - usedW) / 2, (size - usedH) / 2),
            scale
        };

        std::unique_ptr<wxGraphicsContext> gc(wxGraphicsContext::Create(dc));

        if (gc)
        {
            // Logisim 的图标是硬边像素（Java 那边画小图不做抗锯齿），
            // 这里也关掉：18 像素的小图开抗锯齿反而会发灰、发虚。
            gc->SetAntialiasMode(wxANTIALIAS_NONE);

            const wxColour line = def.userDefined ? kUserColour : wxColour(0, 0, 0);

            // penWidth 会乘上 scale，给个大数保证折到 1 像素以上
            PaintSymbolBody(gc.get(), inst, place, line, *wxWHITE, 4.6);

            gc.reset();
        }

        dc.SelectObject(wxNullBitmap);
    }

    return bitmap;
}



} // namespace

// ============================================================
//  符号预览（面板底部）
// ============================================================
class SymbolPreview : public wxPanel
{
public:
    explicit SymbolPreview(wxWindow* parent)
        : wxPanel(parent, wxID_ANY)
    {
        SetBackgroundColour(*wxWHITE);

        // wxAutoBufferedPaintDC 要求窗口声明“自己画背景”，
        // 否则 Debug 版会断言 wxBG_STYLE_PAINT（Release 下静默通过）
        SetBackgroundStyle(wxBG_STYLE_PAINT);

        // 高度必须给足：下面要画“名称 + 符号 + 两行说明”
        SetMinSize(wxSize(150, 152));

        Bind(wxEVT_PAINT, &SymbolPreview::OnPaint, this);
    }

    void SetDef(const ComponentDef* def)
    {
        m_def = def;
        Refresh();
    }

private:
    void OnPaint(wxPaintEvent&);

    const ComponentDef* m_def = nullptr;
};

void SymbolPreview::OnPaint(wxPaintEvent&)
{
    wxAutoBufferedPaintDC dc(this);
    dc.SetBackground(*wxWHITE_BRUSH);
    dc.Clear();

    const wxSize size = GetClientSize();

    dc.SetPen(wxPen(wxColour(226, 230, 238), 1));
    dc.SetBrush(*wxTRANSPARENT_BRUSH);
    dc.DrawRectangle(0, 0, size.x, size.y);

    if (!m_def)
    {
        dc.SetTextForeground(wxColour(150, 155, 165));
        dc.SetFont(wxFont(wxFontInfo(8)));

        const wxString hint = L"选中一个元件，这里显示符号";
        const wxSize   ext  = dc.GetTextExtent(hint);

        dc.DrawText(hint, (size.x - ext.x) / 2, (size.y - ext.y) / 2);
        return;
    }

    const ComponentDef& def = *m_def;

    // ---- 标题行：名称 + （自定义）----
    wxFont nameFont(wxFontInfo(10).Bold());

    dc.SetFont(nameFont);
    dc.SetTextForeground(wxColour(35, 55, 95));
    dc.DrawText(def.name, 8, 5);

    if (def.userDefined)
    {
        const wxSize nameExt = dc.GetTextExtent(def.name);

        dc.SetFont(wxFont(wxFontInfo(7)));
        dc.SetTextForeground(kUserColour);
        dc.DrawText(L"自定义", 8 + nameExt.x + 6, 9);
    }

    // ---- 符号：和画布上用同一个绘制器 ----
    const ComponentInstance inst = MakePreviewInstance(def);

    const double areaTop = 26;
    const double areaH   = size.y - areaTop - 34;
    const double areaW   = size.x - 16;

    // 引脚会伸到框外，所以先估一个留白
    const double pinAllow = 14.0;

    const double scale = std::min((areaW - 2 * pinAllow) / std::max(1, inst.w),
                                  (areaH - 2 * pinAllow) / std::max(1, inst.h));

    const double usedW = inst.w * scale;
    const double usedH = inst.h * scale;

    const SymbolPlacement place { wxPoint2DDouble((size.x - usedW) / 2,
                                                 areaTop + (areaH - usedH) / 2),
                                 scale };

    {
        std::unique_ptr<wxGraphicsContext> gc(wxGraphicsContext::Create(dc));

        if (gc)
        {
            PaintSymbolPins(gc.get(), inst, place, wxColour(90, 100, 120), 1.2, false);
            PaintSymbolBody(gc.get(), inst, place, wxColour(52, 62, 84),
                            wxColour(255, 252, 235), 1.6);

            gc.reset();
        }
    }

    // ---- 底部说明 ----
    dc.SetFont(wxFont(wxFontInfo(8)));
    dc.SetTextForeground(wxColour(105, 112, 125));

    const wxString info = wxString::Format(L"%s · %d 入 / %d 出 · 封装 %s",
                                           ComponentCatalog::KindToChinese(def.kind),
                                           def.inputs,
                                           def.outputs,
                                           def.footprint.IsEmpty() ? wxString(L"无（非物理）")
                                                                   : def.footprint);

    dc.DrawText(info, 8, size.y - 28);

    if (!def.description.IsEmpty())
    {
        dc.SetTextForeground(wxColour(140, 145, 155));

        wxString desc = def.description;
        const int maxWidth = size.x - 16;

        while (!desc.IsEmpty() && dc.GetTextExtent(desc).x > maxWidth)
            desc.RemoveLast();

        dc.DrawText(desc, 8, size.y - 14);
    }
}

// ============================================================
//  元件库面板
// ============================================================

LibraryPanel::LibraryPanel(wxWindow* parent)
    : wxPanel(parent, wxID_ANY)
{
    SetBackgroundColour(wxColour(248, 249, 251));

    // ---- 标题 ----
    wxStaticText* title = new wxStaticText(this, wxID_ANY, L"元件库");
    wxFont titleFont = title->GetFont();
    titleFont.SetPointSize(titleFont.GetPointSize() + 1);
    titleFont.SetWeight(wxFONTWEIGHT_BOLD);
    title->SetFont(titleFont);
    title->SetForegroundColour(wxColour(40, 60, 100));

    m_subtitle = new wxStaticText(this, wxID_ANY, wxEmptyString);
    m_subtitle->SetForegroundColour(wxColour(130, 135, 145));

    // ---- 搜索框 ----
    m_search = new wxSearchCtrl(this, wxID_ANY, wxEmptyString, wxDefaultPosition,
                                wxDefaultSize, wxTE_PROCESS_ENTER);
    m_search->SetDescriptiveText(L"搜索元件（名称 / 说明 / 型号）");
    m_search->ShowSearchButton(false);
    m_search->ShowCancelButton(true);

    wxStaticLine* line = new wxStaticLine(this);

    // ---- 树形控件 ----
    m_tree = new wxTreeCtrl(this,
                            ID_LIBRARY_TREE,
                            wxDefaultPosition,
                            wxDefaultSize,
                            wxTR_HAS_BUTTONS | wxTR_LINES_AT_ROOT |
                            wxTR_SINGLE | wxTR_HIDE_ROOT | wxBORDER_NONE);

    m_tree->SetBackgroundColour(wxColour(255, 255, 255));

    // 图标全部是运行时画出来的（不依赖任何图片资源文件）
    m_images = new wxImageList(kIconSize, kIconSize, true);
    m_images->Add(wxArtProvider::GetBitmap(wxART_FOLDER, wxART_LIST, wxSize(kIconSize, kIconSize)));   // 0 = 分类
    m_tree->AssignImageList(m_images);   // Assign 由控件负责释放

    // ---- 符号预览 ----
    m_preview = new SymbolPreview(this);

    // ---- 布局 ----
    wxBoxSizer* sizer = new wxBoxSizer(wxVERTICAL);
    sizer->Add(title,     0, wxLEFT | wxRIGHT | wxTOP, 8);
    sizer->Add(m_subtitle, 0, wxLEFT | wxRIGHT | wxTOP, 8);
    sizer->Add(m_search,  0, wxEXPAND | wxLEFT | wxRIGHT | wxTOP, 8);
    sizer->Add(line,      0, wxEXPAND | wxTOP, 6);
    sizer->Add(m_tree,    1, wxEXPAND | wxALL, 2);
    sizer->Add(new wxStaticLine(this), 0, wxEXPAND | wxTOP, 2);
    sizer->Add(m_preview, 0, wxEXPAND | wxALL, 2);

    SetSizer(sizer);

    // ---- 事件 ----
    m_tree->Bind(wxEVT_TREE_SEL_CHANGED,    &LibraryPanel::OnSelectionChanged, this);
    m_tree->Bind(wxEVT_TREE_ITEM_ACTIVATED, &LibraryPanel::OnItemActivated,   this);
    m_search->Bind(wxEVT_TEXT,              &LibraryPanel::OnSearchChanged,   this);
    m_search->Bind(wxEVT_SEARCHCTRL_CANCEL_BTN, &LibraryPanel::OnSearchCancel, this);
    m_search->Bind(wxEVT_SEARCHCTRL_SEARCH_BTN, &LibraryPanel::OnSearchChanged, this);

    BuildTree();
    SetMinSize(wxSize(198, 320));
}

// ------------------------------------------------------------
// 图标：按“形态 + 是否自定义”缓存一张，避免每个元件都画一遍
// ------------------------------------------------------------
int LibraryPanel::IconFor(const ComponentDef& def)
{
    // 缓存键必须带上元件名：光用 kind 的话，所有逻辑门都是 Logic，
    // 会共用同一张图标（曾经真的这么错过，结果每个门都长成与门）。
    const wxString key = wxString::Format(L"%s|%d",
                                          def.name,
                                          def.userDefined ? 1 : 0);

    const auto it = m_iconCache.find(key);

    if (it != m_iconCache.end())
        return it->second;

    const int index = m_images->Add(MakeTreeIcon(def, kIconSize));
    m_iconCache[key] = index;

    return index;
}

// ------------------------------------------------------------
void LibraryPanel::BuildTree()
{
    m_rebuilding = true;

    m_tree->DeleteAllItems();

    const wxTreeItemId root = m_tree->AddRoot(L"元件库");   // wxTR_HIDE_ROOT：根不显示

    const wxString filter = m_search ? m_search->GetValue().Trim() : wxString();
    const wxString needle = filter.Lower();

    m_visible = 0;

    for (const CategoryDef& category : ComponentCatalog::Categories())
    {
        // 分类名本身命中 → 整个分类都显示
        const bool categoryHit = !needle.IsEmpty() &&
                                 category.name.Lower().Contains(needle);

        std::vector<const ComponentDef*> hits;

        for (const ComponentDef& item : category.items)
        {
            if (needle.IsEmpty() || categoryHit)
            {
                hits.push_back(&item);
                continue;
            }

            const wxString haystack =
                (item.name + L" " + item.description + L" " + item.value + L" " +
                 item.footprint).Lower();

            if (haystack.Contains(needle))
                hits.push_back(&item);
        }

        if (hits.empty())
            continue;

        wxString label = category.name;

        if (!needle.IsEmpty())
            label += wxString::Format(L"（%d）", static_cast<int>(hits.size()));

        const wxTreeItemId categoryId = m_tree->AppendItem(root, label, 0);

        for (const ComponentDef* item : hits)
        {
            wxString itemLabel = item->name;

            if (item->userDefined)
                itemLabel += L"  ★";   // ★ = 来自 components.xml

            m_tree->AppendItem(categoryId,
                               itemLabel,
                               IconFor(*item),
                               -1,
                               new ComponentItemData(item));

            ++m_visible;
        }

        m_tree->Expand(categoryId);
    }

    if (m_visible == 0 && !needle.IsEmpty())
    {
        m_tree->AppendItem(root, wxString::Format(L"没有匹配「%s」的元件", filter));
    }

    m_rebuilding = false;

    UpdateHeader();
}

// ------------------------------------------------------------
void LibraryPanel::ApplyFilter()
{
    // 过滤后原来选中的元件可能已经不在树里了，一律重新开始
    m_selected = nullptr;
    m_preview->SetDef(nullptr);

    BuildTree();

    if (m_onSelect)
        m_onSelect(nullptr);
}

// ------------------------------------------------------------
void LibraryPanel::UpdateHeader()
{
    if (!m_subtitle)
        return;

    const int total = ComponentCatalog::ComponentCount();
    const int mine  = ComponentCatalog::UserComponentCount();

    const wxString filter = m_search ? m_search->GetValue().Trim() : wxString();

    wxString text;

    if (filter.IsEmpty())
    {
        text = wxString::Format(L"共 %d 个元件", total);

        if (mine > 0)
            text += wxString::Format(L"（含 %d 个自定义 ★）", mine);
    }
    else
    {
        text = wxString::Format(L"匹配 %d / %d 个元件", m_visible, total);
    }

    m_subtitle->SetLabel(text);
    m_subtitle->GetParent()->Layout();
}

// ------------------------------------------------------------
void LibraryPanel::Reload()
{
    // 元件库数据换了 → 指针全部失效，先彻底清空选择
    m_selected = nullptr;

    if (m_preview)
        m_preview->SetDef(nullptr);

    m_iconCache.clear();

    if (m_images)
    {
        // 清空后重新放回 0 号“分类”图标，再按需重画元件图标
        m_images->RemoveAll();
        m_images->Add(wxArtProvider::GetBitmap(wxART_FOLDER, wxART_LIST, wxSize(kIconSize, kIconSize)));
    }

    BuildTree();

    if (m_onSelect)
        m_onSelect(nullptr);
}

// ------------------------------------------------------------
void LibraryPanel::OnSelectionChanged(wxTreeEvent& event)
{
    if (m_rebuilding)
        return;

    const wxTreeItemId item = event.GetItem();

    m_selected = nullptr;

    if (item.IsOk())
    {
        if (ComponentItemData* data =
                static_cast<ComponentItemData*>(m_tree->GetItemData(item)))
        {
            m_selected = data->Get();
        }
    }

    UpdatePreview();

    if (m_onSelect)
        m_onSelect(m_selected);
}

// ------------------------------------------------------------
// 双击元件 = 与单击选中同效
// ------------------------------------------------------------
void LibraryPanel::OnItemActivated(wxTreeEvent& event)
{
    if (ComponentItemData* data =
            static_cast<ComponentItemData*>(m_tree->GetItemData(event.GetItem())))
    {
        m_selected = data->Get();

        UpdatePreview();

        if (m_onSelect)
            m_onSelect(m_selected);
    }
}

// ------------------------------------------------------------
void LibraryPanel::OnSearchChanged(wxCommandEvent& event)
{
    ApplyFilter();
    event.Skip();
}

// ------------------------------------------------------------
void LibraryPanel::OnSearchCancel(wxCommandEvent& event)
{
    m_search->ChangeValue(wxEmptyString);
    ApplyFilter();
    event.Skip();
}

// ------------------------------------------------------------
void LibraryPanel::UpdatePreview()
{
    if (m_preview)
        m_preview->SetDef(m_selected);
}

// ------------------------------------------------------------
void LibraryPanel::ClearSelection()
{
    m_selected = nullptr;

    m_tree->UnselectAll();

    UpdatePreview();

    if (m_onSelect)
        m_onSelect(nullptr);
}

} // namespace se







#pragma once

#include <wx/string.h>

#include <vector>

#include "NetModel.h"
#include "SchematicIO.h"

namespace se
{

// ------------------------------------------------------------
// 通用网表导入（任务 5 的"逆向"：把别人的网表读回成原理图）
//
// 这里只做两件事：认识两种文本网表格式，然后把"元件 + 网络"
// 还原成可以继续编辑的图元。
//
//   1. KiCad 原生网表   (export (version ...) (components ...) (nets ...))
//   2. Protel / Altium 网表    [位号 / 封装 / 值]  +  (网络名 / 位号-引脚 ...)
//
// 网表里没有坐标、没有符号形状，所以布局只能"造"出来：
// 元件排成网格，每个网络在下方占一条横向"车道"，引脚用竖线引到车道上。
// 参阅 docs/netlist-import.md 了解为什么这样布线不会误连。
// ------------------------------------------------------------

enum class NetlistFormat
{
    Unknown,
    KiCad,
    Protel
};

NetlistFormat DetectNetlistFormat(const wxString& text);

struct NetlistImportResult
{
    SchematicDocument     document;
    std::vector<wxString> warnings;
    int                   componentCount = 0;
    int                   netCount       = 0;
    int                   wireCount      = 0;
};

bool ImportNetlistText(const wxString& text, NetlistImportResult& result, wxString& error);

bool ImportNetlistFile(const wxString& path, NetlistImportResult& result, wxString& error);

} // namespace se
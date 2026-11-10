// ============================================================
//  SchematicIO.h  —— 原理图工程文件读写（任务 5）
//
//  文件格式：**XML**，扩展名 .ses（同时兼容 .xml）
//
//  为什么选 XML 而不是自定义文本格式 / JSON / 二进制：
//    * wxWidgets 内置 wxXmlDocument（wxUSE_XML=1，已自动链接 wxbase32u_xml），
//      不必引入任何第三方库，解析健壮；
//    * 自描述、可读、可手改，便于答辩演示与排错；
//    * 有正式的转义/编码规则，中文与特殊字符不会踩坑；
//    * 二进制格式体积略小但要自己写序列化且不便于调试；
//      JSON 需要额外依赖或手写解析器。
//
//  .ses 文件结构（version 1.0）：
//
//    <schematic version="1.0" generator="SchematicEditor 1.0">
//      <view gridStep="10" gridVisible="1" snapEnabled="1" zoom="1.0000"/>
//      <components>
//        <component id="1" name="与门" refdes="U1" x="-280" y="-20" w="60" h="40">
//          <pin name="A" dir="input" side="left" slot="0" count="2"/>
//          ...
//        </component>
//      </components>
//      <wires>
//        <wire id="1" x1="-210" y1="0" x2="210" y2="0"/>
//      </wires>
//    </schematic>
//
//  注意：引脚（pin）也写进文件，而不是只存元件名。
//  这样文件是自包含的 —— 将来元件库升级甚至支持“用户自定义元件”后，
//  旧文件依然能按当时的引脚定义正确还原。
// ============================================================

#pragma once

#include <wx/string.h>

#include <vector>

#include "NetModel.h"

namespace se
{

// ------------------------------------------------------------
// 一份完整的原理图文档
// ------------------------------------------------------------
struct SchematicDocument
{
    std::vector<ComponentInstance> components;
    std::vector<Wire>              wires;
    std::vector<TextItem>          texts;

    // 视图设置（跟着文件走，重新打开时恢复现场）
    int    gridStep    = 10;
    bool   gridVisible = true;
    bool   snapEnabled = true;
    double zoom        = 1.0;

    int PinCount() const
    {
        int n = 0;

        for (const ComponentInstance& c : components)
            n += static_cast<int>(c.pins.size());

        return n;
    }
};

// ------------------------------------------------------------
// 读写器
// ------------------------------------------------------------
class SchematicIO
{
public:
    static wxString SchematicWildcard();
    static wxString NetlistWildcard();

    static bool Validate(const SchematicDocument& doc, wxString* error = nullptr);

    // 保存 / 加载 .ses
    static bool Save(const wxString& path, const SchematicDocument& doc, wxString* error = nullptr);
    static bool Load(const wxString& path, SchematicDocument& doc, wxString* error = nullptr);

    // 纯文本写出（UTF-8，不带 BOM），用于导出网表 / ERC 报告
    static bool WriteTextFile(const wxString& path, const wxString& text, wxString* error = nullptr);
};

} // namespace se

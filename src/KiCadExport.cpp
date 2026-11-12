#include "KiCadExport.h"
#include <set>
namespace se {
namespace {
wxString Quoted(const wxString& raw){wxString s=raw;s.Replace("\\","\\\\");s.Replace("\"","\\\"");s.Replace("\n","\\n");s.Replace("\r","\\r");s.Replace("\t","\\t");return "\""+s+"\"";}
bool Identifier(const wxString& s){if(s.empty())return false;for(auto c:s)if(!((c>='A'&&c<='Z')||(c>='a'&&c<='z')||(c>='0'&&c<='9')||c=='_'))return false;return (s[0]>='A'&&s[0]<='Z')||(s[0]>='a'&&s[0]<='z');}
}
wxString PcbPinNumber(const ComponentInstance& c,size_t pin){if (!c.pins[pin].pad.empty()) return c.pins[pin].pad; return wxString::Format("%d",c.pins[pin].number > 0 ? c.pins[pin].number : static_cast<int>(pin)+1);}
wxString DefaultFootprint(size_t n){if(n<1||n>40)return {};return wxString::Format("Connector_PinHeader_2.54mm:PinHeader_1x%02zu_P2.54mm_Vertical",n);}
bool IsPcbComponent(const ComponentInstance& c){return !c.pcbExcluded&&!c.pins.empty()&&c.name!=L"文本"&&c.name!=L"连接点"&&c.name!=L"未连接标记"&&c.name!=L"网络标签"&&c.name!=L"全局标签"&&c.name!=L"GND"&&c.name!=L"VCC";}

// 老文件（以及手搭电路）里存的是简写封装名：DIP-14 / 0805 / HDR-1X1……
// 这些名字进不了 KiCad——它要的是 库名:封装名。这里做一次查表补全，
// 省得用户挨个去属性表里手改；查不到的仍然原样返回，由调用方报错。
// 0805/0603 这类"同一外形、多种器件"的，只能靠元件名区分（电阻/电容/电感/LED）。
wxString ResolveFootprint(const ComponentInstance& c){
    wxString fp=c.footprint;
    if(fp.Contains(":"))return fp;                       // 已经是 库:封装
    fp.Trim(true).Trim(false);
    if(fp.IsEmpty())return c.footprint;
    struct Entry{const wchar_t* shortName;const wchar_t* fullName;};
    static const Entry kNamed[]={
        {L"DIP-14",L"Package_DIP:DIP-14_W7.62mm"},
        {L"DIP-16",L"Package_DIP:DIP-16_W7.62mm"},
        {L"DIP-24",L"Package_DIP:DIP-24_W15.24mm"},
        {L"SOIC-8",L"Package_SO:SOIC-8_3.9x4.9mm_P1.27mm"},
        {L"TO-92", L"Package_TO_SOT_THT:TO-92_Inline"},
        {L"SOD-123",L"Diode_SMD:D_SOD-123"},
        {L"HDR-1X1",L"Connector_PinHeader_2.54mm:PinHeader_1x01_P2.54mm_Vertical"},
        {L"HDR-1X3",L"Connector_PinHeader_2.54mm:PinHeader_1x03_P2.54mm_Vertical"},
        {L"TP-1X1",L"TestPoint:TestPoint_Pad_D1.5mm"},
    };
    for(const auto& e:kNamed)if(fp==e.shortName)return e.fullName;
    if(fp==L"0805"){
        if(c.name==L"电容")return L"Capacitor_SMD:C_0805_2012Metric";
        if(c.name==L"电感")return L"Inductor_SMD:L_0805_2012Metric";
        if(c.name.Contains(L"LED"))return L"LED_SMD:LED_0805_2012Metric";
        return L"Resistor_SMD:R_0805_2012Metric";
    }
    if(fp==L"0603"){
        if(c.name==L"电容")return L"Capacitor_SMD:C_0603_1608Metric";
        if(c.name==L"电感")return L"Inductor_SMD:L_0603_1608Metric";
        return L"Resistor_SMD:R_0603_1608Metric";
    }
    return c.footprint;
}
bool BuildKiCadNetlist(const std::vector<ComponentInstance>& cs,const NetGraph& graph,wxString& text,wxString& error){
    text.clear();error.clear();std::set<wxString> refs,netnames;int physical=0;
    if(graph.pinNet.size()!=cs.size()){error=L"网络与元件映射不匹配";return false;}
    for(size_t i=0;i<cs.size();++i){const auto& c=cs[i];
        if(graph.pinNet[i].size()!=c.pins.size()){error=L"引脚映射不完整";return false;}
        for(int net:graph.pinNet[i])if(net<0||net>=static_cast<int>(graph.nets.size())){error=L"非法网络下标";return false;}
        if(!IsPcbComponent(c))continue;++physical;
        if(!Identifier(c.refdes)||!refs.insert(c.refdes).second){error=L"PCB位号非法或重复："+c.refdes;return false;}
        if(c.footprint.empty()||!ResolveFootprint(c).Contains(":")||ResolveFootprint(c).BeforeFirst(':').empty()||ResolveFootprint(c).AfterFirst(':').empty()||c.footprint.Contains("\n")||c.footprint.Contains("\r")){
            error=L"请为"+c.refdes+L"指定KiCad封装 Library:Footprint（在属性面板编辑）";return false;
        }
        std::set<wxString> pads;
        for(size_t k=0;k<c.pins.size();++k){const auto n=PcbPinNumber(c,k);if(!Identifier("P"+n)||!pads.insert(n).second){error=L"引脚编号为空、重复或包含非法字符："+c.refdes;return false;}}
    }
    if(!physical){error=L"没有可导出的PCB元件";return false;}
    for(const auto& net:graph.nets)if(net.name.empty()||!netnames.insert(net.name).second){error=L"网络名为空或重复："+net.name;return false;}
    wxString out="(export (version E)\n  (design (source \"SchematicEditor\") (tool \"SchematicEditor 2.0\"))\n  (components\n";
    for(const auto& c:cs)if(IsPcbComponent(c)){
        out+="    (comp (ref "+Quoted(c.refdes)+") (value "+Quoted(c.value.empty()?c.name:c.value)+")\n";
        out+="      (footprint "+Quoted(ResolveFootprint(c))+") (libsource (lib \"SchematicEditor\") (part "+Quoted(c.name)+"))\n";
        out+="      (sheetpath (names \"/\") (tstamps \"/00000000-0000-4000-8000-ffffffffffff/\"))\n";
        out+="      (tstamps "+Quoted(wxString::Format("00000000-0000-4000-8000-%012llx",static_cast<unsigned long long>(c.id)))+"))\n";
    }
    out+="  )\n  (nets\n";int code=0;
    for(size_t ni=0;ni<graph.nets.size();++ni){wxString nodes;
        for(size_t ci=0;ci<cs.size();++ci)if(IsPcbComponent(cs[ci]))
            for(size_t pi=0;pi<cs[ci].pins.size();++pi)if(graph.pinNet[ci][pi]==static_cast<int>(ni))
                nodes+="      (node (ref "+Quoted(cs[ci].refdes)+") (pin "+Quoted(PcbPinNumber(cs[ci],pi))+"))\n";
        if(nodes.empty())continue;
        out+="    (net (code "+wxString::Format("%d",++code)+") (name "+Quoted(graph.nets[ni].name)+")\n"+nodes+"    )\n";
    }
    text=out+"  )\n)\n";return true;
}
}

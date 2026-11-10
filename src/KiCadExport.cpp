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
bool BuildKiCadNetlist(const std::vector<ComponentInstance>& cs,const NetGraph& graph,wxString& text,wxString& error){
    text.clear();error.clear();std::set<wxString> refs,netnames;int physical=0;
    if(graph.pinNet.size()!=cs.size()){error=L"网络与元件映射不匹配";return false;}
    for(size_t i=0;i<cs.size();++i){const auto& c=cs[i];
        if(graph.pinNet[i].size()!=c.pins.size()){error=L"引脚映射不完整";return false;}
        for(int net:graph.pinNet[i])if(net<0||net>=static_cast<int>(graph.nets.size())){error=L"非法网络下标";return false;}
        if(!IsPcbComponent(c))continue;++physical;
        if(!Identifier(c.refdes)||!refs.insert(c.refdes).second){error=L"PCB位号非法或重复："+c.refdes;return false;}
        if(c.footprint.empty()||!c.footprint.Contains(":")||c.footprint.BeforeFirst(':').empty()||c.footprint.AfterFirst(':').empty()||c.footprint.Contains("\n")||c.footprint.Contains("\r")){
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
        out+="      (footprint "+Quoted(c.footprint)+") (libsource (lib \"SchematicEditor\") (part "+Quoted(c.name)+"))\n";
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

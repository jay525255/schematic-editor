#include "TestSupport.h"
#include "KiCadImport.h"
#include "KiCadExport.h"
#include "EditingTools.h"
#include <wx/file.h>
#include <clocale>
#include <set>
#include <map>
namespace {
using Partition=std::set<std::set<wxString>>;
Partition topology(const se::SchematicDocument& d) {
    auto graph=se::NetBuilder::Build(d.components,d.wires);Partition result;
    for(const auto& net:graph.nets){std::set<wxString> nodes;for(const auto& p:net.pins)nodes.insert(d.components[p.compIndex].refdes+"."+se::PcbPinNumber(d.components[p.compIndex],p.pinIndex));result.insert(nodes);}return result;
}
se::SchematicDocument load(const wxString& file) {se::KiCadImportResult r;wxString error;bool ok=se::ImportKiCadSchematic(file,r,error);if(!ok)std::cerr<<error.utf8_string()<<'\n';require(ok,"import actual upstream KiCad schematic");return r.document;}
}
int main(int argc,char**argv){try{
    std::setlocale(LC_ALL,"");require(argc==3,"fixture and output arguments");initModelTests(argv[2]);wxString fixtures=wxString::FromUTF8(argv[1]),out=wxString::FromUTF8(argv[2]),error;
    for(const char* name:{"rectifier-9","sallen-key-8","antenna-9","mirror-9","pin-to-pin-8"}){
        auto d=load(fixtures+"/"+name+".kicad_sch");require(!d.components.empty(),"real components imported");
        auto original=topology(d);auto saved=out+"/"+name+".ses";
        require(se::SchematicIO::Save(saved,d,&error),"save imported pins, graphics and properties");se::SchematicDocument reopened;
        require(se::SchematicIO::Load(saved,reopened,&error),"reopen imported .ses");require(topology(reopened)==original,"roundtrip retains complete terminal partition");
        require(d.components.size()==reopened.components.size()&&d.wires.size()==reopened.wires.size(),"roundtrip retains all objects");
        for(size_t i=0;i<d.components.size();++i){const auto& a=d.components[i];const auto& b=reopened.components[i];require(a.graphics.size()==b.graphics.size()&&a.properties==b.properties&&a.netLabel==b.netLabel,"symbol geometry, parameters and power names retained");
            for(size_t j=0;j<a.graphics.size();++j)require(a.graphics[j].points==b.graphics[j].points&&a.graphics[j].text==b.graphics[j].text&&a.graphics[j].property==b.graphics[j].property,"all symbol paths and text survive save");
            for(size_t j=0;j<a.pins.size();++j)require(se::PinPosition(a,j)==se::PinPosition(b,j)&&a.pins[j].pad==b.pins[j].pad,"exact pad coordinates and numbers survive save");}
        // A group move uses the production rerouter and retains every named network.
        std::vector<int> selected;for(size_t i=0;i<d.components.size();++i)selected.push_back(static_cast<int>(i));
        require(se::MoveConnectedComponents(d.components,d.wires,selected,{20,20}),"imported objects support connected movement");require(topology(d)==original,"move preserves all electrical connectivity");
        for(auto& c:d.components){auto before=c;for(int n=0;n<4;++n)se::RotateComponent(c,1);for(size_t k=0;k<c.pins.size();++k)require(se::PinPosition(c,k)==se::PinPosition(before,k),"four imported-symbol rotations restore pin positions");
            se::MirrorComponent(c,true);se::MirrorComponent(c,true);for(size_t k=0;k<c.pins.size();++k)require(se::PinPosition(c,k)==se::PinPosition(before,k),"double mirror restores imported pin positions");require(c.graphics[0].points==before.graphics[0].points,"rotations and mirrors retain imported drawing");}
    }
    auto d=load(fixtures+"/rectifier-9.kicad_sch");bool resistor=false,capacitor=false,power=false;for(const auto& c:d.components){resistor|=c.refdes=="R1";if(c.refdes=="R1")require(c.xform==3,"KiCad counterclockwise 90 degrees retained in editable orientation");capacitor|=c.refdes=="C1";power|=c.netLabel=="GND";}require(resistor&&capacitor&&power,"real resistor capacitor and ground represented");
    const auto original=topology(d);
    auto bad=out+"/bad.kicad_sch";se::KiCadImportResult held;held.document=d;
    for(const wxString& text:{wxString("(kicad_sch (version 20250114)"),wxString("(kicad_sch (version nan))"),wxString("(kicad_sch (version 20250114) (lib_symbols) (bus))"),wxString("(kicad_sch (version 20990101) (lib_symbols))"),wxString("(kicad_sch (version 20250114) (lib_symbols) (symbol (lib_id \"missing:R\")))")}){
        require(se::SchematicIO::WriteTextFile(bad,text,&error),"write malformed/unsupported regression fixture");require(!se::ImportKiCadSchematic(bad,held,error)&&!error.empty(),"invalid/unsupported file yields concrete diagnostic");require(topology(held.document)==original,"failed import leaves previous document unchanged");}
    require(se::SchematicIO::WriteTextFile(bad,"(kicad_sch (version 20250114) (lib_symbols) (wire (pts (xy 0 0) (xy 10 0))) (wire (pts (xy 5 0.004) (xy 15 0.004))))",&error),"write close parallel-wire precision regression");
    require(!se::ImportKiCadSchematic(bad,held,error)&&error.Contains("topology"),"reject rounding-induced shorts even without colliding endpoint coordinates");
    require(!se::ImportKiCadSchematic(fixtures+"/dynamic-power-9.kicad_sch",held,error)&&error.Contains("sheet"),"real hierarchy rejected explicitly instead of dropping connections");
    return 0;
}catch(const std::exception& e){std::cerr<<"FAIL "<<e.what()<<'\n';return 1;}}

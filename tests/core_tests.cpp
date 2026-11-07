#include "TestSupport.h"
#include <clocale>
int main(int argc,char** argv) { try {
    std::setlocale(LC_ALL, "");require(argc==3,"test arguments supplied");initModelTests(argv[2]);
    se::SchematicDocument d;wxString error;
    require(se::SchematicIO::Load(wxString::FromUTF8(argv[1]),d,&error),"load existing sample");
    require(d.components.size()==3&&d.wires.size()==5,"sample objects are present");
    auto g=se::NetBuilder::Build(d.components,d.wires);
    require(g.pinNet[0][2]==g.pinNet[1][0]&&g.pinNet[0][2]==g.pinNet[2][0],"T-junction connects all intended terminals");
    auto original=component(L"全加器",1,{10,20});se::RotateComponent(original,1);se::MirrorComponent(original,true);
    d.components={original};d.wires={{1,{10,20},{10,120}}};d.texts={{L"注记 & <内容>",{10,30},18}};
    d.gridStep=20;d.gridVisible=false;d.snapEnabled=false;d.zoom=2.25;
    auto path=wxFileName(wxString::FromUTF8(argv[2]),"roundtrip.ses").GetFullPath();
    require(se::SchematicIO::Save(path,d,&error),"save design including text and transforms");
    se::SchematicDocument reopened;require(se::SchematicIO::Load(path,reopened,&error),"reopen saved design");
    require(reopened.components.size()==1&&reopened.wires.size()==1&&reopened.texts.size()==1,"roundtrip object counts");
    const auto& c=reopened.components[0];
    require(c.id==original.id&&c.name==original.name&&c.refdes==original.refdes&&c.value==original.value&&c.footprint==original.footprint&&c.xform==original.xform&&c.pos==original.pos,"roundtrip component identity and parameters");
    for(size_t k=0;k<c.pins.size();++k)require(se::PinPosition(c,k)==se::PinPosition(original,k)&&c.pins[k].number==original.pins[k].number,"roundtrip pin mapping");
    require(reopened.texts[0].text==d.texts[0].text&&reopened.texts[0].pointSize==18&&reopened.zoom==d.zoom&&!reopened.snapEnabled&&!reopened.gridVisible,"roundtrip text encoding and view");
    return 0;
} catch(const std::exception& e){std::cerr<<"FAIL "<<e.what()<<'\n';return 1;} }

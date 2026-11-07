#include "TestSupport.h"
#include "KiCadExport.h"
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
    auto a=component(L"输入引脚",1,{-200,-20}), b=component(L"输出引脚",2,{100,-20});
    a.footprint="Connector_PinHeader_2.54mm:PinHeader_1x01_P2.54mm_Vertical";
    b.footprint=a.footprint;std::vector<se::ComponentInstance> cs={a,b};
    auto net=se::NetBuilder::Build(cs,{{1,se::PinPosition(a,0),se::PinPosition(b,0)}});wxString text;
    require(se::BuildKiCadNetlist(cs,net,text,error),"KiCad exporter accepts valid physical mapping");
    cs[1].refdes=cs[0].refdes;
    require(!se::BuildKiCadNetlist(cs,net,text,error),"KiCad exporter rejects duplicate physical references");
    cs[1].refdes="OUT2";cs[1].footprint="";
    require(!se::BuildKiCadNetlist(cs,net,text,error),"missing footprint cannot silently drop a physical component");
    auto power=component(L"VCC",1,{0,0});auto other=power;other.id=2;other.pos={200,0};
    auto powers=se::NetBuilder::Build({power,other},{});
    require(powers.pinNet[0][0]==powers.pinNet[1][0],"same named power terminals form one network");
    const auto future=wxFileName(wxString::FromUTF8(argv[2]),"future.ses").GetFullPath();
    require(se::SchematicIO::WriteTextFile(future,"<schematic version=\"99.0\"/>",&error),"prepare unsupported future design");
    require(!se::SchematicIO::Load(future,reopened,&error)&&reopened.components.size()==1,"unsupported version cannot replace current document");
    auto blank=d;blank.components[0].value.clear();blank.components[0].footprint.clear();
    const auto blankPath=wxFileName(wxString::FromUTF8(argv[2]),"blank-parameters.ses").GetFullPath();
    require(se::SchematicIO::Save(blankPath,blank,&error)&&se::SchematicIO::Load(blankPath,reopened,&error)&&reopened.components[0].value.empty()&&reopened.components[0].footprint.empty(),"explicitly empty parameters survive roundtrip without library substitution");
    auto invalid=d;invalid.components.push_back(invalid.components[0]);
    require(!se::SchematicIO::Save(path,invalid,&error),"invalid duplicate IDs cannot overwrite a saved design");
    require(se::SchematicIO::Load(path,reopened,&error)&&reopened.components.size()==1,"rejected save leaves original design readable");
    const auto bad=wxFileName(wxString::FromUTF8(argv[2]),"bad.ses").GetFullPath();
    require(se::SchematicIO::WriteTextFile(bad,"<schematic version=\"1.0\"><components><component id=\"oops\"/></components></schematic>",&error),"prepare malformed numeric fixture");
    require(!se::SchematicIO::Load(bad,reopened,&error)&&reopened.components.size()==1,"malformed file leaves current document intact");
    auto label=component(L"网络标签",10);label.value="SHARED";auto label2=label;label2.id=11;label2.refdes="L11";label2.pos={200,0};
    auto labels=se::NetBuilder::Build({label,label2},{});
    require(labels.pinNet[0][0]==labels.pinNet[1][0],"same explicit labels merge electrical nets");
    label2.value="OTHER";labels=se::NetBuilder::Build({label,label2},{});
    require(labels.pinNet[0][0]!=labels.pinNet[1][0],"different labels preserve separate nets");
    return 0;
} catch(const std::exception& e){std::cerr<<"FAIL "<<e.what()<<'\n';return 1;} }

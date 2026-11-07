#include "TestSupport.h"
#include "CircuitTools.h"
#include "EditingTools.h"
#include <clocale>
#include <set>
namespace {
se::ComponentInstance terminal(int id,wxPoint p,bool output){
    auto c=component(output?L"输入引脚":L"输出引脚",id,p);c.w=40;c.h=40;return c;
}
void connected(const std::vector<se::ComponentInstance>& cs,const std::vector<se::Wire>& ws,int a,int b){
    auto g=se::NetBuilder::Build(cs,ws);require(g.pinNet[a][0]==g.pinNet[b][0],"edit preserves terminal connectivity");
    for(const auto& w:ws)require(w.IsAxisAligned()&&w.a!=w.b,"edited wires stay orthogonal and nonzero");
}
}
int main(int argc,char**argv){try{
    std::setlocale(LC_ALL, "");require(argc==3,"test arguments supplied");initModelTests(argv[2]);
    auto c=component(L"全加器",1,{0,0});auto original=c;
    for(int i=0;i<4;++i)se::RotateComponent(c,1);
    require(c.xform==original.xform&&c.w==original.w&&c.h==original.h,"four rotations restore orientation");
    for(size_t k=0;k<c.pins.size();++k)require(se::PinPosition(c,k)==se::PinPosition(original,k),"four rotations restore terminal positions");
    se::MirrorComponent(c,true);se::MirrorComponent(c,true);require(c.xform==original.xform,"two mirrors restore orientation");
    se::ComponentDef dense;dense.name=L"自定义";dense.refPrefix="U";dense.inputs=32;dense.outputs=1;dense.w=40;dense.h=40;wxString error;
    require(!se::ComponentCatalog::ValidateDefinition(dense,&error)&&!error.empty(),"reject a definition that shorts independent inputs by position");
    dense.h=400;require(se::ComponentCatalog::ValidateDefinition(dense,&error),"larger custom definition has room for every input");
    require(!se::ComponentCatalog::SaveUserLibrary({{L"用户自定义",{{dense.name,dense.refPrefix,"",32,1,se::ComponentKind::Logic,40,40}}}},&error),"library save rejects colliding terminals");
    std::vector<se::ComponentInstance> cs={component(L"与门",1),component(L"或门",2)};cs[0].refdes="U1";cs[1].refdes="U3";
    require(se::NextReference(cs,"U")=="U2","shared-prefix allocator checks every component type");
    auto a=terminal(1,{-200,-20},true),b=terminal(2,{100,-20},false);
    se::Wire wire{1,se::PinPosition(a,0),se::PinPosition(b,0)};std::vector<se::Wire> ws;
    cs={a,b};ws={wire};se::MoveConnectedComponents(cs,ws,{0,1},{0,40});connected(cs,ws,0,1);
    require(ws[0].a.y==40&&ws[0].b.y==40,"moving both ends translates their shared wire");
    cs={a,b};ws={{1,{-180,0},{100,0}}};se::MoveConnectedComponents(cs,ws,{0},{0,40});connected(cs,ws,0,1);
    auto fixed=terminal(2,{10,-20},false),third=terminal(3,{10,80},false);
    cs={a,fixed,third};ws={{1,se::PinPosition(a,0),{0,0}},{2,{0,0},{0,100}}};se::MoveConnectedComponents(cs,ws,{0},{0,40});connected(cs,ws,0,1);connected(cs,ws,0,2);
    return 0;
}catch(const std::exception& e){std::cerr<<"FAIL "<<e.what()<<'\n';return 1;}}

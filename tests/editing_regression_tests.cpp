#include "TestSupport.h"
#include <clocale>
#include <set>
int main(int argc,char**argv){try{
    std::setlocale(LC_ALL, "");require(argc==3,"test arguments supplied");initModelTests(argv[2]);
    auto c=component(L"全加器",1,{0,0});auto original=c;
    for(int i=0;i<4;++i)se::RotateComponent(c,1);
    require(c.xform==original.xform&&c.w==original.w&&c.h==original.h,"four rotations restore orientation");
    for(size_t k=0;k<c.pins.size();++k)require(se::PinPosition(c,k)==se::PinPosition(original,k),"four rotations restore terminal positions");
    se::MirrorComponent(c,true);se::MirrorComponent(c,true);
    require(c.xform==original.xform,"two mirrors restore orientation");
    // An existing accepted layout must never silently short independent inputs.
    c.w=40;c.h=40;c.pins=se::MakePins(L"用户自定义",se::ComponentKind::Logic,32,1);
    std::set<std::pair<int,int>> seen;
    for(size_t k=0;k<c.pins.size();++k){auto p=se::PinPosition(c,k);require(seen.insert({p.x,p.y}).second,"custom terminals have unique coordinates");}
    return 0;
}catch(const std::exception& e){std::cerr<<"FAIL "<<e.what()<<'\n';return 1;}}

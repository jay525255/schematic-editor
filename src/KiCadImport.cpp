#include "KiCadImport.h"
#include <wx/file.h>
#include <algorithm>
#include <cmath>
#include <cctype>
#include <map>
#include <set>
#include <stdexcept>
#include <string>

namespace se {
namespace {
constexpr double Scale = 100.0; // mm -> world units; rounded once, after all KiCad transforms
constexpr double Pi = 3.14159265358979323846;
struct Node {
    std::string atom;
    std::vector<Node> children;
    size_t line = 1;
    bool list = false;
    std::string value(size_t i=1) const {
        if (i >= children.size() || children[i].list) throw std::runtime_error("Missing atom at line " + std::to_string(line));
        return children[i].atom;
    }
    std::string key() const { return list && !children.empty() ? value(0) : atom; }
    const Node* find(const std::string& key) const {
        for (const auto& c : children) if (c.list && c.key() == key) return &c;
        return nullptr;
    }
    const Node& need(const std::string& key) const {
        auto* n=find(key); if (!n) throw std::runtime_error("Missing " + key + " at line " + std::to_string(line)); return *n;
    }
    bool flag(const std::string& name) const {
        for (const auto& c:children) if (!c.list && c.atom==name) return true;
        auto* n=find(name); return n && (n->children.size()==1 || n->value()=="yes");
    }
};
class Parser {
    const std::string& s; size_t at=0,line=1,count=0;
    [[noreturn]] void fail(const char* what) const { throw std::runtime_error(std::string(what)+" at line "+std::to_string(line)); }
    void space() { while(at<s.size()) { char c=s[at]; if(c==' '||c=='\t'||c=='\r'||c=='\n') {if(c=='\n')++line;++at;} else if(c==';'){while(at<s.size()&&s[at]!='\n')++at;} else break; } }
    Node read(int depth) {
        space(); if(depth>100 || ++count>1000000) fail("S-expression resource limit");
        if(at>=s.size()) fail("Unexpected end of file"); Node n; n.line=line;
        if(s[at]=='(') { ++at; n.list=true; space(); while(at<s.size()&&s[at]!=')') {n.children.push_back(read(depth+1));space();} if(at==s.size())fail("Unclosed list");++at;if(n.children.empty()||n.children[0].list)fail("Missing list name"); }
        else if(s[at]=='"') { ++at; bool closed=false; while(at<s.size()) {char c=s[at++];if(c=='"'){closed=true;break;}if(c=='\n')++line; if(c=='\\'){if(at==s.size())fail("Incomplete escape");c=s[at++];switch(c){case 'n':c='\n';break;case 'r':c='\r';break;case 't':c='\t';break;case '\\':case '"':break;default:fail("Unsupported string escape");}} n.atom+=c;} if(!closed)fail("Unclosed string"); }
        else {while(at<s.size()&&s[at]!='('&&s[at]!=')'&&!std::isspace(static_cast<unsigned char>(s[at])))n.atom+=s[at++];if(n.atom.empty())fail("Unexpected token");}
        return n;
    }
public:
    explicit Parser(const std::string& input):s(input) {if(s.compare(0,3,"\xef\xbb\xbf")==0)at=3;}
    Node parse(){auto n=read(0);space();if(at!=s.size())fail("Trailing content");return n;}
};
wxString W(const std::string& s) {auto w=wxString::FromUTF8(s.data(),s.size());if(!s.empty()&&w.empty())throw std::runtime_error("Invalid UTF-8");return w;}
double num(const Node& n,size_t i=1) {double v;auto s=W(n.value(i));if(!s.ToCDouble(&v)||!std::isfinite(v)||std::abs(v)>1000000000)throw std::runtime_error("Invalid number at line "+std::to_string(n.line));return v;}
int integer(const Node& n,size_t i=1) {double v=num(n,i);if(v!=std::floor(v))throw std::runtime_error("Expected integer at line "+std::to_string(n.line));return static_cast<int>(v);}
struct Point {double x=0,y=0;};
Point xy(const Node& n) {return {num(n,1),num(n,2)};}
wxPoint world(Point p) {if(std::abs(p.x)>9000||std::abs(p.y)>9000)throw std::runtime_error("Coordinate exceeds supported canvas range");return {static_cast<int>(std::lround(p.x*Scale)),static_cast<int>(std::lround(p.y*Scale))};}
struct Transform {
    Point origin; int angle=0; std::string mirror;
    Point operator()(Point p) const {
        // KiCad library Y points upwards. Instance angles rotate counterclockwise;
        // mirror x/y is applied in sheet coordinates AFTER rotation (KiCad parser).
        Point r;
        switch(angle){case 0:r={p.x,-p.y};break;case 90:r={-p.y,-p.x};break;case 180:r={-p.x,p.y};break;default:r={p.y,p.x};break;}
        if(mirror=="x")r.y=-r.y; if(mirror=="y")r.x=-r.x;
        return {origin.x+r.x,origin.y+r.y};
    }
};
Transform transform(const Node& inst) {Transform t;t.origin=xy(inst.need("at"));t.angle=integer(inst.need("at"),3);if(t.angle!=0&&t.angle!=90&&t.angle!=180&&t.angle!=270)throw std::runtime_error("Symbol angle must be 0/90/180/270");if(auto*m=inst.find("mirror")){t.mirror=m->value();if(t.mirror!="x"&&t.mirror!="y")throw std::runtime_error("Unknown mirror axis");}return t;}
bool hidden(const Node& n) {auto* effects=n.find("effects");return n.flag("hide")||(effects&&effects->flag("hide"));}
int textSize(const Node& n) {auto* e=n.find("effects");auto* f=e?e->find("font"):nullptr;auto* s=f?f->find("size"):nullptr;return s?std::max(1,static_cast<int>(std::lround(num(*s)*Scale))):127;}
std::string prop(const Node& n,const std::string& key,const std::string& fallback="") {for(const auto& c:n.children)if(c.list&&c.key()=="property"&&c.value()==key)return c.value(2);return fallback;}
void electricalName(const std::string& name) {if(name.empty()||name.find("${")!=std::string::npos||name.find("..")!=std::string::npos||name.find('{')!=std::string::npos)throw std::runtime_error("Unresolved variable or bus expression in electrical name: "+name);}
class Converter {
    const Node& root; KiCadImportResult result;
    std::map<std::string,const Node*> libs;
    std::map<std::pair<int,int>,std::pair<long long,long long>> electricalPoints;
    std::set<wxString> refs, warnings;
    int componentId=0;
    wxPoint terminal(Point p) {
        auto pt=world(p);auto source=std::make_pair(std::llround(p.x*1000000),std::llround(p.y*1000000));
        auto key=std::make_pair(pt.x,pt.y);auto it=electricalPoints.find(key);
        if(it!=electricalPoints.end()&&it->second!=source)throw std::runtime_error("Electrical coordinates closer than 0.01 mm would merge; import cancelled");
        electricalPoints[key]=source;return pt;
    }
    void verifyConnectivity() const {
        // Check the entire electrical drawing before committing the conversion.
        // Rounding must not turn nearby parallel wires or a near-miss T into a short.
        using Precise=std::pair<long long,long long>;
        std::vector<Precise> precise,rounded;
        std::map<std::pair<int,int>,int> index;
        for(const auto& entry:electricalPoints) {
            index[entry.first]=static_cast<int>(precise.size());
            precise.push_back(entry.second);rounded.push_back({entry.first.first,entry.first.second});
        }
        struct Dsu {
            std::vector<int> parent;
            explicit Dsu(size_t count):parent(count){for(size_t i=0;i<count;++i)parent[i]=static_cast<int>(i);}
            int find(int x){while(parent[x]!=x){parent[x]=parent[parent[x]];x=parent[x];}return x;}
            void join(int a,int b){a=find(a);b=find(b);if(a!=b)parent[a]=b;}
        } exact(precise.size()), converted(precise.size());
        auto id=[&](wxPoint p){return index.at({p.x,p.y});};
        auto on=[](Precise p,Precise a,Precise b) {
            if(a.first==b.first)return p.first==a.first&&p.second>=std::min(a.second,b.second)&&p.second<=std::max(a.second,b.second);
            if(a.second==b.second)return p.second==a.second&&p.first>=std::min(a.first,b.first)&&p.first<=std::max(a.first,b.first);
            return false;
        };
        for(const auto& wire:result.document.wires) {
            int a=id(wire.a),b=id(wire.b);exact.join(a,b);converted.join(a,b);
            for(size_t i=0;i<precise.size();++i){
                if(on(precise[i],precise[a],precise[b]))exact.join(a,static_cast<int>(i));
                if(on(rounded[i],rounded[a],rounded[b]))converted.join(a,static_cast<int>(i));
            }
        }
        std::map<wxString,int> named;
        for(const auto& c:result.document.components)for(size_t k=0;k<c.pins.size();++k) {
            wxString name=c.netLabel.empty()?c.pins[k].implicitNet:c.netLabel;
            if(name.empty())continue;int node=id(PinPosition(c,static_cast<int>(k)));
            auto found=named.find(name);if(found==named.end())named[name]=node;
            else {exact.join(found->second,node);converted.join(found->second,node);}
        }
        std::map<int,int> forward,reverse;
        for(size_t i=0;i<precise.size();++i) {
            int a=exact.find(static_cast<int>(i)),b=converted.find(static_cast<int>(i));
            if((forward.count(a)&&forward[a]!=b)||(reverse.count(b)&&reverse[b]!=a))
                throw std::runtime_error("0.01 mm coordinate rounding would change electrical topology; import cancelled");
            forward[a]=b;reverse[b]=a;
        }
    }
    void graphic(ComponentInstance& c,const Node& n,const Transform& t) {
        SymbolGraphic g;auto add=[&](Point p){g.points.push_back(world(t(p)));};auto key=n.key();
        if(auto* stroke=n.find("stroke"))if(auto* width=stroke->find("width"))g.width=std::max(0,static_cast<int>(std::lround(num(*width)*Scale)));
        if(auto* fill=n.find("fill")){auto type=fill->need("type").value();g.fill=type=="outline"?1:type=="background"?2:0;}
        if(key=="polyline") {for(const auto& pt:n.need("pts").children)if(pt.list&&pt.key()=="xy")add(xy(pt));}
        else if(key=="rectangle") {auto a=xy(n.need("start")),b=xy(n.need("end"));add(a);add({b.x,a.y});add(b);add({a.x,b.y});add(a);}
        else if(key=="circle") {auto a=xy(n.need("center"));double r=num(n.need("radius"));if(r<=0)throw std::runtime_error("Invalid circle radius");for(int i=0;i<=64;++i)add({a.x+r*std::cos(2*Pi*i/64),a.y+r*std::sin(2*Pi*i/64)});}
        else if(key=="arc") {
            auto a=xy(n.need("start")),b=xy(n.need("mid")),d=xy(n.need("end"));
            double det=2*(a.x*(b.y-d.y)+b.x*(d.y-a.y)+d.x*(a.y-b.y));if(std::abs(det)<1e-12)throw std::runtime_error("Degenerate symbol arc");
            double aa=a.x*a.x+a.y*a.y,bb=b.x*b.x+b.y*b.y,dd=d.x*d.x+d.y*d.y;
            Point center{(aa*(b.y-d.y)+bb*(d.y-a.y)+dd*(a.y-b.y))/det,(aa*(d.x-b.x)+bb*(a.x-d.x)+dd*(b.x-a.x))/det};
            auto positive=[](double v){while(v<0)v+=2*Pi;while(v>=2*Pi)v-=2*Pi;return v;};
            double start=std::atan2(a.y-center.y,a.x-center.x),end=positive(std::atan2(d.y-center.y,d.x-center.x)-start),mid=positive(std::atan2(b.y-center.y,b.x-center.x)-start);
            double sweep=mid<=end?end:end-2*Pi,r=std::hypot(a.x-center.x,a.y-center.y);
            for(int i=0;i<=48;++i){double angle=start+sweep*i/48;add({center.x+r*std::cos(angle),center.y+r*std::sin(angle)});}
        }
        else if(key=="bezier") {std::vector<Point> p;for(const auto& pt:n.need("pts").children)if(pt.list&&pt.key()=="xy")p.push_back(xy(pt));if(p.size()!=4)throw std::runtime_error("Bezier requires four points");for(int i=0;i<=48;++i){double u=i/48.,v=1-u;add({v*v*v*p[0].x+3*v*v*u*p[1].x+3*v*u*u*p[2].x+u*u*u*p[3].x,v*v*v*p[0].y+3*v*v*u*p[1].y+3*v*u*u*p[2].y+u*u*u*p[3].y});}}
        else if(key=="text") {if(hidden(n))return;g.text=W(n.value());g.textSize=textSize(n);add(xy(n.need("at")));}
        else throw std::runtime_error("Unsupported symbol graphic: "+key);
        if(g.points.empty())throw std::runtime_error("Empty symbol graphic");c.graphics.push_back(std::move(g));
    }
    void pin(ComponentInstance& c,const Node& p,const Node& lib,const Transform& t,bool power) {
        Pin pin;pin.exact=true;pin.pad=W(p.need("number").value());if(pin.pad.empty())throw std::runtime_error("Empty physical pin number");
        pin.number=static_cast<int>(c.pins.size())+1;pin.name=W(p.need("name").value());if(pin.name.empty())pin.name=pin.pad;
        const auto kind=p.value(); pin.dir=kind=="input"?PinDir::Input:kind=="output"||kind=="tri_state"?PinDir::Output:kind=="bidirectional"?PinDir::Bidir:PinDir::Passive;
        const auto& at=p.need("at");auto a=xy(at);int angle=integer(at,3);if(angle%90)throw std::runtime_error("Unsupported pin angle");double length=num(p.need("length"));if(length<0)throw std::runtime_error("Negative pin length");
        Point b{a.x+length*std::cos(angle*Pi/180),a.y+length*std::sin(angle*Pi/180)};
        pin.endpoint=terminal(t(a));pin.anchor=world(t(b));pin.hidden=p.flag("hide");
        const auto d=pin.endpoint-pin.anchor;pin.side=std::abs(d.x)>std::abs(d.y)?(d.x<0?Side::Left:Side::Right):(d.y<0?Side::Top:Side::Bottom);
        if(kind=="power_in"&&pin.hidden&&!power){electricalName(pin.name.utf8_string());pin.implicitNet=pin.name;}
        if(power&&kind=="power_in") {auto name=c.value.utf8_string();electricalName(name);c.netLabel=W(name);}
        c.pins.push_back(pin);
        const auto shape=p.value(2);if(shape!="line")warnings.insert(L"引脚端部装饰统一显示为直线；引脚名称、编号与连接位置保留。");
        auto label=[&](const wxString& text,const wxPoint& where,int size){if(text.empty()||text=="~")return;SymbolGraphic g;g.text=text;g.points.push_back(where);g.textSize=size;c.graphics.push_back(g);};
        auto* names=lib.find("pin_names");auto* numbers=lib.find("pin_numbers");
        if(!pin.hidden&&!(numbers&&numbers->flag("hide")))label(pin.pad,(pin.endpoint+pin.anchor)/2+wxPoint(0,-45),70);
        if(!pin.hidden&&!(names&&names->flag("hide")))label(pin.name,pin.anchor+wxPoint(d.x<0?80:d.x>0?-80:0,d.y<0?80:d.y>0?-80:0),80);
    }
    void symbol(const Node& inst) {
        auto id=inst.need("lib_id").value();auto* alias=inst.find("lib_name");auto found=libs.find(alias?alias->value():id);
        if(found==libs.end())throw std::runtime_error("Missing embedded lib_symbols definition: "+(alias?alias->value():id));const auto& lib=*found->second;
        if(lib.find("extends"))throw std::runtime_error("Embedded symbol inheritance is not resolved: "+id);
        ComponentInstance c;c.imported=true;c.id=++componentId;c.name=W(id);c.refdes=W(prop(inst,"Reference"));c.value=W(prop(inst,"Value",prop(lib,"Value",id)));c.footprint=W(prop(inst,"Footprint",prop(lib,"Footprint")));
        if(c.refdes.empty()||c.refdes.Contains("?"))throw std::runtime_error("Unannotated or empty symbol reference: "+id);
        if(!refs.insert(c.refdes).second)throw std::runtime_error("Duplicate reference / multi-unit package not supported: "+c.refdes.utf8_string());
        bool power=lib.find("power")!=nullptr;c.pcbExcluded=power||(inst.find("on_board")&&inst.need("on_board").value()=="no");
        for(const auto& n:inst.children)if(n.list&&n.key()=="property")c.properties.push_back({W(n.value()),W(n.value(2))});
        auto t=transform(inst);
        c.xform=((360-t.angle)%360)/90;
        if(t.mirror=="x")c.xform=4+(2-c.xform+4)%4;
        else if(t.mirror=="y")c.xform=4+(4-c.xform)%4;
        int unit=inst.find("unit")?integer(inst.need("unit")):1;int style=inst.find("convert")?integer(inst.need("convert")):1;
        if(inst.find("body_style"))style=integer(inst.need("body_style"));
        if(unit<1||style<1||style>2)throw std::runtime_error("Invalid symbol unit/body style");bool matched=false,specificUnits=false;
        const std::set<std::string> graphics={"polyline","rectangle","circle","arc","bezier","text"};
        auto contents=[&](const Node& parent){for(const auto& item:parent.children)if(item.list){auto key=item.key();if(key=="pin")pin(c,item,lib,t,power);else if(graphics.count(key))graphic(c,item,t);else if(key=="text_box"||key=="image")throw std::runtime_error("Unsupported graphic in embedded symbol: "+key);}};
        contents(lib);
        for(const auto& child:lib.children)if(child.list&&child.key()=="symbol") {
            auto name=child.value();auto last=name.rfind('_'),previous=last==std::string::npos?last:name.rfind('_',last-1);if(previous==std::string::npos)throw std::runtime_error("Invalid symbol unit identifier: "+name);
            auto u=name.substr(previous+1,last-previous-1),s=name.substr(last+1);
            if(u!="0")specificUnits=true;
            if((u=="0"||u==std::to_string(unit))&&(s=="0"||s==std::to_string(style))){contents(child);if(u==std::to_string(unit))matched=true;}
        }
        if(!matched&&specificUnits)throw std::runtime_error("Missing unit/body style in symbol: "+id);
        if(c.pins.empty()&&c.graphics.empty())throw std::runtime_error("Embedded symbol has no graphics or pins: "+id);
        std::set<wxString> pads;for(const auto& p:c.pins)if(!pads.insert(p.pad).second)throw std::runtime_error("Stacked duplicate pad number not supported: "+c.refdes.utf8_string()+"."+p.pad.utf8_string());
        // Instance properties use absolute sheet coordinates, not the library transform.
        for(const auto& property:inst.children)if(property.list&&property.key()=="property"&&!hidden(property)&&property.find("at")){
            auto name=property.value(),value=property.value(2);if(value.empty()||value=="~")continue;SymbolGraphic g;g.text=W(value);g.property=W(name);g.textSize=textSize(property);g.points.push_back(world(xy(property.need("at"))));c.graphics.push_back(g);
        }
        int minX=world(t.origin).x-30,minY=world(t.origin).y-30,maxX=minX+60,maxY=minY+60;
        auto grow=[&](const wxPoint& p){minX=std::min(minX,p.x);minY=std::min(minY,p.y);maxX=std::max(maxX,p.x);maxY=std::max(maxY,p.y);};
        for(const auto& p:c.pins){grow(p.endpoint);grow(p.anchor);}
        for(const auto& g:c.graphics) {
            for(const auto& p:g.points)grow(p);
            if(!g.text.empty()&&!g.points.empty()){int half=static_cast<int>(g.text.size()*g.textSize*0.65/2);grow(g.points[0]-wxPoint(half,g.textSize/2));grow(g.points[0]+wxPoint(half,g.textSize/2));}
        }
        c.pos={minX,minY};c.w=std::max(1,maxX-minX);c.h=std::max(1,maxY-minY);
        if(c.w>100000||c.h>100000)throw std::runtime_error("Symbol bounding box exceeds supported size");
        for(auto& p:c.pins){p.endpoint-=c.pos;p.anchor-=c.pos;}for(auto& g:c.graphics)for(auto& p:g.points)p-=c.pos;
        // Graphic-only symbols have no electrical effect and remain editable.
        result.document.components.push_back(std::move(c));
    }
    void label(const Node& n) {
        electricalName(n.value());ComponentInstance c;c.imported=true;c.pcbExcluded=true;c.id=++componentId;c.name=n.key()=="global_label"?L"全局标签":L"网络标签";c.refdes=wxString::Format("#LABEL%d",c.id);
        while(refs.count(c.refdes))c.refdes+="_";refs.insert(c.refdes);c.value=W(n.value());c.netLabel=c.value;
        auto at=terminal(xy(n.need("at")));c.pos=at-wxPoint(0,180);c.w=std::max(100,static_cast<int>(c.value.size())*80);c.h=180;
        Pin p;p.name=c.value;p.number=1;p.exact=true;p.pad="1";p.anchor={0,180};p.endpoint={0,180};p.dir=PinDir::Passive;c.pins.push_back(p);
        SymbolGraphic g;g.property="Value";g.textSize=textSize(n);g.points.push_back({c.w/2,80});c.graphics.push_back(g);result.document.components.push_back(c);
    }
public:
    explicit Converter(const Node& n):root(n){}
    KiCadImportResult convert() {
        if(root.key()!="kicad_sch")throw std::runtime_error("Expected kicad_sch root");
        int version=integer(root.need("version"));if(version<20211123||version>20250114)throw std::runtime_error("Unsupported KiCad schematic version: "+std::to_string(version));
        const std::set<std::string> metadata={"version","generator","generator_version","uuid","paper","title_block","lib_symbols","sheet_instances","symbol_instances","embedded_fonts"};
        const std::set<std::string> supported={"symbol","wire","junction","label","global_label","no_connect","text","text_box","polyline","rectangle","circle","arc","bezier"};
        for(const auto& n:root.children)if(n.list&&!metadata.count(n.key())&&!supported.count(n.key()))throw std::runtime_error("Unsupported schematic element '"+n.key()+"' at line "+std::to_string(n.line)+" (hierarchy, buses and external resources cannot be flattened safely)");
        for(const auto& lib:root.need("lib_symbols").children)if(lib.list&&lib.key()=="symbol")if(!libs.emplace(lib.value(),&lib).second)throw std::runtime_error("Duplicate embedded symbol definition");
        for(const auto& n:root.children)if(n.list&&n.key()=="symbol")symbol(n);
        for(const auto& n:root.children)if(n.list&&(n.key()=="label"||n.key()=="global_label"))label(n);
        for(const auto& n:root.children)if(n.list&&n.key()=="wire") {
            std::vector<wxPoint> pts;std::vector<Point> source;
            for(const auto& p:n.need("pts").children)if(p.list&&p.key()=="xy"){source.push_back(xy(p));pts.push_back(terminal(source.back()));}
            if(source.size()==2&&source[0].x!=source[1].x&&source[0].y!=source[1].y)throw std::runtime_error("Diagonal electrical wire not supported");
            if(pts.size()!=2)throw std::runtime_error("Wire must have exactly two endpoints");Wire w;w.a=pts[0];w.b=pts[1];if(!w.IsAxisAligned()||w.a==w.b)throw std::runtime_error("Diagonal or zero-length electrical wire not supported");result.document.wires.push_back(w);
        }
        // Split at explicit junctions so the existing net builder distinguishes a
        // crossing with a junction from an unconnected crossing without one.
        for(const auto& n:root.children)if(n.list&&n.key()=="junction") {
            auto p=terminal(xy(n.need("at")));auto size=result.document.wires.size();for(size_t i=0;i<size;++i){auto& w=result.document.wires[i];if(p!=w.a&&p!=w.b&&PointOnSegment(p,w.a,w.b)){Wire tail;tail.a=p;tail.b=w.b;w.b=p;result.document.wires.push_back(tail);}}
        }
        int wireId=0;for(auto& w:result.document.wires)w.id=++wireId;
        for(const auto& n:root.children)if(n.list) {
            auto key=n.key();
            if(key=="no_connect") {
                auto at=terminal(xy(n.need("at")));bool attached=false;
                for(auto& c:result.document.components)for(size_t k=0;k<c.pins.size();++k)if(PinPosition(c,static_cast<int>(k))==at){c.pins[k].noConnect=true;attached=true;}
                if(!attached){TextItem mark;mark.text=L"×";mark.pos=at-wxPoint(20,40);mark.pointSize=6;result.document.texts.push_back(mark);}
            }else if(key=="text"||key=="text_box") {
                TextItem text;text.text=W(n.value());text.pos=world(xy(n.need("at")));text.pointSize=std::max(6,std::min(72,textSize(n)/13));result.document.texts.push_back(text);
                if(key=="text_box")warnings.insert(L"文本框转换为可编辑注记，边框和排版未保留。");
            }else if(key=="polyline"||key=="rectangle"||key=="circle"||key=="arc"||key=="bezier") {
                ComponentInstance c;c.imported=true;c.pcbExcluded=true;c.id=++componentId;c.refdes=wxString::Format("#GRAPHIC%d",c.id);while(refs.count(c.refdes))c.refdes+="_";refs.insert(c.refdes);c.name=L"KiCad 图形";Transform t; // sheet coordinates already point down
                // Convert using a second Y reversal to neutralize the library transform.
                graphic(c,n,t);for(auto& g:c.graphics)for(auto& p:g.points)p.y=-p.y;
                int minX=0,minY=0,maxX=1,maxY=1;bool first=true;for(const auto& g:c.graphics)for(const auto& p:g.points){if(first){minX=maxX=p.x;minY=maxY=p.y;first=false;}minX=std::min(minX,p.x);minY=std::min(minY,p.y);maxX=std::max(maxX,p.x);maxY=std::max(maxY,p.y);}
                c.pos={minX,minY};c.w=std::max(1,maxX-minX);c.h=std::max(1,maxY-minY);for(auto& g:c.graphics)for(auto& p:g.points)p-=c.pos;result.document.components.push_back(c);
            }
        }
        if(result.document.components.size()>5000||result.document.wires.size()>10000)throw std::runtime_error("Schematic exceeds import object limit");
        verifyConnectivity();
        result.document.gridStep=10;result.document.zoom=0.1;result.warnings.assign(warnings.begin(),warnings.end());return std::move(result);
    }
};
}
bool ImportKiCadSchematic(const wxString& path,KiCadImportResult& result,wxString& error) {
    error.clear();try {
        wxFile file(path);if(!file.IsOpened())throw std::runtime_error("Cannot open KiCad schematic");auto size=file.Length();if(size<=0||size>16*1024*1024)throw std::runtime_error("Empty schematic or file exceeds 16 MiB limit");
        std::string bytes(static_cast<size_t>(size),'\0');if(file.Read(bytes.data(),bytes.size())!=size)throw std::runtime_error("Cannot read schematic");if(bytes.find('\0')!=std::string::npos)throw std::runtime_error("NUL byte in schematic");W(bytes);
        auto root=Parser(bytes).parse();auto imported=Converter(root).convert();result=std::move(imported);return true;
    }catch(const std::exception& e){error=L"KiCad 导入失败："+wxString::FromUTF8(e.what());return false;}
}
}

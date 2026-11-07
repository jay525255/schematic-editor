#include "EditingTools.h"
#include <algorithm>
#include <map>
#include <set>
#include <utility>
namespace se {
namespace {
using Position = std::pair<int,int>;
Position key(const wxPoint& p) { return {p.x,p.y}; }
bool sameTerminals(const NetGraph& oldGraph, const NetGraph& newGraph) {
    if (oldGraph.pinNet.size()!=newGraph.pinNet.size()) return false;
    std::map<int,int> forward,backward;
    for(size_t c=0;c<oldGraph.pinNet.size();++c) {
        if(oldGraph.pinNet[c].size()!=newGraph.pinNet[c].size())return false;
        for(size_t k=0;k<oldGraph.pinNet[c].size();++k) {
            int a=oldGraph.pinNet[c][k],b=newGraph.pinNet[c][k];
            if((forward.count(a)&&forward[a]!=b)||(backward.count(b)&&backward[b]!=a))return false;
            forward[a]=b;backward[b]=a;
        }
    }
    return true;
}
}
bool ReconnectChangedComponents(const std::vector<ComponentInstance>& before,
    const std::vector<ComponentInstance>& after, std::vector<Wire>& wires) {
    if(before.size()!=after.size())return false;
    std::set<Position> nodes;
    std::map<Position,std::vector<wxPoint>> targets;
    for(size_t ci=0;ci<before.size();++ci) {
        if(before[ci].pins.size()!=after[ci].pins.size())return false;
        for(size_t k=0;k<before[ci].pins.size();++k) {
            const auto old=PinPosition(before[ci],static_cast<int>(k));
            const auto fresh=PinPosition(after[ci],static_cast<int>(k));
            nodes.insert(key(old));auto& list=targets[key(old)];
            if(std::find(list.begin(),list.end(),fresh)==list.end())list.push_back(fresh);
        }
    }
    int nextId=0;
    for(const auto& w:wires){nextId=std::max(nextId,w.id);nodes.insert(key(w.a));nodes.insert(key(w.b));}
    std::vector<Wire> pieces;
    for(const auto& w:wires) {
        if(!w.IsAxisAligned())return false;
        std::vector<wxPoint> cuts;
        for(auto p:nodes)if(PointOnSegment({p.first,p.second},w.a,w.b))cuts.emplace_back(p.first,p.second);
        std::sort(cuts.begin(),cuts.end(),[&](const wxPoint&a,const wxPoint&b){return w.a.x==w.b.x?a.y<b.y:a.x<b.x;});
        for(size_t k=1;k<cuts.size();++k)pieces.push_back({k==1?w.id:++nextId,cuts[k-1],cuts[k]});
    }
    const auto oldGraph=NetBuilder::Build(before,wires);
    auto destination=[&](const wxPoint& p){auto i=targets.find(key(p));return i==targets.end()?p:i->second.front();};
    // Remove obsolete degree-two bends as an alternative. A mirrored terminal
    // can occupy a former bend, so retaining that bend can create an unintended T.
    std::map<Position,std::vector<size_t>> adjacent;
    for(size_t i=0;i<pieces.size();++i){adjacent[key(pieces[i].a)].push_back(i);adjacent[key(pieces[i].b)].push_back(i);}
    std::vector<bool> visited(pieces.size(),false);
    std::vector<Wire> chains;
    for(const auto& item:adjacent) {
        if(item.second.size()==2&&!targets.count(item.first))continue;
        for(size_t edge:item.second) {
            if(visited[edge])continue;
            wxPoint start(item.first.first,item.first.second),at=start;size_t current=edge;
            while(true) {
                visited[current]=true;const auto& part=pieces[current];
                at=part.a==at?part.b:part.a;
                const auto& next=adjacent[key(at)];
                if(next.size()!=2||targets.count(key(at)))break;
                auto following=next[0]==current?next[1]:next[0];
                if(visited[following])break;current=following;
            }
            if(start!=at)chains.push_back({pieces[edge].id,start,at});
        }
    }
    for(size_t i=0;i<pieces.size();++i)if(!visited[i])chains.push_back(pieces[i]);
    // Try preserved paths first, then reroute chains without obsolete bends.
    // Every accepted candidate must retain the entire terminal partition.
    for(int alternate=0;alternate<4;++alternate) {
        std::vector<Wire> candidate;int id=nextId;
        auto route=[&](wxPoint a,wxPoint b,int originalId,bool horizontal){
            if(a==b)return;
            if(a.x==b.x||a.y==b.y){candidate.push_back({originalId,a,b});return;}
            auto bend=(horizontal!=bool(alternate%2))?wxPoint(b.x,a.y):wxPoint(a.x,b.y);
            candidate.push_back({originalId,a,bend});candidate.push_back({++id,bend,b});
        };
        for(const auto& part:(alternate<2?pieces:chains))route(destination(part.a),destination(part.b),part.id,part.a.y==part.b.y);
        for(const auto& item:targets)for(size_t k=1;k<item.second.size();++k)
            route(item.second.front(),item.second[k],++id,true);
        if(sameTerminals(oldGraph,NetBuilder::Build(after,candidate))) {wires=std::move(candidate);return true;}
    }
    return false;
}
bool MoveConnectedComponents(std::vector<ComponentInstance>& components,
    std::vector<Wire>& wires,const std::vector<int>& selected,const wxPoint& delta) {
    if(delta==wxPoint(0,0)||selected.empty())return true;
    auto after=components;
    std::set<int> unique(selected.begin(),selected.end());
    for(int ci:unique)if(ci>=0&&ci<static_cast<int>(after.size()))after[ci].pos+=delta;
    if(!ReconnectChangedComponents(components,after,wires))return false;
    components=std::move(after);return true;
}
}

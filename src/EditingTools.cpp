#include "EditingTools.h"
#include <algorithm>
#include <set>
#include <utility>
namespace se {
void MoveConnectedComponents(std::vector<ComponentInstance>& cs,std::vector<Wire>& ws,const std::vector<int>& selected,const wxPoint& delta){
    if(delta==wxPoint(0,0)||selected.empty())return;
    std::set<std::pair<int,int>> moving,nodes;
    std::set<int> selectedIndices;
    for(int ci:selected)if(ci>=0&&ci<(int)cs.size())selectedIndices.insert(ci);
    if(selectedIndices.empty())return;
    for(const auto& c:cs)for(size_t k=0;k<c.pins.size();++k){auto p=PinPosition(c,k);nodes.insert({p.x,p.y});}
    for(int ci:selectedIndices)for(size_t k=0;k<cs[ci].pins.size();++k){auto p=PinPosition(cs[ci],k);moving.insert({p.x,p.y});}
    std::set<std::pair<int,int>> retained;
    for(size_t ci=0;ci<cs.size();++ci)if(!selectedIndices.count((int)ci))
        for(size_t k=0;k<cs[ci].pins.size();++k){auto p=PinPosition(cs[ci],k);if(moving.count({p.x,p.y}))retained.insert({p.x,p.y});}
    int nextId=0;for(const auto& w:ws){nextId=std::max(nextId,w.id);nodes.insert({w.a.x,w.a.y});nodes.insert({w.b.x,w.b.y});}
    std::vector<Wire> pieces;
    for(const auto& w:ws){std::vector<wxPoint> cuts;
        for(auto p:nodes)if(PointOnSegment(wxPoint(p.first,p.second),w.a,w.b))cuts.emplace_back(p.first,p.second);
        std::sort(cuts.begin(),cuts.end(),[&](const wxPoint&a,const wxPoint&b){return w.a.x==w.b.x?a.y<b.y:a.x<b.x;});
        for(size_t k=1;k<cuts.size();++k){Wire part{k==1?w.id:++nextId,cuts[k-1],cuts[k]};pieces.push_back(part);}
    }
    ws.clear();
    for(auto part:pieces){bool aMoved=moving.count({part.a.x,part.a.y}),bMoved=moving.count({part.b.x,part.b.y});
        const bool horizontal=part.a.y==part.b.y;
        if(aMoved)part.a+=delta;if(bMoved)part.b+=delta;
        if(part.a==part.b)continue;
        if(part.IsAxisAligned())ws.push_back(part);
        else {
            // Keep the fixed side's original direction and create an orthogonal elbow.
            wxPoint bend=horizontal?(aMoved?wxPoint(part.a.x,part.b.y):wxPoint(part.b.x,part.a.y)):
                                    (aMoved?wxPoint(part.b.x,part.a.y):wxPoint(part.a.x,part.b.y));
            auto end=part.b;part.b=bend;if(part.a!=part.b)ws.push_back(part);
            if(bend!=end)ws.push_back({++nextId,bend,end});
        }
    }
    // A coincident unselected terminal must retain a path to the moved node even
    // when every original wire endpoint at that coordinate moved with the selection.
    auto retainSegment=[&](const wxPoint& start,const wxPoint& end){
        if(start==end)return;
        for(const auto& wire:ws)if(PointOnSegment(start,wire.a,wire.b)&&PointOnSegment(end,wire.a,wire.b))return;
        ws.push_back({++nextId,start,end});
    };
    for(auto at:retained){
        wxPoint start(at.first,at.second),end=start+delta,bend(end.x,start.y);
        retainSegment(start,bend);retainSegment(bend,end);
    }
    for(int ci:selectedIndices)cs[ci].pos+=delta;
}
}

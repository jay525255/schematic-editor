#pragma once
#include "NetModel.h"
namespace se {
wxString PcbPinNumber(const ComponentInstance& c,size_t pin);
wxString DefaultFootprint(size_t pinCount);
bool IsPcbComponent(const ComponentInstance& c);
// Native KiCad s-expression netlist. Does not convert abstract logic to a physical 74xx IC.
bool BuildKiCadNetlist(const std::vector<ComponentInstance>& components,const NetGraph& graph,
                      wxString& text,wxString& error);
}

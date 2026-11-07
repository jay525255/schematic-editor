#include "CircuitTools.h"
#include <set>
namespace se {
wxString NextReference(const std::vector<ComponentInstance>& components,const wxString& prefix) {
    std::set<wxString> used;
    for(const auto& c:components)used.insert(c.refdes);
    for(int n=1;;++n){auto candidate=prefix+wxString::Format("%d",n);if(!used.count(candidate))return candidate;}
}
}

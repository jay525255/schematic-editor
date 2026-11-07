#pragma once
#include "NetModel.h"
namespace se {
// Reconnect matching terminal indices after a geometric edit. Returns false if
// orthogonal routing would change which original terminals share a network.
// On failure, wires are left unchanged. Callers keep the original components.
bool ReconnectChangedComponents(const std::vector<ComponentInstance>& before,
    const std::vector<ComponentInstance>& after, std::vector<Wire>& wires);
bool MoveConnectedComponents(std::vector<ComponentInstance>& components,
    std::vector<Wire>& wires, const std::vector<int>& selected, const wxPoint& delta);
}

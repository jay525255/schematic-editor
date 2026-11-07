#pragma once
#include "NetModel.h"
namespace se {
// Keep logical terminals connected while changing their world positions.
// The operation splits wires at existing junctions before creating orthogonal paths.
void MoveConnectedComponents(std::vector<ComponentInstance>& components,
    std::vector<Wire>& wires, const std::vector<int>& selected, const wxPoint& delta);
}

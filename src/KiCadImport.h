#pragma once
#include "SchematicIO.h"
namespace se {
struct KiCadImportResult {
    SchematicDocument document;
    std::vector<wxString> warnings;
};
// Transactional: neither result nor the open canvas changes on failure.
bool ImportKiCadSchematic(const wxString& path, KiCadImportResult& result, wxString& error);
}

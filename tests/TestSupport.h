#pragma once
#include "ComponentCatalog.h"
#include "SchematicIO.h"
#include "SimModel.h"
#include <wx/filefn.h>
#include <wx/filename.h>
#include <cstdlib>
#include <iostream>
#include <stdexcept>
inline void require(bool ok, const char* message) {
    if (!ok) throw std::runtime_error(message);
    std::cout << "PASS " << message << std::endl;
}
inline se::ComponentInstance component(const wxString& name, int id, wxPoint at={0,0}) {
    const auto* def=se::ComponentCatalog::Find(name);
    if (!def) throw std::runtime_error("missing builtin component");
    se::ComponentInstance c; c.id=id;c.name=name;c.refdes=def->refPrefix+wxString::Format("%d",id);
    c.pos=at;c.w=def->w;c.h=def->h;c.footprint=def->footprint;c.value=def->value;
    c.pins=se::MakePins(name,def->kind,def->inputs,def->outputs);return c;
}
inline void initModelTests(const char* directory) {
    const auto path=wxString::FromUTF8(directory);
    wxFileName::Mkdir(path,wxS_DIR_DEFAULT,wxPATH_MKDIR_FULL);
    se::ComponentCatalog::SetLibraryPath(wxFileName(path,"components.xml").GetFullPath());
}

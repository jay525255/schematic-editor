#include "SchematicIO.h"
#include "KiCadImport.h"
#include "SimModel.h"
#include "KiCadExport.h"
#include "BuildVersion.h"
#include <clocale>
#include <iostream>
#include <string>
int main(int argc, char** argv) {
    std::setlocale(LC_ALL, "");
    if (argc == 2 && std::string(argv[1]) == "--version") {
        std::cout << "SchematicEditor source " << SE_SOURCE_REV << '\n'; return 0;
    }
    if (argc == 4 && std::string(argv[1]) == "import-kicad") {
        se::KiCadImportResult imported; wxString error;
        if (!se::ImportKiCadSchematic(wxString::FromUTF8(argv[2]),imported,error) ||
            !se::SchematicIO::Save(wxString::FromUTF8(argv[3]),imported.document,&error)) {
            std::cerr << error.utf8_string() << '\n'; return 1;
        }
        for (const auto& warning:imported.warnings) std::cerr << warning.utf8_string() << '\n';
        return 0;
    }
    if (argc != 4 || (std::string(argv[1]) != "export-protel" && std::string(argv[1]) != "export-kicad" && std::string(argv[1]) != "topology")) {
        std::cerr << "Usage: schematic-cli --version | import-kicad input.kicad_sch output.ses | export-kicad design.ses output.net | export-protel design.ses output.net | topology design.ses output.tsv\n"; return 2;
    }
    se::SchematicDocument doc; wxString error;
    if (!se::SchematicIO::Load(wxString::FromUTF8(argv[2]), doc, &error)) {
        std::cerr << error.utf8_string() << '\n'; return 1;
    }
    wxString text;
    auto graph = se::NetBuilder::Build(doc.components, doc.wires);
    if (std::string(argv[1]) == "topology") {
        for (const auto& net : graph.nets) for (const auto& p : net.pins) {
            const auto& c=doc.components[p.compIndex];
            if (se::IsPcbComponent(c)) text+=c.refdes+"\t"+se::PcbPinNumber(c,p.pinIndex)+"\t"+wxString::Format("%d",net.index)+"\n";
        }
    } else if (std::string(argv[1]) == "export-kicad") {
        if (!se::BuildKiCadNetlist(doc.components, graph, text, error)) {
            std::cerr << error.utf8_string() << '\n'; return 1;
        }
    } else text = se::NetBuilder::ToProtel(doc.components, graph);
    if (!se::SchematicIO::WriteTextFile(wxString::FromUTF8(argv[3]), text, &error)) {
        std::cerr << error.utf8_string() << '\n'; return 1;
    }
    return 0;
}

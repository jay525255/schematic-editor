#include "SchematicIO.h"
#include "SimModel.h"
#include "BuildVersion.h"
#include <clocale>
#include <iostream>
#include <string>
int main(int argc, char** argv) {
    std::setlocale(LC_ALL, "");
    if (argc == 2 && std::string(argv[1]) == "--version") {
        std::cout << "SchematicEditor source " << SE_SOURCE_REV << '\n'; return 0;
    }
    if (argc != 4 || std::string(argv[1]) != "export-protel") {
        std::cerr << "Usage: schematic-cli --version | export-protel design.ses output.net\n"; return 2;
    }
    se::SchematicDocument doc; wxString error;
    if (!se::SchematicIO::Load(wxString::FromUTF8(argv[2]), doc, &error) ||
        !se::SchematicIO::WriteTextFile(wxString::FromUTF8(argv[3]),
            se::NetBuilder::ToProtel(doc.components, se::NetBuilder::Build(doc.components, doc.wires)), &error)) {
        std::cerr << error.utf8_string() << '\n'; return 1;
    }
    return 0;
}

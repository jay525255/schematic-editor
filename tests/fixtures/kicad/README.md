# Upstream KiCad schematics

These are complete, unmodified schematics from the official KiCad source mirror.
The filename suffix identifies the release distribution used, not necessarily
its saved file format: KiCad retains older files in newer release trees.

Source: https://github.com/KiCad/kicad-source-mirror (tag `9.0.0`, unless noted).

| Local file | Upstream path | Saved format |
|---|---|---|
| rectifier-9.kicad_sch | demos/simulation/rectifier/rectifier.kicad_sch | 20230121 |
| sallen-key-8.kicad_sch | demos/simulation/sallen_key/sallen_key.kicad_sch (tag 8.0.0) | 20230121 |
| antenna-9.kicad_sch | demos/royalblue54L_feather/nfc_antenna/RoyalBlue54L-NFC-Antenna/RoyalBlue54L-NFC-Antenna.kicad_sch | 20250114 / KiCad 9.0 |
| mirror-9.kicad_sch | qa/data/eeschema/issue18346.kicad_sch | 20240602 / 8.99 |
| pin-to-pin-8.kicad_sch | qa/data/eeschema/erc_multiple_pin_to_pin.kicad_sch | 20231120 / 8.0 |
| dynamic-power-9.kicad_sch | qa/data/eeschema/ERC_dynamic_power_symbol_test.kicad_sch | 20230819; hierarchy rejection test |

Demos are CC BY-SA 4.0; QA schematics fall under KiCad's GPLv3-or-later terms.
See LICENSE.README and the included license texts. Original authors and embedded
properties are retained. The CI oracle additionally writes twelve transformed
copies of the whole antenna demo and asks actual KiCad 9 to export each one.
These derived copies are test artifacts, not replacements for the originals.

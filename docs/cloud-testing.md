# Cloud testing

CI runs on pushes, pull requests, and manual dispatch. The public repository uses
standard windows-2022 and ubuntu-24.04 hosted runners. No paid/larger runner or
self-hosted display is required. Windows compiles the original Visual Studio solution
and CMake tests; Ubuntu builds the same application, runs native mouse/keyboard GUI
checks under Xvfb, and imports the exported netlist in the real KiCad PCB Editor.

Each job uploads logs, JUnit and build products under its exact checkout SHA and run
attempt. build-info.json records commit, binary SHA-256 and workflow URL. status.json
records absent/failed/skipped cases as not passed. GUI failures retain a failure
screenshot when a display is available. A missing display/dependency is an error.
Artifacts expire after 14 days; download a validated run for longer retention.

Acceptance covers file round trips, globally unique references, dense-pin rejection,
connected multi-selection/interior-pin/branch movement, gate and adder truth tables,
and timed propagation. Temporal scenarios assert accumulated gate delays, live input
changes, short-pulse cancellation, independent automatic clocks, conflicting drivers,
and persisted custom truth tables. Native GUI checks operate real menus, shortcuts,
mouse placement/wiring/dragging, copy/paste, delete/undo, save/open, and simulation
run/pause/step/resume/reset. The KiCad test loads the exported native netlist through
PCB Editor's actual GUI, saves the board, and checks every footprint and pad net.
All automated tests execute in Actions, never locally.

The timed digital runtime uses logical ticks (one GUI timer tick is 100 ms); wire
nets are ideal connections and gates have configurable inertial delay. It is not an
analog/SPICE simulator. F5 runs/pauses, F6 pauses and steps once, Shift+F5 resets;
click an input source to change its live value. Document edits clear runtime state.
Models may be named explicitly in the library editor or instance properties.
`truth` tables contain row-major bits without spaces: first input is the low bit,
rows increment from zero, outputs follow pin order. A two-input XOR is `0110`.
Version 1.1 designs preserve model/table/delay/clock period; version 1.0 remains
readable. Unsupported future versions are rejected. Design and library saves use
a temporary output and commit only after successful XML serialization.

Future changes must add focused regressions, be pushed, and have actual cloud results
reviewed. A YAML file or screenshot alone is not acceptance.

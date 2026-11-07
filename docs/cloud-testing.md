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

The baseline intentionally exposes audited defects instead of accepting them as
expected passes. In particular the dense-pin regression and current Protel-to-KiCad
import may fail until their production fixes land. Passing steady-state truth-table
checks does not mean delayed simulation exists. Add temporal assertions with the
future timing model. All automated tests are executed in Actions, not locally.

Future changes must add focused regressions, be pushed, and have actual cloud results
reviewed. A YAML file or screenshot alone is not acceptance.

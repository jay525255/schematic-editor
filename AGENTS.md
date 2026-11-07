# Project development and acceptance

Primary repository: jay525255/schematic-editor. Preserve its mature UI, symbol painter,
component library and file data. Reference repositories are sources of specific ideas,
not replacements or wholesale merge targets.

All automated test execution and acceptance happens in GitHub Actions. Do not block on
local GUI/display restrictions. Every feature/fix must include a focused test in the
same change. Cover core models, regression cases, native GUI input, digital simulation
and real KiCad import as appropriate. Never call an absent, skipped, mocked import or
unexecuted test a pass. Read failed cloud logs, fix the underlying issue and rerun.
Use standard public-repository hosted runners. Do not publish credentials or personal
local data. Keep logs, screenshots, JUnit, source SHA and binary hashes in run artifacts.
If GitHub permissions or a dependency blocks publishing/running, state the exact error
and request the missing access once; do not repeatedly retry the restricted operation.

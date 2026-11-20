# src

Implementation code for the Sirena LwM2M Zephyr client lives here.

Source files, headers, and supporting implementation assets are placed under
`src/` (possibly in subdirectories as the project grows). Generated build
output is never committed; it stays under ignored paths such as `build/`.

Zephyr module build metadata (`CMakeLists.txt`, `zephyr/module.yml`) is
retained at the repository root as expected by `west`; it is not part of this
directory. Those files are introduced by later epics and are not fabricated
here.

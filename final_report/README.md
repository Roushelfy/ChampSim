# Final Report Package

This directory contains the submission-ready final project report for the
15-740 ChampSim project.

- `report.tex`: primary LaTeX source with compact scale-sweep figures and tables.
- `report.pdf`: compiled submission PDF, when generated locally.
- `report.md`: detailed Markdown reference with the full scale-sweep result tables.

The LaTeX report is based on the current result ledger in the repository root
(`report.md`), the final adaptive selector implementation, the Phase 2
OpenEvolve best-point source, poster data, the Phase 1 scale sweep, and the
adaptive-selector commit history.

Primary source artifacts:

- `prefetcher/adaptive_selector/adaptive_selector.cc`
- `prefetcher/adaptive_selector_phase2_best/adaptive_selector.cc`
- `configs/adaptive_selector_config.json`
- `configs/adaptive_selector_2core.json`
- `poster/src/data.js`
- root-level `report.md`

Assumptions used in the report:

- Language: English.
- Package location: `final_report/`.
- Main result framing: Phase 1 final scale sweep is the primary result; Phase 2
  OpenEvolve is a focused 2M+10M diagnostic.
- Collaboration statement: individual contributions are recorded, including
  Zhaofeng Luo leading the final report and poster writing.

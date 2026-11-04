# Chemcraft appearance study — 2026-10-21

## Evidence

- User-supplied reference PNG in this conversation.
- Local Chemcraft export GIFs `C:/Chemcraft/animation_b810_20261021_091007.gif` and `animation_b810_20261021_091735.gif`. First frames were decoded into `.youngkly/chemcraft-study/` for visual inspection.
- `C:/Chemcraft/schemes3/schemdat.txt` marks the user-defined `Mine` scheme with `*`. Its binary settings file exists. Repeated records contain recognizable RGB triples, but field meanings and radius mappings have not been verified through the UI; no guessed binary radius fields were imported.
- The Computer Use helper could not expose a Chemcraft window. No Chemcraft settings screen or camera pose was read through UI automation.
- Official documentation describes display schemes as combinations of atom/bond sizes, colors and lighting, with configurable shadows and higher-quality exports: https://chemcraftprog.com/help/dispmenu.html and https://chemcraftprog.com/help/obtainpics.html.

## Visual findings

The preceding Youngkly version had similar-sized carbon/nitrogen spheres, comparatively thick bonds, and two compact white highlights on most atoms. The supplied reference and inspected Chemcraft exports show smaller carbon beads relative to nitrogen, thin connecting lines, and smooth grey sheen surrounding the focused highlight on dark atoms. These are observations from rendered images, not recovered Chemcraft rendering equations.

## First calibrated implementation

- Retain molecular coordinates, orthographic projection, black carbon, pure red/blue, and pastel mint.
- Keep base atom scale 0.30; apply radius multipliers C 0.70, N 1.12, O 1.03.
- Reduce bond radius from 0.060 to 0.028.
- Use one key light; retain existing soft self-shadow mapping and the 24% shadow control default.
- Specular intensity 0.95, exponent 120 for the focused glint.
- Add a broad specular lobe with exponent 12 and weight 0.08, in linear-light space before the existing display gamma transform. Gate it by the existing atom material coefficient so slender bonds do not receive the broad lobe.
- CMake creates a Youngkly-local shader adapter with checked insertion/include anchors and configure dependencies. The upstream Youngcraft source tree is preserved.

## Comparison and limits

Previous rendering: `.youngkly/bright-render/black-carbon/aldehyde-88f38dda0dde/group-1-row-30.png`.

New rendering: `.youngkly/chemcraft-study/candidate/aldehyde-88f38dda0dde/group-1-row-30.png`.

The before/after Youngkly pair uses the same Gaussian geometry and batch camera. Chemcraft exports show different structures and camera poses, so they support appearance comparison rather than pixel-perfect error measurement. This is an initial visual reconstruction; an exact match still requires rendering the same structure from the same camera in Chemcraft.

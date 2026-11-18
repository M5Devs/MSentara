# MSentara format parsers (placeholder)

This directory will contain importers for the animation formats MSentara
targets, most notably **PMX** (MikuMikuDance model) and **VMD** (motion data).

Nothing is implemented yet. The parsers will be added as a later milestone and
must stay independent of both the rendering layer (`src/platform`) and the UI
(`src/ui`): they translate a file on disk into the plain data structures
declared in `src/core` (`Scene`, `MeshData`, `Action`, `Timeline`).

Planned layout:

```
src/formats/
  pmx/        # PMX model parser (geometry, materials, skeleton, morphs)
  vmd/        # VMD motion parser (bone and morph keyframes)
  common/     # shared binary-reader helpers (LE integers, UTF-16 strings)
```

Until then this file keeps the directory tracked in version control.

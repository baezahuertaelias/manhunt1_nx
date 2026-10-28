# Synthetic NV2A attribute proof

`./scripts/nv2a-backend-proof.sh` builds in the pinned devkitA64 image and runs
through the existing Eden runner. It requires the prepared upstream header at
`game/build/xboxrecomp/src/kernel/nv2a_backend.h`; no game assets are read.
The build consumes the actual `Nv2aVertex` declaration and asserts its layout.
Generated NRO/shader/object files stay local and gitignored.

Two offscreen RGBA8 targets (128×96 and 192×128) receive the same asymmetric
triangle scaled to their dimensions. Input data uses surface x/y, guest z,
rhw, packed AARRGGBB diffuse and UV attributes. The vertex shader reconstructs
homogeneous w=1/rhw, converts pixel coordinates to clip coordinates with a
screen-down Y convention, unpacks colour, and interpolates colour/UV. The
fragment shader mixes UV into red/green to exercise those attributes without
a texture. Depth/stencil, culling and blending are disabled; guest z is
intentionally unused because its mapping is not established by this proof.

CPU verification computes analytical barycentric weights at pixel centres.
For interior pixels, each RGBA channel must be within three quantization
steps of the perspective reference. Most tested interior pixels must differ
from an affine reference by more than twelve steps in at least one channel,
so a constant-w or noperspective substitution cannot pass vacuously. Exterior
pixels must remain the exact asymmetric clear colour. A narrow barycentric
band around edges is excluded to avoid claiming raster fill-rule parity.
Nontrivial interior/exterior population checks prevent an empty draw or all
clear from passing. Surface-wide checks exercise orientation and row layout.

Every case emits `NV2A-BACKEND-CASE PASS|FAIL`; the final marker is
`NV2A-BACKEND-PROOF PASS cases=2` only when both pass. Queue/resource/shader
failures return failure. Pixel buffers remain in memory; no game-derived data
or files are produced. Hardware is UNVERIFIED unless separately exercised.

This is an attribute/coordinate test, not a registered `Nv2aBackend`, a menu
renderer, or a performance result. Textures, alpha/blend/depth/stencil state,
combiner stages, surface switching/coherence and guest synchronization remain
outside its scope. Restore the previous test set by removing this directory
and its script.

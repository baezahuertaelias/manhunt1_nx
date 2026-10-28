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
coordinate-case fragment shader mixes UV into red/green to exercise those attributes without
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
`NV2A-BACKEND-PROOF PASS` only when every enabled case passes. Queue/resource/shader
failures return failure. Pixel buffers remain in memory; no game-derived data
or files are produced. Hardware is UNVERIFIED unless separately exercised.

This is a synthetic attribute/state test, not a registered `Nv2aBackend`, a menu
renderer, or a performance result. Its bounded texture/state extension is
described below; combiner stages, surface switching/coherence and guest
synchronization remain outside its scope. Restore the previous test set by removing this directory
and its script.

## Texture and state extension

The two coordinate cases remain. Ten additional 128×96 cases upload an
already-decoded, row-major 2×2 RGBA8 texture, with asymmetric RGB and alpha
values 127/128/129/255. Incoming UVs are texels, including negative and
out-of-range values; the fragment shader divides by the texture dimensions
exactly once. Nearest sampling exercises NV wrap=1 (repeat) and wrap=3
(clamp-to-edge). No game texture or decoder is used. In particular, the
upstream decoder helper's palette field omission prevents claiming menu P8
support; RGBA8 upload is not evidence for that helper or other formats.

The bounded Nv2aRenderState mapping and tests cover:

- Alpha test GL_GREATER (0x204), reference 128, comparing rounded 8-bit alpha;
  texels on either side and exactly at the threshold are checked.
- GL_FUNC_ADD (0x8006), SRC_ALPHA/ONE_MINUS_SRC_ALPHA (0x302/0x303), for RGB
  and alpha. GL_FUNC_REVERSE_SUBTRACT (0x800B), ONE/ONE, for RGB and alpha.
- Individual NV colour-mask bytes R=0x00010000, G=0x00000100,
  B=0x00000001, A=0x01000000, and combined RG with alpha discard and blending.

The CPU reference computes perspective UVs independently, applies floor and
integer clamp/modulo for nearest texels, compares alpha, evaluates explicit
blend equations, then preserves masked destination channels. Each case must
check all four texels, substantial kept pixels and, when enabled, discarded
pixels. Triangle edges and UV sampling boundaries have small excluded bands;
no fill-rule or sampling-boundary parity is claimed. Five active unsupported
states (wrap, alpha compare, blend factor, blend equation and mask) must be
rejected, with a valid-state positive control. A per-case marker reports
mismatches; final PASS now requires both coordinate cases and all ten state
cases plus rejection checks.

Only the stated modes are tested. Bilinear/anisotropic/mipmap filtering,
other formats, palettes, other alpha compare functions/references, blend
factors/equations/constants, sRGB, depth/stencil/culling, multiple surfaces,
GPU/guest coherence and completion/semaphore ordering remain UNVERIFIED.
This remains a synthetic proof, with no registered backend or menu claim.

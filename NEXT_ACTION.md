# Next action

**RENDER-GPU-STATE-001** (P0): extend the passing synthetic Nv2aVertex GPU proof with asymmetric synthetic texture upload, texel-coordinate normalization, clamp/wrap, alpha discard, blend factors/equations and colour write masks. Compare readback with independent analytical references; build, test and get independent review before any menu-backend registration. Exact prior evidence and remaining interface/state gaps: [RENDER_PLAN.md](reports/RENDER_PLAN.md).

The coordinate/colour/UV/perspective proof passed at two sizes with zero pixel mismatches. Scripted Switch-build menu navigation also passed in Eden. RENDER-001 stays open for guest CPU/wait attribution, repeatable instrumentation-overhead/output checks and remaining state verification. Preserve configurable resolution/aspect/projection for WIDE-002. Hardware, full GPU rendering, audio and gameplay gates remain UNVERIFIED.

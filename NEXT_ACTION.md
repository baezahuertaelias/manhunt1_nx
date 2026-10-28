# Next action

**RENDER-001** (analysis for milestone 3, First image): measure where frame time goes on the Switch build (pushbuffer walk vs CPU rasteriser vs guest code), and evaluate a deko3d back end for xboxrecomp's `nv2a_backend.h` interface, using the methods the menu actually issues (the executor's "unhandled methods" ranking). Write `reports/RENDER_PLAN.md`; create implementation tasks only from it. Exercise menu navigation with a scripted pad (`sdmc:/manhunt.env`, `MANHUNT_PAD_SCRIPT`) at the same time.
The plan must keep render resolution, aspect ratio and projection configurable, so 16:9 (WIDE-002) needs no back-end redesign.

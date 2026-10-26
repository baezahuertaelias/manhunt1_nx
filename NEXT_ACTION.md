# Next action

**FEASIBILITY-001** (analysis for milestone 2, Boot): write `reports/FEASIBILITY.md` covering:

- the path from the entry point `0x001fe908` to the first frame;
- the kernel ordinals and XDK functions reached on that path;
- whether to lift the statically linked D3D/DSOUND/XMV sections or replace them natively (try XboxXBERecomp's XDK 5849 signatures for identification);
- the game's x87 control word;
- the cost of a full lift and link for AArch64.

Create milestone-2 implementation tasks only from that report.

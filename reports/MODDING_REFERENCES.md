# Modding references: widescreen, developer menu, mod menu, MHP

Date: 2026-10-28. Requested by the user: a real 16:9 view, the Rockstar developer menu or a mod menu, and whether the MHP patches can be applied. Tasks: WIDE-001…003, DEVMENU-001/002, MODMENU-001…004, MHP-EVAL-001 (TASK_GRAPH.md).

Everything below is analysis, not an implementation. Where a statement rests only on a README or on strings, it is a **hypothesis**.

## Sources

| Repository | What it is | License |
|---|---|---|
| [ermaccer/Manhunt.PluginMH](https://github.com/ermaccer/Manhunt.PluginMH) (last push 2023-11-21) | ASI plugin for the **PC** release: Rockstar dev menu toggle (F1), custom menu (F2), console, new frontend, first-person mode, weapon adjuster | **none** |
| [Fire-Head/MHP](https://github.com/Fire-Head/MHP) (last push 2023-11-28) | ASI plugin for the **PC** release: PS2-style rendering fixes and display options | **none** |

Neither repository has a license, so all rights are reserved. **No code, address table or shader from them may be copied into this public repository.** They are used only as feature references: what exists in the game and what is worth having. Each feature has to be reimplemented from our own analysis of the Xbox XBE.

## Why the PC plugins cannot be applied as they are

1. **Different binary.** Both plugins patch fixed addresses of the PC executable, for example the PC dev-menu flag at `0x7CF088` and video-mode hooks at `0x4C3D80` and `0x64012A`. The Xbox XBE is a separate build, with a different compiler pass (LTCG) and layout, so none of those addresses carry over. Each target has to be found again in the XBE: from strings, xrefs and the function database.
2. **Different loading mechanism.** They are Win32 DLLs injected by the Ultimate ASI Loader. Our build is a static recompilation with no DLL loading. The hook equivalent here is `game/manual_functions.json`: a guest function replaced by native C, or a call-site patch in the analysis fixes.
3. **Different renderer.** MHP's render fixes replace PC D3D8 states and shaders. Our frames come from the NV2A pushbuffer (today a CPU rasteriser, later a deko3d back end, RENDER-001), so render fixes belong in that back end.
4. **No license.** See above.

Applicable: the **ideas**, reimplemented against Xbox addresses. Not applicable: the code.

## Developer menu: Xbox evidence so far

The PC plugin opens Rockstar's own developer menu by setting one frontend flag, which suggests that the menu code shipped in the retail PC build. In the Xbox `DEFAULT.xbe`, `strings` finds the same family of leftovers. These were checked on 2026-10-28; they are paraphrased here, not copied:

- a god-mode toggle item and its on/off messages;
- weather items for all seven presets PluginMH lists (Free, Clear, Cloudy, Thunder, Rainy, Foggy, Windy);
- debug font size settings, a debug raster initialisation, and console creation and console button names;
- a cheat handler, "are cheats active" logic, and cheat menu navigation items;
- script-level functions to force the weather type and set an entity invulnerable.

**Hypothesis:** the developer menu is compiled into the Xbox build and only its enable path is closed. Whether the menu builder, drawing and input code are complete or stripped is **UNVERIFIED** until DEVMENU-001 traces the xrefs. If the menu is there, most of the requested mod-menu features (god mode, weather, cheats, console) may already exist in it. The custom mod menu (MODMENU-*) then covers only what is missing: infinite health, fall damage, waypoints, a coordinate display and the weapon spawner.

## Widescreen: current state

- The title renders 640×480. The Switch present (`game/src/horizon_compat.c`) scales it to 960×720 and pillarboxes it inside 1280×720. The image keeps its 4:3 shape and is not stretched, but it is not 16:9 either.
- The XBE has no strings for widescreen, aspect ratio or field of view. The title probably ignores the dashboard's widescreen flag (`XGetVideoFlags`); WIDE-001 has to confirm this.
- A real 16:9 view (**Hor+**) needs four things:
  1. a 16:9 render target, so pixels stay square;
  2. a horizontal view extent ×4/3 at the same vertical FOV, in the RenderWare camera view window or projection;
  3. culling that follows the wider frustum;
  4. 2D elements (HUD, menus, text) and FMV kept at their own aspect, not stretched.

  Stretching 4:3 to 16:9 is explicitly not wanted.
- ROADMAP lists 16:9 Hor+ under Release 2. The ROADMAP also says to centralise projection, viewport and render resolution from the start, so RENDER-001 must design for a variable aspect ratio.

## Mod menu feature list (from the PluginMH README)

Wanted by the user:
- **Player:** infinite health, god mode, fall damage on/off, save/load a custom waypoint, player coordinate display.
- **Weapons:** a weapon spawner that gives any weapon *loaded in the current level*, with maximum ammo. Only the weapons a level uses are loaded, which is how the game works.
- **Cheats:** toggle cheats and clear or set the cheats flag. The game uses that flag to lock saving.
- **Weather and world:** weather presets; hiding the stars and moon.
- **Console:** show the console.

Other PluginMH ideas, not requested and not tasked: HUD timer and body count, screenshot mode (freeze, free camera, hidden HUD), first-person mode, a stats page, a skin loader, the weapon adjuster.

Project rules that apply to every mod feature (AGENTS.md "gameplay difference"):
- opt-in and off by default;
- reversible;
- with the menu closed and the switch off, behaviour is identical to the original;
- using a cheat feature sets the game's own cheats flag, so saves are marked as they would be with the original cheat codes.

## MHP: per-feature applicability (preliminary)

The column "On Xbox?" comes from the MHP README's own platform notes. Rows marked UNVERIFIED have to be checked against Xbox frames (MHP-EVAL-001).

| MHP feature | Problem per README | On Xbox? | Way to do it here |
|---|---|---|---|
| PS2 alpha test | PC **and Xbox** RW ports draw alpha worse than PS2 | Yes (README) | Two-pass alpha test in the render back end, opt-in |
| PS2 halos | pickup corona texture not cleared before the wire sphere | UNVERIFIED (depends on Xbox RW state reset) | Guest-level: clear the texture state before the sphere draw |
| Usable object highlighting | light intensity always 0 | **No**: README says PC only, consoles fine | — |
| Crosshair in scope | PC-only static mouse crosshair | **No** | — |
| Skin fallback | wrong default texture after a randomised skin | Likely (game logic); UNVERIFIED | Guest-level fix in model texture selection |
| EnvMap (PS2 style) | differs per platform | Xbox has its own; a PS2 look is optional | Back-end option, low priority |
| PS2 matfx dual pass + player damage (blood/sweat) | palette-swap effect lost in PC **and Xbox** ports | Yes, effect missing | **UNVERIFIED feasibility.** MHP rebuilds textures from palettes; whether Xbox TXDs keep the needed palettes is unknown |
| Lighting with vertex colour, spotlight formula | broken in fixed function, **also on Xbox** | Yes (README) | Per-vertex lighting in the back end's shaders, opt-in |
| PS2 brightness | PC brightness changes gamma and ambient light | UNVERIFIED for Xbox | Check the Xbox brightness code first |
| PS2 CameraFX ("FX ArtFilter") | code present in PC **and Xbox**, disabled | Yes (README) | Guest-level enable, resolution-aware; may also be a dev-menu item (DEVMENU-001) |
| YCbCr (PS2 analog colours) | post-filter | Applicable | Back-end post-process, opt-in |
| MSAA / MSAA RW | NVIDIA driver issues | **No** | MSAA on deko3d is a separate performance decision |
| Display / video mode | PC windowing | **No** | Widescreen is WIDE-* |

Summary: about half of MHP is relevant to the Xbox build, and none of it can be applied as code. The relevant items are PS2-look options. They are **P3 polish** after Beta 1, and the default must stay the original Xbox look.

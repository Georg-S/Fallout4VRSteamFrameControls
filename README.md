# SteamFrameControls — Fallout 4 VR

Disclaimer: Project is pretty much entirely vibe coded

Context-aware controller remapper (F4SEVR plugin) and SteamVR legacy binding for **Valve Steam Frame** controllers in **Fallout 4 VR** (`v1.2.72.0`).

## Features

- Full support for the Steam Frame's extra inputs (Left D-Pad, Bumpers, Left/Right Menu buttons, Right X/Y) under Fallout 4 VR's legacy Oculus Touch emulation.
- Context-aware bindings for **Gameplay**, **Pip-Boy**, **Container / Workbench / Barter**, and **Workshop / Crafting** menus, configurable via `SteamFrameControls.ini`.
- **Map-tab-only stick inversion**: fixes Fallout 4 VR's inverted drag-style map panning on both sticks while keeping STAT / INV / DATA / RADIO list scrolling normal.
- Compatible with any Fallout 4 VR (`v1.2.72.0` + F4SEVR) setup — no mod dependencies required. Tested with the **Gingas Fallout VR Essentials Overhaul** (including `F4VRControlsConfig`, `FallUI VR`, `PipboyTabsVR`, `FRIK`, and `Virtual Holsters`).

## Default Controls (Extra Steam Frame Buttons)

| Button | Gameplay | Pip-Boy | Container / Workbench |
|---|---|---|---|
| **Left D-Pad Left / Right** | Secondary Activate / VATS | Prev / Next Tab | Prev / Next Category Tab |
| **Left D-Pad Up / Down** | Flashlight (hold) / Crouch | Scroll Up / Down | Scroll Up / Down |
| **Left / Right Bumper** | Sprint / Favorites Wheel | Prev / Next Sub-Tab | Switch Player ↔ Container |
| **Right X / Y** | Reload (tap) / Jump | Secondary / Perk Menu | Take All / Sort |
| **Left View / Right Menu** | Pause Menu | Close | Close |

All mappings and stick-inversion options can be customized in `mod/SteamFrameControls/F4SE/Plugins/SteamFrameControls.ini`.

## Installation

1. **Install the F4SEVR plugin**: Install `SteamFrameControls-MO2.zip` with Mod Organizer 2 (or copy `mod/SteamFrameControls/F4SE` into your Fallout 4 VR `Data` folder) and enable it.
2. **Import the SteamVR binding**: In SteamVR → **Settings → Controllers → Manage Controller Bindings → Fallout 4 VR**, switch to **Custom**, import `steam.app.611660_frame_controller.json` (*"Fallout 4 VR - Steam Frame Controls v1.3 (F4SEVR)"*), and click **Save**. Alternatively, copy `steam.app.611660_frame_controller.json` into `%USERPROFILE%\Documents\steamvr\input\imports\` and select it in SteamVR's controller bindings UI.
3. Launch Fallout 4 VR via **F4SEVR** / MO2.

## Building from Source

Requires CMake 3.20+ and Visual Studio 2022 (MSVC x64):

```powershell
.\build.ps1
```

This builds `build/Release/SteamFrameControls.dll`, copies it into `mod/SteamFrameControls/F4SE/Plugins/`, and packages `SteamFrameControls-MO2.zip`.

## License

MIT — see [LICENSE](LICENSE).

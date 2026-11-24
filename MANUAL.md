# Fallout 4 VR — Steam Frame Controls (F4SEVR Mod + SteamVR Binding)

A complete, context-aware controller solution for **Valve Steam Frame** controllers in **Fallout 4 VR** (tested against **Fallout VR Essentials / Gingas Overhaul**).

> **Nothing inside `C:\Fallout VR Essentials` was modified.** Everything lives in this workspace so you can install it cleanly through Mod Organizer 2 and SteamVR.

---

## Why a small F4SEVR mod is needed (not just a JSON binding)

1. **Steam Frame hardware layout** (from SteamVR's `frame_controller_profile.json`):
   - **Left controller**: Thumbstick, **4-way D-Pad** (`dpad_up/down/left/right`), Bumper, Trigger, Grip, View, System.
   - **Right controller**: Thumbstick, **A / B / X / Y** face buttons, Bumper, Trigger, Grip, Menu, System.
2. **Fallout 4 VR's legacy OpenVR limit**:
   - Fallout 4 VR (`steam.app.611660`) only speaks legacy OpenVR (`IVRSystem::GetControllerState`) and emulates **Oculus Touch** (which has no D-Pad, no bumpers, and only 2 face buttons per hand).
   - In Fallout 4 VR's **Container / Workbench Transfer** (`world_ContainerMenu.swf`) and **Barter** (`world_BarterMenu.swf`) menus, switching item category tabs (**ALL / WEAPONS / APPAREL / AID / MISC / JUNK / MODS / AMMO**) is driven by `LTrigger` (Left Trigger) and `RTrigger` (Right Trigger).
   - A pure SteamVR legacy JSON binding is **context-unaware** and cannot safely map **Left D-Pad Right** to **Right Trigger** without either being rejected as a cross-hand legacy binding or firing your gun whenever you tap D-Pad Right in combat.
3. **How `SteamFrameControls` solves this cleanly**:
   - **`steam.app.611660_frame_controller.json`** maps the core Oculus Touch inputs directly, and routes the 9 extra Steam Frame buttons (`D-Pad Left/Right/Up/Down`, `Left/Right Bumpers`, `Right X/Y`, `Right Menu`) onto spare unused legacy bits (`axis3`/`axis4`/`left_axis2_press`) with **zero** conflicts.
   - **`SteamFrameControls.dll`** (F4SEVR plugin) tracks which Fallout 4 VR menu is open via `RE::UI` `MenuOpenCloseEvent` and translates those 9 buttons **context-sensitively** (including cross-hand injection such as Left D-Pad Right → `RTrigger` in Container/Barter/Pip-Boy menus), while leaving `F4VRControlsConfig` (MCM Controls), `FallUI VR`, `PipboyTabsVR`, `FRIK`, and `Virtual Holsters` completely intact.

---

## Default Control Layout

### Core Controls (always active, native Oculus Touch emulation)

| Steam Frame Input | Action |
|---|---|
| **Left Thumbstick** | Move / Menu navigation (kept on `left_axis0` for `PipboyTabsVR` compatibility) |
| **Right Thumbstick** | Turn / Menu scroll |
| **Left Trigger** | Off-hand / Flashlight (hold) / Previous Tab in menus |
| **Right Trigger** | Fire weapon / Next Tab in menus |
| **Left Grip** | Off-hand grab / Fill Bottle |
| **Right Grip** | Reload (tap) / Grenade (hold) |
| **Right A** | Activate / Interact / Take & Store item in menus |
| **Right B** | VATS / Cancel & Close menu |
| **Left View** | Pause Menu |

### Context-Aware Extra Controls (powered by `SteamFrameControls.dll` + `SteamFrameControls.ini`)

| Steam Frame Button | Container / Workbench / Barter Menu | Pip-Boy Menu | Workshop / Crafting | Gameplay (Exploration & Combat) |
|---|---|---|---|---|
| **Left D-Pad Left** | **Previous Category Tab** (`LTrigger`) | **Previous Sub-Tab** (`LTrigger`) | Browse Left | Secondary Activate (`X`) |
| **Left D-Pad Right** | **Next Category Tab** (`RTrigger`) | **Next Sub-Tab** (`RTrigger`) | Browse Right | VATS (`B`) |
| **Left D-Pad Up** | **Scroll List Up** (no accidental pane switch) | **Scroll List Up** | Up / Subcategory | Flashlight (hold) / Off-hand |
| **Left D-Pad Down** | **Scroll List Down** | **Scroll List Down** | Down / Subcategory | **Crouch / Sneak** |
| **Left Bumper** | **Switch to Player Inventory** | **Previous Main Tab** (STAT/INV/DATA/MAP/RADIO) | Prev Page | **Sprint** (saves stick click) |
| **Right Bumper** | **Switch to Container / Workbench** | **Next Main Tab** | Next Page | **Favorites Wheel** |
| **Right X** | **Take All / Store All Junk** (`XButton`) | Drop / Secondary (`X`) | Secondary (`X`) | **Reload** (tap) / Grenade (hold) |
| **Right Y** | **Sort / Inspect** (`YButton`) | Sort / Perk Chart (`Y`) | Secondary (`Y`) | **Jump** (no more stick-up jumping!) |
| **Right Menu** | Close / Back (`B`) | Close / Back (`B`) | Close / Back (`B`) | **Pause Menu** |

Every single mapping above can be customized in **`SteamFrameControls.ini`** without recompiling!

---

## Installation (Step by Step)

### Step 1 — Install the F4SEVR Mod in Mod Organizer 2

1. Open **Mod Organizer 2** (`C:\Fallout VR Essentials\ModOrganizer.exe`).
2. Click **Install a new mod from an archive** (top-left disc icon) and select:
   `SteamFrameControls-MO2.zip` from this folder.
   *(Or manually copy the `mod\SteamFrameControls` folder into `C:\Fallout VR Essentials\mods\`.)*
3. Enable **SteamFrameControls** in the left pane.
4. *(Recommended)* Uncheck the old `FrameInput - Frame Controller v3 (dpad)` mod in MO2 if it is still enabled so there are no stale INI files left around.

### Step 2 — Import the SteamVR Controller Binding (next time your Steam Frame is connected)

1. Turn on your **Steam Frame** and start **SteamVR**.
2. Open **SteamVR Settings → Controllers → Manage Controller Bindings** (or in the SteamVR Dashboard: **Devices → Controller Input**).
3. Select **Fallout 4 VR** (`steam.app.611660`).
4. Switch Active Controller Binding to **Custom**, click **Import** (or copy `steam.app.611660_frame_controller.json` into your SteamVR bindings folder), and load:
   `steam.app.611660_frame_controller.json`
5. Confirm the binding name shows **"Fallout 4 VR - Steam Frame Controls (F4SEVR)"** and click **Save**.

### Step 3 — Launch & Verify

1. Launch **Play Fallout Essentials** (F4SEVR) through MO2.
2. Check the log file created at:
   `Documents\My Games\Fallout4VR\F4SE\SteamFrameControls.log`
   You should see:
   - `[Config] Loaded ... SteamFrameControls.ini`
   - `[OpenVR] Hooked IVRSystem_019 vtable (slots 34/35, role slot 19)`
   - `[Menu] Registered MenuOpenCloseEvent sink`
3. In-game:
   - Open any enemy corpse or workbench transfer menu → **Left D-Pad Left / Right** switches category tabs, **Left D-Pad Up / Down** scrolls cleanly, **Left / Right Bumpers** switch between your inventory and the container, **Right X** takes/stores all!
   - In gameplay → **Right Y** jumps, **Right X** reloads, **Left Bumper** sprints, **Right Bumper** opens Favorites.

---

## Files in This Folder

- `SteamFrameControls-MO2.zip` — Ready-to-install Mod Organizer 2 archive containing `F4SE\Plugins\SteamFrameControls.dll` and `SteamFrameControls.ini`.
- `steam.app.611660_frame_controller.json` — Validated SteamVR legacy binding for `frame_controller` (all paths verified against SteamVR's official `frame_controller_profile.json`).
- `mod\SteamFrameControls\` — Unpacked MO2 mod folder.
- `CMakeLists.txt` & `src\` — Full C++20 source code (`src\main.cpp`, `src\exports.def`) if you ever want to modify or rebuild the DLL.

# XMB Item Hider PSP

<i>Continuation of Frostegater's project: <a href="https://www.gamebrew.org/wiki/XMB_Item_Hider_PSP" target="_blank">https://www.gamebrew.org/wiki/XMB_Item_Hider_PSP</a></i>

XMB Item Hider (aka XrossMediaBar™ Item Hider) plugin for PSP. Upgraded to include option to _completely_ hide XMB categories (not just the menu items _within_ categories). The biggest appeal is the ability to hide the largely unused `Network` and `PlayStation®Network` (PSN) categories. _**(Hiding any other categories beyond those 2 introduces bugs, but is mostly stable — see below for the full bug list.)**_

Tested on:
- 6.61 ARK-4
- 6.61 ARK-5

## Installation:
1\. Download & Extract the `.zip` from [the latest Release](https://github.com/wad11656/XMB-Item-Hider-PSP/releases/latest)<br>

2\. Move the included `.prx` and `.ini` to `<MemoryStick>:/SEPLUGINS` _(non-PSP Go)_ or `<InternalStorage>:/SEPLUGINS` _(PSP Go)_.<br>

&nbsp;&nbsp;&nbsp;&nbsp;3a\. **ARK CFW:** Add `vsh, ms0:/SEPLUGINS/xmbih.prx, on` _(non-PSP Go)_ or `vsh, ef0:/SEPLUGINS/xmbih.prx, on` _(PSP Go)_ to your `<StorageDevice>:/SEPLUGINS/PLUGINS.txt`.<br>

&nbsp;&nbsp;&nbsp;&nbsp;3b\. **Non-ARK CFW:** Add `ms0:/SEPLUGINS/xmbih.prx 1` _(non-PSP Go)_ or `ef0:/SEPLUGINS/xmbih.prx 1` _(PSP Go)_ to your `<StorageDevice>:/SEPLUGINS/VSH.txt`.<br>

4\. Open `xmbih.ini` in a text editor and hide menu items by entering a `1` value, and completely hide Global XMB categories with a `2` value.<br>

5\. Boot your PSP/Reset the VSH (XMB).

## Bonus Features

#### <ins>Start XMB on Memory Stick:</ins>
Set `START_AT_MEMORY_STICK = 1` under `[Global]` in the `xmbih.ini` to make the XMB boot with the cursor on `Game` > `Memory Stick` instead of `Saved Data Utility` / `Game Sharing`.
<br><img width="480" height="272" alt="Image" src="https://github.com/user-attachments/assets/e25a053c-27b9-4be3-a5ac-837bf6f07c43" />

 - This setting **force-hides the UMD `PSP™ Update` item** (the equivalent of setting `UMD_UPDATE = 1` in `xmbih.ini`) to prevent crashes.
#### <ins>Relocate ARK's "Extras" items:</ins>
On ARK CFW, the `Extras` category holds three injected items: `Custom Firmware Settings`, `Plugins Manager`, and `Custom Launcher`. Using flags under `[Global]` in `xmbih.ini`, you can relocate these items:

- **`MOVE_ARK_EXTRAS = 1`**:
  - Moves `Custom Launcher` → `Game`
  - Moves  `Custom Firmware Settings` & `Plugins Manager` → `Settings` (with updated icons)!
  <img width="480" height="272" alt="Image" src="https://github.com/user-attachments/assets/1438e70e-cdcd-49c9-bf82-8bb1e8e2e983" />

## Known Bugs/Limitations:
- You must use the below custom build of `Game Categories Lite` to retain `Game Categories Lite` functionality when XMB categories left of `Game` are hidden through `XMB Item Hider`: 
  - <a href="https://github.com/wad11656/game-categories-lite/releases/latest" target="_blank">@wad11656/game-categories-lite</a> (bundled with the <a href="https://github.com/wad11656/XMB-Item-Hider-PSP/releases/latest" target="_blank">latest Release</a>)
- You can't completely hide the `Settings` or `Extras` categories with `HIDE_ALL_... = 2` in `xmbih.ini` — only their contents (`HIDE_ALL_... = 1`).
  - Safely hide the `Extras` category by changing your `Custom Firmware Settings` > `VSH Region` to one of the following:
<br>`Latin America` `Hong Kong` `Taiwan` `Russia` `China` `Debug I`
- Hiding `System Settings` with `SYSTEM = 1` or `HIDE_ALL_SETTINGS = 1` renders ARK menu items unresponsive.
- When certain combinations of XMB categories are hidden, static UMD preview icons are missing the first time you hover on the UMD (but not on subsequent hovers).
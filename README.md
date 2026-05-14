# XMB Item Hider PSP

XMB Item Hider (aka XrossMediaBar™ Item Hider) plugin for PSP. Upgraded to include option to _completely_ hide XMB categories (not just the menu items _within_ categories). The biggest appeal is the ability to hide the largely unused **Network** and **PlayStation®Network** (PSN) categories.

Continuation of Frostegater's project: https://www.gamebrew.org/wiki/XMB_Item_Hider_PSP

#### <ins>Installation:</ins>
`1.` Download & Extract the `.zip` from [the latest Release](https://github.com/wad11656/XMB-Item-Hider-PSP/releases/latest)<br>
`2.` Move the included `.prx` and `.ini` to `<MemoryStick>:/SEPLUGINS` _(non-PSP Go)_ or `<InternalStorage>:/SEPLUGINS` _(PSP Go)_.<br>
`3a.` **ARK CFW:** Add `vsh, ms0:/SEPLUGINS/xmbih.prx, on` _(non-PSP Go)_ or `vsh, ef0:/SEPLUGINS/xmbih.prx, on` _(PSP Go)_ to your `<StorageDevice>:/SEPLUGINS/PLUGINS.txt`.<br>
`3b.` **Non-ARK CFW:** Add `ms0:/SEPLUGINS/xmbih.prx 1` _(non-PSP Go)_ or `ef0:/SEPLUGINS/xmbih.prx 1` _(PSP Go)_ to your `<StorageDevice>:/SEPLUGINS/VSH.txt`.<br>
`4.` Open the `.ini` file in a text editor and hide icons by entering a `1` value, and completely hide Global XMB categories with a `2` value.<br>
`5.` Boot your PSP/Reset the VSH/XMB.

#### <ins>Known Limitations:</ins>
- You can't completely hide the leftmost `Settings` category with `HIDE_ALL_SETTINGS = 2` in the `.ini` file--only its contents (`HIDE_ALL_SETTINGS = 1`). (The `Settings` category seems to act as the "anchor" for the rest of the categories.)
  - `Settings` *does* get hidden with `HIDE_ALL = 2`.
- Completely hiding any category to the left of the `Game` category can cause buggy behavior with `Game` menu items. (i.e., Duplicated `Memory Stick` entries; Deleted `Resume Game` entries don't properly disappear until the next full VSH reset.)

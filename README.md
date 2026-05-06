# XMB Item Hider PSP

XMB Item Hider (aka XrossMediaBar™ Item Hider) plugin for PSP. Upgraded to include option to _completely_ hide XMB categories (not just the menu items _within_ categories). The biggest appeal is the ability to hide the largely unused **Network** and **PlayStation®Network** (PSN) categories.

Continuation of Frostegater's project: https://www.gamebrew.org/wiki/XMB_Item_Hider_PSP

#### <ins>Known Limitations:</ins>
- You can't completely hide the leftmost `Settings` category with `HIDE_ALL_SETTINGS = 2` in the `.ini` file--only its contents (`HIDE_ALL_SETTINGS = 1`). The `Settings` category seems to act as the "anchor" for the rest of the categories.
  - `Settings` *does* get hidden with `HIDE_ALL = 2`.
- Completely hiding any category to the left of the `Game` category can cause buggy behavior with `Game` menu items. (i.e., Duplicated `Memory Stick` entries; Deleted `Resume Game` entries don't properly disappear until the next full VSH reset.)

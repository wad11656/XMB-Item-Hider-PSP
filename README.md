# XMB Item Hider PSP

XMB Item Hider (aka XrossMediaBar™ Item Hider) plugin for PSP. Upgraded to include option to _completely_ hide XMB categories (not just the menu items _within_ categories). The biggest appeal is the ability to hide the largely unused **Network** and **PlayStation®Network** (PSN) categories.

Continuation of Frostegater's project: https://www.gamebrew.org/wiki/XMB_Item_Hider_PSP

Tested on:
- 6.61 ARK-4
- 6.61 ARK-5

## Installation:
1\. Download & Extract the `.zip` from [the latest Release](https://github.com/wad11656/XMB-Item-Hider-PSP/releases/latest)<br>

2\. Move the included `.prx` and `.ini` to `<MemoryStick>:/SEPLUGINS` _(non-PSP Go)_ or `<InternalStorage>:/SEPLUGINS` _(PSP Go)_.<br>

&nbsp;&nbsp;&nbsp;&nbsp;3a\. **ARK CFW:** Add `vsh, ms0:/SEPLUGINS/xmbih.prx, on` _(non-PSP Go)_ or `vsh, ef0:/SEPLUGINS/xmbih.prx, on` _(PSP Go)_ to your `<StorageDevice>:/SEPLUGINS/PLUGINS.txt`.<br>

&nbsp;&nbsp;&nbsp;&nbsp;3b\. **Non-ARK CFW:** Add `ms0:/SEPLUGINS/xmbih.prx 1` _(non-PSP Go)_ or `ef0:/SEPLUGINS/xmbih.prx 1` _(PSP Go)_ to your `<StorageDevice>:/SEPLUGINS/VSH.txt`.<br>

4\. Open the `.ini` file in a text editor and hide menu items by entering a `1` value, and completely hide Global XMB categories with a `2` value.<br>

5\. Boot your PSP/Reset the VSH (XMB).

## Bonus Features

#### <ins>Start XMB on Memory Stick:</ins>
Set `START_AT_MEMORY_STICK = 1` under `[Global]` in the `xmbih.ini` to make the XMB boot with the cursor on **Memory Stick** in the `Game` category instead of `Saved Data Utility` / `Game Sharing`.
<br><img width="480" height="272" alt="Image" src="https://github.com/user-attachments/assets/e25a053c-27b9-4be3-a5ac-837bf6f07c43" />

 - This setting **force-hides the "UMD Update" item** (the equivalent of setting `UMD_UPDATE = 1` in `xmbih.ini`) in order to prevent crashes.
#### <ins>Relocate ARK's "Extras" items:</ins>
On ARK CFW, the `Extras` category holds three injected items: **Custom Firmware Settings**, **Plugins Manager**, and **Custom Launcher**. Using flags under `[Global]` in `xmbih.ini`, you can relocate these items:

- **`MOVE_ARK_EXTRAS = 1`** — Moves **Custom Launcher → `Game`**, and **Custom Firmware Settings & Plugins Manager → end of `Settings`** (with updated Settings-column icons)!<br><img width="480" height="272" alt="Image" src="https://github.com/user-attachments/assets/1438e70e-cdcd-49c9-bf82-8bb1e8e2e983" />
- **`HIDE_ALL_EXTRAS = 2`** — Mimics ARK CFW when the `Extras` category is absent: Hides `Extras` completely, and moves all three ARK items into `Game`.
  - Introduces bugs, so Fake VSH Region should be used to hide `Extras` instead—see **Known Limitations** below.

## Known Limitations:
- You can't completely hide the leftmost `Settings` category with `HIDE_ALL_SETTINGS = 2` in the `xmbih.ini` file—only its contents (`HIDE_ALL_SETTINGS = 1`). (The `Settings` category seems to act as the "anchor" for the rest of the categories.)
  - `Settings` *does* get hidden with `HIDE_ALL = 2`.
- Completely hiding any category to the left of the `Game` category can cause buggy behavior with `Game` menu items. (i.e., Duplicated `Memory Stick` entries; Deleted `Resume Game` entries don't properly disappear until the next full VSH reset.)
  - Completely hiding the `Extras` category via the plugin adds additional bugs. If you want to hide `Extras`, there is already a safe CFW way to do so: Just change your VSH region to one of the following in your CFW settings:
<br>`Latin America` `Hong Kong` `Taiwan` `Russia` `China` `Debug I`
<br><br><table>
<tr>
  <td colspan="3">
    <div align="center"><h4><ins><b>Known bugs when completely hiding categories left of the "Game" category</b></ins></h4>
    </div>
  </td>
</tr>
<tr>
  <th width="325"><b>Bug</b></th>
  <th><b>Video</b></th>
  <th><b>Status</b></th>
</tr>
<tr>
  <td>Crash when inserting a UMD</td>
  <td>https://youtu.be/UfljThsvCdk</td>
  <td><a href="https://github.com/wad11656/XMB-Item-Hider-PSP/tree/Shift-UMD-entries">🔍 Investigating</a></td>
</tr>
<tr>
  <td><i>(Non-PSP Go)</i> Crash when waking from sleep</td>
  <td>https://youtu.be/cDNEmgeg8wE</td>
  <td><div align="center">➖</div></td>
</tr>
<tr>
  <td><i>(PSP Go)</i> Deleting <code>Resume Game</code><br>doesn't apply until VSH reset</td>
  <td>https://youtu.be/A1_ZhReEZRM</td>
  <td><div align="center">➖</div></td>
</tr>
<tr>
  <td colspan="3"><div align="center"><ins><code>Memory Stick</code> entries get duplicated</ins>
</tr>
<tr>
  <td><i>(PSP Go)</i> <ins>After sleep+wake:</ins>
  <br>
    <ul>
      <li><code>Game</code> > <code>Memory Stick</code> duplicates.</li>
      <li>If <code>HIDE_ALL_EXTRAS = 2</code> in <code>xmbih.ini</code>:
      <code>Memory Stick</code> in the category to the right
      of the <code>Settings</code> category duplicates.</li>
    </ul>
  </td>
  <td>https://youtu.be/v4vyOVKliQ0</td>
  <td><div align="center">➖</div></td>
</tr>
<tr>
  <td><ins>After Memory Stick removal+re-insert:</ins>
  <br>
    <ul>
      <li><code>Game</code> > <code>Memory Stick</code> duplicates.</li>
      <li>If <code>HIDE_ALL_EXTRAS = 2</code> in <code>xmbih.ini</code>:
      <code>Memory Stick</code> in the category to the right
      of the <code>Settings</code> category duplicates.</li>
    </ul>
  </td>
  <td>https://youtu.be/1nz6dfnWh-4</td>
  <td><div align="center">➖</div></td>
</tr>
<tr>
  <td><ins>After entering+exiting USB Mode:</ins>
  <br>
    <ul>
      <li><code>Game</code> > <code>Memory Stick</code> duplicates.</li>
      <li>If <code>HIDE_ALL_EXTRAS = 2</code> in <code>xmbih.ini</code>:
      <code>Memory Stick</code> in the category to the right
      of the <code>Settings</code> category duplicates.</li>
    </ul>
  </td>
  <td>https://youtu.be/dbXEe9qp_v0</td>
  <td><div align="center">➖</div></td>
</tr>
</table>

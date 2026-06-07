# XMB Item Hider PSP

<i>Continuation of Frostegater's project: <a href="https://www.gamebrew.org/wiki/XMB_Item_Hider_PSP" target="_blank">https://www.gamebrew.org/wiki/XMB_Item_Hider_PSP</a></i>

XMB Item Hider (aka XrossMediaBar™ Item Hider) plugin for PSP. Upgraded to include option to _completely_ hide XMB categories (not just the menu items _within_ categories). The biggest appeal is the ability to hide the largely unused `Network` and `PlayStation®Network` (PSN) categories. _**(Hiding any other categories beyond those 2 is experimental—see below for the full bug list.)**_

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

 - This setting **force-hides the UMD `PSP™ Update` item** (the equivalent of setting `UMD_UPDATE = 1` in `xmbih.ini`) in order to prevent crashes.
#### <ins>Relocate ARK's "Extras" items:</ins>
On ARK CFW, the `Extras` category holds three injected items: **Custom Firmware Settings**, **Plugins Manager**, and **Custom Launcher**. Using flags under `[Global]` in `xmbih.ini`, you can relocate these items:

- **`MOVE_ARK_EXTRAS = 1`**:
  - Moves `Custom Launcher` → `Game`
  - Moves  `Custom Firmware Settings` & `Plugins Manager` → `Settings` (with updated icons)!
  <img width="480" height="272" alt="Image" src="https://github.com/user-attachments/assets/1438e70e-cdcd-49c9-bf82-8bb1e8e2e983" />
- **`HIDE_ALL_EXTRAS = 2`**: Mimics ARK CFW when the `Extras` category is absent — Hides `Extras` completely, and moves all 3 ARK items into `Game`.
  - Introduces bugs, so one of the following fake `VSH Region`s should be used to hide `Extras` instead (see **Known Limitations** below for details):
  <br>`Latin America` `Hong Kong` `Taiwan` `Russia` `China` `Debug I`

## Known Bugs/Limitations:
- `PlayStation®Network` is the only category that can be safely hidden **on its own** using the `2` flag in `xmbih.ini`'s `[Global]` section (`HIDE_ALL_PSN = 2`). If you use the `2` flag to hide any other category, you must also hide at least 1 additional `[Global]` category using the `2` flag, or else the XMB will crash.
  - If you hide the `PlayStation®Network` category with `HIDE_ALL_PSN = 2`—but don't also hide `Network` with `HIDE_ALL_NETWORK = 2`—the first few icons in the `Network` category will be temporarily missing. To refresh them, scroll down the `Network` category then scroll back up.
- You can't completely hide the leftmost `Settings` category with `HIDE_ALL_SETTINGS = 2` in `xmbih.ini`—only its contents (`HIDE_ALL_SETTINGS = 1`). (The `Settings` category seems to act as the "anchor" for the rest of the categories.)
  - Hiding `System Settings` with `SYSTEM = 1` or `HIDE_ALL_SETTINGS = 1` renders ARK menu items unresponsive.
  - The entire `Settings` category *does* get hidden with `HIDE_ALL = 2`.
    - This setting completely hides all XMB categories and is strictly experimental!
- Completely hiding any category to the left of the `Game` category can cause some bugs—see the table below for the full list.
  - Completely hiding the `Extras` category via the plugin introduces additional bugs. If you want to hide `Extras`, there is already a safe CFW way to do so: Just change your `Custom Firmware Settings` > `VSH Region` to one of the following:
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
  <td><code>Game Categories Lite</code> plugin doesn't work</td>
  <td> </td>
  <td align="center"><a href="https://github.com/wad11656/game-categories-lite/releases/latest">✅&nbsp;Solved - Use updated plugin</a></td>
</tr>
<tr>
  <td><i>(Non-PSP Go)</i> Crash when inserting a UMD</td>
  <td>https://youtu.be/UfljThsvCdk</td>
  <td align="center">🟡&nbsp;Partially solved: XMB may crash during boot w/ UMD inserted</td>
</tr>
<tr>
  <td>Crash when waking from sleep <i>UNLESS</i> <code>Video</code> is one of the hidden categories</td>
  <td>https://youtu.be/cDNEmgeg8wE</td>
  <td align="center">✅&nbsp;Solved</td>
</tr>
<tr>
  <td>Permanently missing <code>Network</code> category icons (if <code>Network</code> isn't hidden)</td>
  <td>https://youtu.be/KFEfO-UfcC0</td>
  <td><div align="center">➖</div></td>
</tr>
<tr>
  <td>If <code>Network</code> & <code>PlayStation®Network</code> aren't both also hidden, the XMB will boot into one of those categories—to the right of <code>Game</code></td>
  <td>https://youtu.be/HOvTVaQ09Kg</td>
  <td><div align="center">➖</div></td>
</tr>
<tr>
  <td><i>(Non-PSP Go)</i> If <code>Network</code> & <code>PlayStation®Network</code> aren't both also hidden OR <code>Game</code> is the 3rd listed category: Blank UMD preview icons <in> (static-image preview icons only)
  <ul><li>+ Blank bg if <code>HIDE_ALL_EXTRAS = 2</code> in <code>xmbih.ini</code></li></ul>
</td>
  <td><ul><li>https://youtu.be/IgET1a6V7_E (Blank icon)</li>
  <li>https://youtu.be/xTUn423SjPM (Blank icon + bg)</li></ul></td>
  <td><div align="center">➖</div></td>
</tr>
<tr>
  <td><i>(Non-PSP Go)</i> Ejecting/Removing <code>UMD™</code> doesn't fully remove <code>UMD™</code>'s entry from the XMB until VSH reset</td>
  <td>https://youtu.be/yhgnUOTe94M</td>
  <td><div align="center">➖</div></td>
</tr>
<tr>
  <td><i>(PSP Go)</i> Deleting <code>Resume Game</code><br>doesn't apply until VSH reset</td>
  <td>https://youtu.be/A1_ZhReEZRM</td>
  <td><div align="center">➖</div></td>
</tr>
<tr>
  <td colspan="3"><div align="center"><ins>"Memory Stick" entries get duplicated</ins>
</tr>
<tr>
  <td><ins>After sleep+wake:</ins>
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
<tr>
  <td colspan="3"><div align="center"><ins>"UMD" entries get duplicated</ins>
</tr>
<tr>
  <td><ins>After sleep+wake:</ins>
  <br>
    <ul>
      <li><code>UMD™</code> & <code>PSP™ Update</code> duplicate if inside the 2nd listed category</li>
      <li>If <code>HIDE_ALL_EXTRAS = 2</code> in <code>xmbih.ini</code>:
      <code>UMD™</code> & <code>PSP™ Update</code> duplicate</li>
    </ul>
  </td>
  <td>https://youtu.be/r8TsL0e9lPM</td>
  <td><div align="center">➖</div></td>
</tr>
<tr>
  <td><ins>After UMD eject+re-insert:</ins>
  <br>
    <ul>
      <li><code>UMD™</code> & <code>PSP™ Update</code> duplicate if inside the 2nd listed category</li>
      <li>If <code>HIDE_ALL_EXTRAS = 2</code> in <code>xmbih.ini</code>:
      <code>UMD™</code> & <code>PSP™ Update</code> duplicate</li>
    </ul>
  </td>
  <td>https://youtu.be/tjQfxX1yVIw</td>
  <td><div align="center">➖</div></td>
</tr>
</table>

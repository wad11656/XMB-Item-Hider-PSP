# XMB Item Hider PSP

XMB Item Hider (aka XrossMediaBar™ Item Hider) plugin for PSP. Upgraded to include option to _completely_ hide XMB categories (not just the menu items _within_ categories). The biggest appeal is the ability to hide the largely unused **Network** and **PlayStation®Network** (PSN) categories.

Continuation of Frostegater's project: https://www.gamebrew.org/wiki/XMB_Item_Hider_PSP

Tested on:
- 6.61 ARK-4
- 6.61 ARK-5

#### <ins>Installation:</ins>
1\. Download & Extract the `.zip` from [the latest Release](https://github.com/wad11656/XMB-Item-Hider-PSP/releases/latest)<br>

2\. Move the included `.prx` and `.ini` to `<MemoryStick>:/SEPLUGINS` _(non-PSP Go)_ or `<InternalStorage>:/SEPLUGINS` _(PSP Go)_.<br>

&nbsp;&nbsp;&nbsp;&nbsp;3a\. **ARK CFW:** Add `vsh, ms0:/SEPLUGINS/xmbih.prx, on` _(non-PSP Go)_ or `vsh, ef0:/SEPLUGINS/xmbih.prx, on` _(PSP Go)_ to your `<StorageDevice>:/SEPLUGINS/PLUGINS.txt`.<br>

&nbsp;&nbsp;&nbsp;&nbsp;3b\. **Non-ARK CFW:** Add `ms0:/SEPLUGINS/xmbih.prx 1` _(non-PSP Go)_ or `ef0:/SEPLUGINS/xmbih.prx 1` _(PSP Go)_ to your `<StorageDevice>:/SEPLUGINS/VSH.txt`.<br>

4\. Open the `.ini` file in a text editor and hide icons by entering a `1` value, and completely hide Global XMB categories with a `2` value.<br>

5\. Boot your PSP/Reset the VSH (XMB).

#### <ins>Known Limitations:</ins>
- You can't completely hide the leftmost `Settings` category with `HIDE_ALL_SETTINGS = 2` in the `.ini` file--only its contents (`HIDE_ALL_SETTINGS = 1`). (The `Settings` category seems to act as the "anchor" for the rest of the categories.)
  - `Settings` *does* get hidden with `HIDE_ALL = 2`.
- Completely hiding any category to the left of the `Game` category can cause buggy behavior with `Game` menu items. (i.e., Duplicated `Memory Stick` entries; Deleted `Resume Game` entries don't properly disappear until the next full VSH reset.)
  - Completely hiding the "Extras" category adds additional bugs. If you want to hide "Extras", there is already a safe CFW way to do so: Just change your VSH region to one of the following in your CFW settings:
<br>`Latin America` `Hong Kong` `Taiwan` `Russia` `China` `Debug I`
<br><br><table>
<tr>
  <td colspan="3">
    <div align="center"><h4><ins><b>Known bugs when completely hiding Categories left of the "Game" Category</b></ins></h4></div>
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
  <td><i>(PSP Go)</i> Deleting "Resume Game"<br>doesn't apply until VSH reset</td>
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
      <li>Game > "Memory Stick" duplicates.</li>
      <li>If <code>HIDE_ALL_EXTRAS = 2</code> in <code>xmbih.ini</code>,
      "Memory Stick" in the Category to the right
      of the "Settings" Category duplicates.</li>
    </ul>
  </td>
  <td>https://youtu.be/v4vyOVKliQ0</td>
  <td><div align="center">➖</div></td>
</tr>
<tr>
  <td><ins>After Memory Stick removal+re-insert:</ins>
  <br>
    <ul>
      <li>Game > "Memory Stick" duplicates.</li>
      <li>If <code>HIDE_ALL_EXTRAS = 2</code> in <code>xmbih.ini</code>,
      "Memory Stick" in the Category to the right
      of the "Settings" Category duplicates.</li>
    </ul>
  </td>
  <td>https://youtu.be/1nz6dfnWh-4</td>
  <td><div align="center">➖</div></td>
</tr>
<tr>
  <td><ins>After entering+exiting USB Mode:</ins>
  <br>
    <ul>
      <li>Game > "Memory Stick" duplicates.</li>
      <li>If <code>HIDE_ALL_EXTRAS = 2</code> in <code>xmbih.ini</code>,
      "Memory Stick" in the Category to the right
      of the "Settings" Category duplicates.</li>
    </ul>
  </td>
  <td>https://youtu.be/dbXEe9qp_v0</td>
  <td><div align="center">➖</div></td>
</tr>
</table>

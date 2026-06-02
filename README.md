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

#### <ins>Start on Memory Stick:</ins>
Set `START_AT_MEMORY_STICK = 1` under `[Global]` in the `.ini` to make the XMB boot with the cursor on **Memory Stick** in the `Game` category instead of `Saved Data Utility` / `Game Sharing`.

It briefly hides those items (and the UMD disc, if inserted) during boot so the cursor settles on Memory Stick, then re-adds them automatically once the XMB has finished loading — so everything stays accessible. The re-add is tied to a detected "XMB ready" signal (no fixed delay).

While `START_AT_MEMORY_STICK` is enabled it also **force-hides the "UMD Update" item** (the same effect as `UMD_UPDATE = 1`). This is required: booting / resetting the VSH with that item present and a UMD inserted crashes the XMB.

#### <ins>Relocating the "Extras" CFW items:</ins>
On ARK CFW the `Extras` category holds three injected items — **Custom Firmware Settings**, **Plugins Manager**, and **Custom Launcher**. `HIDE_ALL_EXTRAS` can hide `Extras` while keeping those items reachable:

- **`HIDE_ALL_EXTRAS = 2`** — hide `Extras` and move all three items into `Game`.
- **`MOVE_ARK_EXTRAS = 1`** — leave `Extras` visible, but move the ARK-injected items out of it: **Custom Launcher → `Game`**, **Custom Firmware Settings + Plugins Manager → end of `Settings`** (repointed to Settings-column icons). Non-ARK items already in `Extras` are left alone.

`MOVE_ARK_EXTRAS = 1` does not hide `Extras`, so it does not need a fake VSH region. If your firmware region already hides `Extras`, that firmware behavior still applies; the plugin only controls where ARK's own injected items land.

#### <ins>Known Limitations:</ins>
- You can't completely hide the leftmost `Settings` category with `HIDE_ALL_SETTINGS = 2` in the `.ini` file--only its contents (`HIDE_ALL_SETTINGS = 1`). (The `Settings` category seems to act as the "anchor" for the rest of the categories.)
  - `Settings` *does* get hidden with `HIDE_ALL = 2`.
- Completely hiding any category to the left of the `Game` category can cause buggy behavior with `Game` menu items. (i.e., Duplicated `Memory Stick` entries; Deleted `Resume Game` entries don't properly disappear until the next full VSH reset.)
  - Completely hiding the "Extras" category via the plugin adds additional bugs. If you want to hide "Extras", there is already a safe CFW way to do so: Just change your VSH region to one of the following in your CFW settings:
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
  <td><i>(PSP Go)</i> Deleting "Resume Game"<br>doesn't apply until VSH reset</td>
  <td>https://youtu.be/A1_ZhReEZRM</td>
  <td><div align="center">➖</div></td>
</tr>
<tr>
  <td colspan="3"><div align="center"><ins>"Memory Stick" entries get duplicated</ins>
</tr>
<tr>
  <td><i>(PSP Go)</i> <ins>After sleep+wake:</ins>
  <br>
    <ul>
      <li>Game > "Memory Stick" duplicates.</li>
      <li>If <code>HIDE_ALL_EXTRAS = 2</code> in <code>xmbih.ini</code>:
      "Memory Stick" in the category to the right
      of the "Settings" category duplicates.</li>
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
      <li>If <code>HIDE_ALL_EXTRAS = 2</code> in <code>xmbih.ini</code>:
      "Memory Stick" in the category to the right
      of the "Settings" category duplicates.</li>
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
      <li>If <code>HIDE_ALL_EXTRAS = 2</code> in <code>xmbih.ini</code>:
      "Memory Stick" in the category to the right
      of the "Settings" category duplicates.</li>
    </ul>
  </td>
  <td>https://youtu.be/dbXEe9qp_v0</td>
  <td><div align="center">➖</div></td>
</tr>
</table>

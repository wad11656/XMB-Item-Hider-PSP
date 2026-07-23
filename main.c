/*
	XMBIH (XrossMediaBar� Item Hider)
	Copyright (C) 2011, Bubbletune
	Copyright (C) 2011, Total_Noob
	Copyright (C) 2008-2011, CompuPhase
	Copyright (C) 2011, Frostegater
	Copyright (C) 2011, codestation
	Copyright (C) 2011, zer01ne
	
	main.c: XMBIH main code
	
	This program is free software: you can redistribute it and/or modify
	it under the terms of the GNU General Public License as published by
	the Free Software Foundation, either version 3 of the License, or
	(at your option) any later version.

	This program is distributed in the hope that it will be useful,
	but WITHOUT ANY WARRANTY; without even the implied warranty of
	MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
	GNU General Public License for more details.

	You should have received a copy of the GNU General Public License
	along with this program.  If not, see <http://www.gnu.org/licenses/>.
*/

/*
	WARNING: For updating plugin on new core change patch[0] and patch[1].
	Dessasembly vshmain.prx by prxtool (DISASM_w) and stydy ASM code.
*/

#include <pspsdk.h>
#include <pspkernel.h>
#include <pspiofilemgr.h>
#include <pspmscm.h>
#include <psppower.h>
#include <pspusb.h>
#include <pspumd.h>
#include <systemctrl.h>
#include <systemctrl_se.h>
#include <main.h>
#include <pspctrl.h>

/* Set when the user presses any d-pad direction during boot (polled
   directly by boot_focus_thread); stands the boot Memory-Stick park/hold
   down so the user's own navigation is never fought. */
static volatile int boot_user_nav = 0;
/* True while the boot MS park/settle owns Memory-Stick parking; maybe_jump
   stands down until it clears so its msgshare_ms re-add snaps don't fight the
   boot park or the user's own navigation during the settle. */
static volatile int boot_settling = 0;
/* Latched once the cursor has actually reached Game>Memory Stick. Until then
   the boot forces MS and ignores early input (which is forgotten on landing);
   after it, a fresh user press releases the boot holds. */
static volatile int boot_ms_established = 0;
#include <kubridge.h>
#include <string.h>
#include <stdarg.h>
#include "minGlue.h"
#include "minIni.h"

PSP_MODULE_INFO("XMBIH", 0x0007, 1, 3);

static struct {
	volatile unsigned char guard[0x200];
	volatile unsigned char flags[64];
} cfg_store;

#define set cfg_store.flags




/*
 * Hand-rolled libc replacements. Linking against newlib's libc.a on a
 * user-mode plugin pulls in __retarget_lock_*, _ctype_, _sbrk, etc., which
 * cascade into Kernel_Library / ThreadManForUser / sceNetInet imports that
 * the PSP module loader rejects. So we provide the few string helpers we
 * need, and link with -nodefaultlibs (no -lc).
 */
int strcmp(const char *a, const char *b)
{
	while (*a && *a == *b) { a++; b++; }
	return (int)(unsigned char)*a - (int)(unsigned char)*b;
}

char *strcpy(char *d, const char *s)
{
	char *r = d;
	while ((*d++ = *s++)) ;
	return r;
}

unsigned int strlen(const char *s)
{
	const char *p = s;
	while (*p) p++;
	return (unsigned int)(p - s);
}

char *strrchr(const char *s, int c)
{
	const char *last = NULL;
	for (; *s; s++) if (*s == (char)c) last = s;
	if ((char)c == 0) return (char *)s;
	return (char *)last;
}

char *strncpy(char *d, const char *s, unsigned int n)
{
	char *r = d;
	while (n && *s) {
		*d++ = *s++;
		n--;
	}
	while (n) {
		*d++ = 0;
		n--;
	}
	return r;
}

char *strchr(const char *s, int c)
{
	for (; *s; s++) if (*s == (char)c) return (char *)s;
	if ((char)c == 0) return (char *)s;
	return NULL;
}

void *memset(void *dst, int v, unsigned int n)
{
	unsigned char *p = (unsigned char *)dst;
	while (n--) *p++ = (unsigned char)v;
	return dst;
}

void *memcpy(void *dst, const void *src, unsigned int n)
{
	unsigned char *d = (unsigned char *)dst;
	const unsigned char *s = (const unsigned char *)src;
	while (n--) *d++ = *s++;
	return dst;
}

long strtol(const char *s, char **end, int base)
{
	long v = 0;
	int neg = 0;
	while (*s == ' ' || *s == '\t') s++;
	if (*s == '+') s++;
	else if (*s == '-') { neg = 1; s++; }
	if (base == 0) {
		if (*s == '0' && (s[1] == 'x' || s[1] == 'X')) { base = 16; s += 2; }
		else if (*s == '0') { base = 8; s++; }
		else base = 10;
	} else if (base == 16 && *s == '0' && (s[1] == 'x' || s[1] == 'X')) {
		s += 2;
	}
	while (*s) {
		int d;
		if (*s >= '0' && *s <= '9') d = *s - '0';
		else if (*s >= 'a' && *s <= 'z') d = *s - 'a' + 10;
		else if (*s >= 'A' && *s <= 'Z') d = *s - 'A' + 10;
		else break;
		if (d >= base) break;
		v = v * base + d;
		s++;
	}
	if (end) *end = (char *)s;
	return neg ? -v : v;
}

/* AddVshItem here is *not* the literal real AddVshItem; it's whatever the
   JAL at patch[1] points to right before we overwrite it. On stock firmware
   that's the real function; on ARK-5/ARK-4 it's xmbctrl's wrapper (which
   chain-injects the CFW menu items and then forwards to the real one).
   Wrappers calling this pointer therefore preserve xmbctrl's injection. */
int (* AddVshItem)(void *a0, int topitem, SceVshItem *item);
int (* umdIoOpen)(PspIoDrvFileArg* arg, char* file, int flags, SceMode mode);

/* File-scope replacement for the original GCC-nested-function definition
   of umdIoOpenPatched inside PatchVshMain. The isofs driver retains this
   pointer indefinitely; a nested function's address is a stack-resident
   trampoline that gets clobbered when PatchVshMain returns, which on
   modern psp-gcc + ARK-5 causes the PSP to crash the moment a UMD's
   preview is loaded and isofs calls into us. As a top-level function it
   captures nothing from PatchVshMain (it only references globals), so the
   lift is behaviourally identical except it doesn't crash. */
static int start_at_ms_flag;    /* defined further down (START_AT_MEMORY_STICK) */
static int umdIoOpenPatched(PspIoDrvFileArg* arg, char* file, int flags, SceMode mode)
{
	/* START_AT_MEMORY_STICK force-hides the UMD Update item: with it enabled,
	   booting (or resetting the VSH) with a UMD inserted while that item is
	   present crashes the XMB, so we block its PARAM.SFO unconditionally here
	   the same way UMD_UPDATE=1 does.

	   All flags are read from the cache parsed at module_start (set[58]=
	   UMD_UPDATE, set[54]=HIDE_ALL, start_at_ms_flag=START_AT_MEMORY_STICK,
	   set[4]=HIDE_ALL_GAME). We deliberately do NOT call cfg() here: this runs
	   inside isofs's IoOpen hook, and re-opening/parsing the ini off the
	   memory stick from within the storage driver path is needless work (the
	   flags are static for the boot) and best avoided in that context. */
	return (strcmp(file, "/PSP_GAME/SYSDIR/UPDATE/PARAM.SFO") == 0 &&
	        (set[58] || set[54] || start_at_ms_flag || set[4]))
	       ? -1
	       : umdIoOpen(arg, file, flags, mode);
}

/* Once we've force-forwarded one xmbctrl trigger, xmbctrl's items_added
   static flips and the CFW menus are injected. Every subsequent trigger
   item can then go through skip() normally, so the user gets to hide them.
   Without this gate we'd force-forward every trigger forever and lose
   the ability to hide them. */
static int xmbctrl_triggered = 0;

/* Prologue-patch trampoline buffer. We attempt to patch the real
   AddVshItem's first two instructions to `j AddVshItemFilter; nop`. If it
   takes effect, every call -- including the xmbctrl wrapper forwarding the
   trigger item to the real function -- routes through our filter so the
   first trigger can ALSO be hidden. If it doesn't take effect (we observed
   this on v1.3fix2-derived code), the only consequence is that one
   trigger item still shows; the rest of the code still works. */
static u32 add_vsh_trampoline[4] __attribute__((section(".data"), aligned(4))) = { 0, 0, 0, 0 };
int skip(SceVshItem *item, int location);  /* forward decl, defined further down */
static int is_ark_custom_item(const char *text);
static int prepare_topitem_for_item(SceVshItem *item, int incoming_topitem,
	int *out_topitem, const char *source);
static int (*TopcatSelectShiftedFunc)(void *ctx, int adjusted_topitem,
	int original_topitem);
static int (*TopcatPositionFunc)(void *obj, int topitem);
static int (*TrackColumnIconKey)(void *atlas, int topitem, const char *icon_key);
static int (*NetworkDispatchFunc)(void *ctx, int a1, int a2, int a3);
static int (*IconGetTex)(void *buf, void *atlas, void *entry);
static int (*EnsureIconEntryFunc)(void *ctx, void *entry);
static int (*FinalizeIconEntryFunc)(void *ctx, void *entry, int topitem, int slot);
/* vshmain disc-preview "is asset N still needed?" check (FUN_00026334): returns
   *(ctx->e70 + slot[N]) != 0 (0 = load it). For ICON0 (asset 2) that slot comes
   up stale-nonzero in the compacted layout, so the refresh skips the real load
   and the static preview stays blank. */
static int (*DiscAssetNeeded)(void *ctx, int asset);
/* vshmain disc-preview asset clearer (FUN_000290b4): clears the per-asset
   state/pointers under ctx->e70 using the same native teardown path the stock
   preview code takes when rebuilding an asset. */
static int (*ClearDiscAsset)(void *ctx, int asset);
/* vshmain's media-item remover (FUN_00023468): removes every item of one
   relocate-group from one column. Every media-clear/eject path funnels through
   it with HARDCODED NATIVE columns (UMD group 2 from cols 3-6, MS group 3 from
   cols 2-5, ...). Under category compaction the items live at shifted columns,
   so those removals miss them and they linger/duplicate. We wrap it to also
   remove from the shifted column. */
static int (*RemoveMediaByRelocate)(void *ctx, int topitem, int group);
/* paf list-view primitives reused from vshmain's UMD-insert cursor jump
   (FUN_00022bd4). All are runtime-resolved import stubs; the native handler
   only ever jumps to msgshare_umd items, so we reuse the same primitives to
   jump the Game cursor to Memory Stick when it (re)appears post-boot --
   resume from sleep re-adds it, physical reinsert too. The column's list
   object is *(*(obj+0x360) + col*4).
   ListSetRowFunc (0x3F378): snap the list to a row (native calls row,row).
   list_scroll_fwd/back (0x3F230/0x3F798): one animated step; scroll speed
   200.0f passed out-of-band in $f12. list_focus (0x3F640): settle/refresh
   the focused entry, speed 0.0f in $f12. ListFocusCheckFunc (0x3FB90):
   native gate before that focus refresh. */
static int (*ListSetRowFunc)(void *list, int row, int row2);
static int (*ListFocusCheckFunc)(void);
/* vshmain's native "navigate top menu to (column, row)" (FUN_0002240C):
   packs the target into ctx+0xF8 and queues the animated transition via a
   paf deferred callback (event 0x70) -- asynchronous, executed on the UI
   thread. This is the same call the disc-insert flow uses to focus the
   disc's column (log-verified: nav a1=4 a2=0 a3=3 t0=5 at boot). */
static void (*NavigateTopMenuFunc)(void *ctx, int topitem, int row);
/* FUN_0002128C: row of the reloc-group block in a column's model (the disc
   row when called with group 2). Used to recompute the disc-focus row after
   column adjustment. */
static int (*FindMediaRowFunc)(void *ctx, int topitem, int group);
/* FindMediaRowFunc media groups (low byte of an item's relocate field): 2 =
   disc/UMD, 7 = Memory Stick game root (msgshare_ms), 8 = System Storage
   (ef0, "msg_em", PSP Go only). */
#define MS_MEDIA_GROUP 7

/* START_AT cold-boot hold: how many boot-slide bounces to catch before handing
   control back. The slide clamps focus to the rightmost visible column once;
   we release right after correcting it so the user's own navigation is never
   fought. */
#define START_AT_HOLD_BOUNCES 1
static u32 list_scroll_fwd_addr;
static u32 list_scroll_back_addr;
static u32 list_focus_addr;
/* paf's PROPER row snap (paf.prx FUN_0010DD1C, export nid 0xAE2B626E, not
   imported by vshmain -- resolved straight off the loaded paf module):
   clamps the row, sets position AND runs the list relayout + refresh. This
   is what the bare 0x3F378 snap was missing -- snapping with it does not
   leave the recycled cells above the viewport (ARK's injected Game items)
   stale. */
static u32 paf_list_setrow_addr = 0;
static void maybe_jump_game_cursor_to_ms(SceVshItem *item, int topitem, int row);
static int hide_top_category(int index);
static int UmdGameSelectShiftPatched(void *ctx, int topitem);
static volatile u32 vsh_text_addr = 0;
static volatile u32 vsh_seg1_addr = 0;
static volatile u32 vsh_seg1_size = 0;
static volatile u32 icon_layout_atlas = 0;
static volatile int icon_layout_main_mod = -1;
static volatile int icon_layout_shadow_mod = -1;
static volatile int icon_layout_glow_mod = -1;
static volatile u32 icon_layout_prev2_off;
static volatile u32 icon_layout_prev1_off;


/* Used by AddVshItemFilter below (START_AT_MEMORY_STICK), but the rest of the
   feature's state is defined further down. */
static int count_hidden_top_categories_before(int topitem);  /* defined below */
static int adjust_topitem_for_hidden_categories(int topitem);  /* defined below */
static volatile int boot_hide_for_ms = 0;
static SceVshItem captured_ark[5];
static volatile int captured_ark_count = 0;

int AddVshItemFilter(void *a0, int topitem, SceVshItem *item)
{
	int adjusted_topitem = topitem;

	/* Race mitigation: yield per add so our re-add/scroll below never runs
	   ahead of vshmain's list rebuild during Settings/USB transitions. */
	sceKernelDelayThread(15000);

	if (!strcmp(item->text, "msgshare_umd")) {
	}

	/* Items reaching us here either came from xmbctrl forwarding (the
	   trigger item, or its CFW item insertions) or any other caller of
	   the real AddVshItem. Apply the user's hide rules and forward via
	   the trampoline if allowed. Location 0 is the default; the
	   trigger items and CFW item names don't depend on location, so
	   that's fine.
	   Do NOT compact ordinary items here: on ARK this filter sits behind
	   our higher-level wrapper, so normal items reaching this point have
	   already had their topitem adjusted once. Re-adjusting them shifts
	   Game->Video, Video->Music, etc. Only the ARK-injected CFW items need
	   remapping at this layer because they bypass the wrapper. */
	if (is_ark_custom_item(item->text)) {
		if (!prepare_topitem_for_item(item, topitem, &adjusted_topitem, "filter"))
			return 0;

		/* START_AT_MEMORY_STICK: when an ARK CFW item is being rerouted into
		   Game (whether because Extras is hidden or MOVE_ARK_EXTRAS is on),
		   its adjusted_topitem equals Game's displayed index and it would steal
		   the boot cursor. In that case hide + capture it; the worker thread
		   re-adds it at the TOP of Game (before Game Sharing) after the cursor
		   settles on MS. When the item stays in Extras, adjusted_topitem !=
		   Game, so we leave it alone. */
		if (boot_hide_for_ms &&
		    adjusted_topitem == adjust_topitem_for_hidden_categories(5)) {
			int k, dup = 0;
			for (k = 0; k < captured_ark_count; k++)
				if (!strcmp(captured_ark[k].text, item->text)) { dup = 1; break; }
			if (!dup && captured_ark_count < 5) {
				memcpy(&captured_ark[captured_ark_count], item, sizeof(SceVshItem));
				captured_ark_count++;
			}
			return 0;
		}
	}

	if(skip(item, 0)) {
		int (*trampoline)(void *, int, SceVshItem *) =
			(int(*)(void *, int, SceVshItem *))add_vsh_trampoline;
		/* The real AddVshItem (0x22648 on 6.60) returns the row the item
		   landed on -- the native UMD-insert jump consumes it the same way.
		   Every add funnels through this filter, so this is the one spot
		   that reliably sees Memory Stick (re)appear in the Game column. */
		int row = trampoline(a0, adjusted_topitem, item);
		maybe_jump_game_cursor_to_ms(item, adjusted_topitem, row);
		return row;
	}
	return 0;
}
u32 topcat_count_patch;

/* START_AT_MEMORY_STICK=1 in xmbih.ini: hide the fixed Game items that precede
   Memory Stick (Game Sharing, Saved Data Utility, and the UMD disc when
   present) during the boot pass so the XMB cursor lands on Memory Stick.
   A worker thread keeps copies of the hidden items and the add-context
   (container + Game topitem) that vshmain used; once the XMB has finished
   loading and placed the cursor, it re-adds the copies by calling AddVshItem
   directly -- incrementally, the way a real disc/card insert adds an item --
   so the Game row isn't re-scanned/rebuilt and there's no blink.

   "XMB finished loading" is detected by watching the scene object's current
   top-category field (obj+0x338): it reaches the Game column's index
   (start_ms_game_index, computed below) once the XMB settles, just after the
   cursor is placed. We poll for that instead of guessing a delay, so the
   re-add lands at the right moment regardless of boot speed. */
static int start_at_ms_flag = 0;
/* The Game column's displayed top-category index, i.e. the value obj+0x338
   reaches when Game is focused (= our "XMB ready" signal). Game's native
   index is 5, but hiding any category BEFORE Game (Photo/Music/Video/etc.)
   shifts it left, so this is computed at module_start as 5 minus the hidden
   pre-Game categories -- the same shift categories_lite applies. */
static int start_ms_game_index = 5;
static volatile int ms_thread_started = 0;
/* Scene context, captured in the count hook so the worker thread can reach
   the scene object (`*(ctx+0xA6C)`) and poll its "XMB ready" field. */
static volatile u32 scene_ctx = 0;
/* Heap-free copies of the fixed Game items we hide at boot -- Game Sharing,
   Saved Data Utility, and the UMD disc when present (shallow memcpy; their
   context/subtitle pointers reference vshmain's persistent data, same as
   ARK-5's item injection). Re-added directly by the worker thread, via
   AddVshItem, once the XMB is ready. */
static SceVshItem captured_gamedl;
static SceVshItem captured_savedata;
static SceVshItem captured_savedata_ef;    /* PSP Go: System Storage (ef0) Saved Data Utility */
static SceVshItem captured_umd;            /* inserted UMD disc (msgshare_umd) */
static volatile int gamedl_captured = 0;
static volatile int savedata_captured = 0;
static volatile int savedata_ef_captured = 0;
static volatile int umd_captured = 0;
/* captured_ark[] / captured_ark_count moved up (used by AddVshItemFilter). When
   ARK CFW items are rerouted into Game, they land ahead of Memory Stick and
   would otherwise steal the boot cursor, so START_AT hides + captures them too
   and re-adds them at the top of Game once the cursor is placed. */
/* Add-context captured during the boot walk: the container (a0) and Game
   topitem that vshmain passed when adding the fixed items. Reused to re-add
   the captured copies directly (incremental, like a real disc/card insert),
   so we avoid the "media changed" re-scan that causes a visible blink.
   Relies on this container outliving the boot walk -- it does: it's the
   long-lived Game-column list vshmain also adds to on a real disc insert. */
static volatile void *game_a0 = 0;
static volatile int game_topitem = 0;
static volatile int game_ctx_captured = 0;
static volatile void *boot_umd_a0 = 0;
static volatile int boot_umd_topitem = 0;
static volatile int boot_umd_defer_active = 0;
static volatile int boot_umd_defer_captured = 0;
static volatile int boot_umd_defer_thread_started = 0;
static int top_category_hidden_count;
static u32 top_category_runtime_obj;
static SceUID power_callback_uid = -1;
static SceUID power_callback_thread_uid = -1;
static SceUID media_watch_thread_uid = -1;
static SceUID umd_callback_uid = -1;
static SceUID ms_callback_uid = -1;
static volatile int suppress_next_game_ms_add = 0;
static volatile int suppress_next_leading_ms_add = 0;
static volatile int seen_leading_video_umd = 0;
static volatile int suppress_next_game_umd_adds = 0;
static volatile int seen_game_umd_items = 0;
static volatile u32 suppress_umd_adds_until = 0;
static volatile int last_ms_inserted_state = -2;
static volatile int last_usb_state = -1;
/* Wake park (START_AT_MEMORY_STICK): the Memory Stick row cannot be found
   by walking vshmain's item lists -- with Categories Lite the visible MS
   entry is CL's own item ("gcN"/Uncategorized), and the media plugins add
   their cells straight into the paf widget without a vshmain-side node,
   so no vshmain structure maps display rows completely. Instead use the
   invariant the boot park gives us for free: once the boot replay has
   settled, the cursor IS on Memory Stick, so its display row can simply
   be sampled (ms_boot_row). The post-resume worker scrolls back to that
   row after the wake re-adds settle -- the column converges to the same
   boot layout, so the row is the same. */
static volatile int ms_boot_row = -1;
static volatile int wake_park_thread_running = 0;
/* While nonzero, the disc-focus navigate is redirected to Game>Memory Stick
   when START_AT_MEMORY_STICK is set: armed for ~45s at boot and ~15s on
   resume, so the native Video>UMD jump can't steal the park in those
   windows -- but a disc hot-inserted mid-session still jumps natively. */
static volatile u32 ms_park_redirect_until = 0;
/* Latched once the user deliberately navigates to another column during the
   disc-focus redirect window: after that, later disc re-registrations must NOT
   yank the cursor back to Game (reset when the window is re-armed at boot/wake). */
static volatile int user_left_game_during_redirect = 0;
static int wake_park_thread(SceSize args, void *argp);
/* Boot category focus: with categories hidden left of Game, the stock boot
   slide targets native Game=5 resolved against the compacted layout and
   lands clamped on the rightmost visible category; when a disc is present
   the disc-focus (FUN_0002240C) also fires and the two RACE -- boot ends on
   whichever ran last (both observed on hardware). This thread re-asserts
   Game via the same native navigate call after the race settles. Runs
   whenever categories are compacted, regardless of START_AT_MEMORY_STICK
   (stock boots into Game; this just restores that). */
static volatile int boot_focus_thread_started = 0;
static int boot_focus_thread(SceSize args, void *argp);

static int path_is_dir(const char *path);
static int current_umd_is_video(void);
static int preferred_umd_location(const char *text);

static int media_duplicate_risk_active(void)
{
	if (!scene_ctx)
		return 0;

	return hide_top_category(1) ||
	       hide_top_category(2) ||
	       hide_top_category(3) ||
	       hide_top_category(4);
}

static void arm_media_readd_suppression(volatile u32 *until, u32 duration_us)
{
	if (!media_duplicate_risk_active())
		return;

	*until = sceKernelGetSystemTimeLow() + duration_us;
}

static void arm_game_ms_readd_suppression(void)
{
	if (!media_duplicate_risk_active() || !scene_ctx)
		return;

	suppress_next_game_ms_add = 1;
}

static int first_visible_media_location_after_settings(void)
{
	if (!hide_top_category(1))
		return 0;
	if (!hide_top_category(2))
		return 1;
	if (!hide_top_category(3))
		return 2;
	if (!hide_top_category(4))
		return 3;

	return 4;
}

/* Display index (adjusted topitem) of the first visible top category to the
   right of Settings, DISREGARDING Extras. The compaction only ever desyncs
   vshmain's native UMD dedup for this leftmost media column, so it is the only
   column whose UMD adds we suppress. Photo(2)/Music(3)/Video(4) are checked in
   order; if all are hidden the first media column is Game(5). Extras(1) is
   skipped because a UMD never lands there. */
static int first_visible_media_topitem(void)
{
	int native = 5;                          /* Game, if Photo/Music/Video hidden */

	if (!hide_top_category(2))
		native = 2;                      /* Photo */
	else if (!hide_top_category(3))
		native = 3;                      /* Music */
	else if (!hide_top_category(4))
		native = 4;                      /* Video */

	return adjust_topitem_for_hidden_categories(native);
}

static int leading_ms_duplicate_risk_active(void)
{
	int location;

	if (!scene_ctx || !hide_top_category(1))
		return 0;

	location = first_visible_media_location_after_settings();
	return location >= 1 && location <= 3;
}

static void arm_leading_ms_readd_suppression(void)
{
	int location;

	if (!leading_ms_duplicate_risk_active())
		return;

	location = first_visible_media_location_after_settings();
	suppress_next_leading_ms_add = location;
}

static int leading_umd_duplicate_risk_active(void)
{
	return hide_top_category(1) &&
	       first_visible_media_location_after_settings() == 3;
}

static int should_suppress_umd_select_route(int topitem)
{
	int preferred = preferred_umd_location("msgshare_umd");

	if (preferred == 3)
		return leading_umd_duplicate_risk_active() && topitem == 5;

	if (preferred == 4)
		return hide_top_category(4) && topitem == 4;

	return 0;
}

static void reset_leading_umd_seen(void)
{
	if (!seen_leading_video_umd)
		return;

	seen_leading_video_umd = 0;
}

static int game_umd_item_mask(const char *text, int location)
{
	int base = 0;

	if (!strcmp(text, "msgshare_umd"))
		base = 1;
	else if (!strcmp(text, "msg_system_update") ||
		 !strcmp(text, "msgtop_sysconf_update"))
		base = 4;

	if (!base)
		return 0;

	return location == 3 ? base : (base << 1);
}

static int preferred_umd_location(const char *text)
{
	if (strcmp(text, "msgshare_umd"))
		return 4;

	/* Route by disc type. A movie's ONLY add path is the Video/Movie one --
	   vshmain issues no Game add for a movie -- so even when the Video column
	   is hidden the movie must stay on the Video path (preferred 3). When Video
	   is hidden, adjust(4) (Video) and adjust(5) (Game) resolve to the same
	   compacted index, so allowing that video-add lands the movie in Game (it
	   "collapses" in). Returning 4 here -- as a blanket "Video hidden => Game"
	   rule did -- instead suppressed the movie's only add path (loc 3 != 4) and
	   the entry vanished entirely. A game disc uses the Game path (4). */
	switch (current_umd_is_video()) {
	case 1:
		return 3;
	case 0:
		return 4;
	default:
		/* Disc type momentarily unavailable (drive spinning up). When Video is
		   hidden, fall back to the Game route so a single path is still chosen. */
		return hide_top_category(4) ? 4 : 0;
	}
}

static int current_umd_is_video(void)
{
	pspUmdInfo info;

	if (sceUmdCheckMedium() <= 0)
		return -1;

	memset(&info, 0, sizeof(info));
	info.size = sizeof(info);

	if (sceUmdGetDiscInfo(&info) >= 0)
		return (info.type & PSP_UMD_TYPE_VIDEO) != 0;

	if (path_is_dir("disc0:/UMD_VIDEO"))
		return 1;
	if (path_is_dir("disc0:/PSP_GAME"))
		return 0;

	return 0;
}

static void reset_game_umd_seen(void)
{
	if (!seen_game_umd_items)
		return;

	seen_game_umd_items = 0;
}

static void arm_game_umd_readd_suppression(int count)
{
	if (!media_duplicate_risk_active() || !scene_ctx || count <= 0)
		return;

	if (suppress_next_game_umd_adds < count)
		suppress_next_game_umd_adds = count;
}

static int maybe_suppress_media_readd(SceVshItem *item, int location, int topitem)
{
	(void)item;
	(void)location;
	(void)topitem;
	/* Media de-dupe removed for both UMD and Memory Stick: shift-aware removal
	   (RemoveMediaShiftWrap) now clears the stale entry on eject across the
	   native and compacted columns, so re-inserting re-adds it cleanly with no
	   duplicate. The old suppression only blocked the re-add from coming back. */
	return 0;
}

static void mark_boot_umd_replay_seen(int topitem)
{
	/* Only the first visible media column (right of Settings, skipping Extras)
	   is deduped, so the replay only needs to mark that column as seen -- both
	   the Video-path (bit 1) and Game-path (bit 2) bits, so any later walk add
	   landing there is suppressed. */
	if (topitem != first_visible_media_topitem())
		return;

	seen_game_umd_items |= game_umd_item_mask("msgshare_umd", 3) |
	                       game_umd_item_mask("msgshare_umd", 4);
}

/* The boot replay re-adds the captured UMD via AddVshItem directly, which
   bypasses maybe_suppress_media_readd(). If the normal boot walk already added
   the deduped first-media-column entry (its per-column seen-bit is set),
   replaying would duplicate it. */
static int boot_umd_already_present(void)
{
	return (seen_game_umd_items &
	        (game_umd_item_mask("msgshare_umd", 3) |
	         game_umd_item_mask("msgshare_umd", 4))) != 0;
}

static int should_preserve_network_topitem(int incoming_topitem, int adjusted_topitem,
	SceVshItem *item)
{
	(void)incoming_topitem;
	(void)adjusted_topitem;
	(void)item;

	/* DISABLED. Preserving the original Network topitem (6) made AddVshItem add
	   the items at a column that no longer exists once pre-Game categories are
	   compacted away: with N categories hidden, the visible layout only has
	   (8 - N) columns (valid indices 0..7-N), so the native Network index lands
	   out of range and vshmain silently drops every Network item -- Network
	   appears empty. An item must be added at its in-range, adjusted topitem to
	   render at all, so the column position cannot be used to keep the "native
	   resource path". Any icon fix has to keep the adjusted topitem and address
	   the atlas separately (e.g. remap each item's image keys, or the tail-entry
	   preserve in PatchTopCategories). */
	return 0;
}

/* The item-add wrappers must pass the compacted topitem into AddVshItem or the
   shifted Network column is dropped as out-of-range. Later in vshmain, though,
   the icon tracker at 0x2edbc still special-cases Network by its native slot 6
   to queue the column's icon keys for loading. When Network shifts left, that
   helper sees the adjusted slot (4/3/...) and skips the queueing work, leaving
   the descriptors' loaded-texture pointer at 0. Remap only that helper's
   topitem back to native Network so the existing preload path still runs. */
static int TrackColumnIconKeyPatched(void *ctx, int topitem, const char *icon_key)
{
	int native_network_topitem;
	int tracked_topitem = topitem;
	int ret;

	if (!hide_top_category(6)) {
		native_network_topitem = 6;
		if (count_hidden_top_categories_before(native_network_topitem) > 0 &&
		    topitem == adjust_topitem_for_hidden_categories(native_network_topitem)) {
			tracked_topitem = native_network_topitem;
		}
	}


	ret = TrackColumnIconKey(ctx, tracked_topitem, icon_key);

	return ret;
}

static int power_callback(int unknown, int powerInfo, void *arg)
{
	(void)arg;
	sceKernelDelayThread(15000);   /* race mitigation */

	if (powerInfo & (PSP_POWER_CB_RESUMING | PSP_POWER_CB_RESUME_COMPLETE)) {
		/* Do NOT reset_game_umd_seen() on resume. A sleep/wake keeps the
		   existing UMD entry in its column (the disc never changed), but vshmain
		   re-walks the add path on resume. Clearing the seen-mask let those
		   re-adds through, duplicating the entry that survived the sleep. Leave
		   the mask set so maybe_suppress_media_readd() swallows the resume
		   re-walk -- the same way the Memory Stick path relies on
		   suppress_next_game_ms_add persisting across resume. A genuine disc
		   change still clears the mask via reset_game_umd_seen() in
		   umd_callback(). */
		arm_leading_ms_readd_suppression();
		arm_game_ms_readd_suppression();
		arm_media_readd_suppression(&suppress_umd_adds_until, 3000000);
		if (sceUmdCheckMedium() > 0)
			arm_game_umd_readd_suppression(2);
	}
	/* Don't yank the cursor to Game>Memory Stick on wake while a submenu /
	   context menu is open -- that strands the overlay (the user can't dismiss
	   it). scene_ctx+0x14C's bit 0 is vshmain's "bare XMB is the foreground"
	   flag: SET (…0001) while resting on a plain column, CLEAR (…0000) while a
	   submenu, context menu, Settings sub-page, or applet is up (hardware-diffed
	   awake AND at wake; it reverts on close -- unlike the 0xE2C counter, and it
	   covers more than the narrower 0x128 submenu flag). Skip the wake park
	   while an overlay is up; a normal wake parks as usual. NOTE: ARK CFW's own
	   custom menus (e.g. its Update menu) are NOT vshmain overlays and don't
	   clear this bit, so they're not covered here. */
	if (powerInfo & PSP_POWER_CB_RESUME_COMPLETE)
	if ((powerInfo & PSP_POWER_CB_RESUME_COMPLETE) && start_at_ms_flag &&
	    !wake_park_thread_running &&
	    !(scene_ctx && !(*(u32 *)((char *)scene_ctx + 0x14C) & 1))) {
		SceUID tid = sceKernelCreateThread("xmbih_wakepark",
			wake_park_thread, 0x18, 0x1000, 0, NULL);
		/* Same disc-present gate as the boot window: only a disc that is
		   in the drive AT WAKE has its re-registrations redirected; a
		   disc inserted after a disc-less wake jumps natively. */
		if (sceUmdCheckMedium() > 0) {
			ms_park_redirect_until =
				sceKernelGetSystemTimeLow() + 15000000;
			user_left_game_during_redirect = 0;
		}
		if (tid >= 0) {
			wake_park_thread_running = 1;
			sceKernelStartThread(tid, 0, NULL);
		}
	}

	return 0;
}

static int umd_callback(int unknown, int event, void *arg)
{
	(void)unknown;
	(void)arg;

	if (event & (PSP_UMD_NOT_PRESENT | PSP_UMD_CHANGED)) {
		reset_leading_umd_seen();
		reset_game_umd_seen();
		arm_media_readd_suppression(&suppress_umd_adds_until, 3000000);
		arm_game_umd_readd_suppression(2);
	}

	return 0;
}

static int ms_callback(int unknown, int event, void *arg)
{
	sceKernelDelayThread(15000);   /* race mitigation */
	(void)unknown;
	(void)arg;

	if (event == MS_CB_EVENT_EJECTED) {
		arm_leading_ms_readd_suppression();
		arm_game_ms_readd_suppression();
	}
	else if (event == MS_CB_EVENT_INSERTED) {
		arm_leading_ms_readd_suppression();
		arm_game_ms_readd_suppression();
	}

	return 0;
}

static int media_watch_thread(SceSize args, void *argp)
{
	(void)args;
	(void)argp;

	while (1) {
		int inserted = MScmIsMediumInserted();
		int usb_state = sceUsbGetState();
		int usb_drop = (last_usb_state != 0x221 && usb_state == 0x221);
		int usb_rise = (last_usb_state == 0x221 && usb_state != 0x221);
		if (inserted != last_ms_inserted_state || usb_state != last_usb_state) {
		}

		if (last_ms_inserted_state >= -1 && inserted != last_ms_inserted_state) {
			if (inserted <= 0) {
				arm_leading_ms_readd_suppression();
				arm_game_ms_readd_suppression();
			}
			else {
				arm_leading_ms_readd_suppression();
				arm_game_ms_readd_suppression();
			}
		}
		else if (scene_ctx && inserted > 0 && usb_drop) {
			arm_leading_ms_readd_suppression();
			arm_game_ms_readd_suppression();
		}
		else if (scene_ctx && inserted > 0 && usb_rise) {
			arm_leading_ms_readd_suppression();
			arm_game_ms_readd_suppression();
		}

		last_ms_inserted_state = inserted;
		last_usb_state = usb_state;
		sceKernelDelayThreadCB(200000);
	}

	return 0;
}

static int power_callback_thread(SceSize args, void *argp)
{
	int slot;

	(void)args;
	(void)argp;

	power_callback_uid = sceKernelCreateCallback("xmbih_power", power_callback, NULL);
	if (power_callback_uid < 0) {
		return 0;
	}

	slot = scePowerRegisterCallback(-1, power_callback_uid);
	if (slot < 0)
		return 0;

	umd_callback_uid = sceKernelCreateCallback("xmbih_umd", umd_callback, NULL);
	if (umd_callback_uid >= 0)
		sceUmdRegisterUMDCallBack(umd_callback_uid);

	ms_callback_uid = sceKernelCreateCallback("xmbih_ms", ms_callback, NULL);
	if (ms_callback_uid >= 0)
		MScmRegisterMSInsertEjectCallback(ms_callback_uid);

	while (1)
		sceKernelSleepThreadCB();

	return 0;
}

static void start_power_callbacks(void)
{
	if (power_callback_thread_uid >= 0)
		return;

	power_callback_thread_uid = sceKernelCreateThread(
		"xmbih_powercb",
		power_callback_thread,
		0x11,
		0x1000,
		0,
		NULL);
	if (power_callback_thread_uid < 0) {
		return;
	}

	sceKernelStartThread(power_callback_thread_uid, 0, NULL);

	if (media_watch_thread_uid >= 0)
		return;

	media_watch_thread_uid = sceKernelCreateThread(
		"xmbih_mediawatch",
		media_watch_thread,
		0x10,
		0x1000,
		0,
		NULL);
	if (media_watch_thread_uid < 0) {
		return;
	}

	sceKernelStartThread(media_watch_thread_uid, 0, NULL);
}

static int should_defer_boot_umd_add(void)
{
	if (start_at_ms_flag)
		return 0;

	if (hide_top_category(4))
		return 0;

	return hide_top_category(1) ||
	       hide_top_category(2) ||
	       hide_top_category(3);
}

static int boot_umd_readd_thread(SceSize args, void *argp)
{
	int i;

	(void)args;
	(void)argp;

	/* Narrow experiment: only delay the initial boot/VSH-reset UMD add while
	   the compacted top-category layout settles, then replay the exact item
	   through the normal AddVshItem path. */
	for (i = 0; i < 40; i++) {
		u32 obj = 0;

		if (scene_ctx)
			obj = *(u32 *)(scene_ctx + 0xA6C);
		if (obj && *(int *)(obj + 0x334) > 0)
			break;

		sceKernelDelayThread(25000);
	}

	sceKernelDelayThread(500000);

	boot_umd_defer_active = 0;
	if (boot_umd_defer_captured && boot_umd_a0) {
		if (boot_umd_already_present()) {
		} else {
			mark_boot_umd_replay_seen(boot_umd_topitem);
			AddVshItem((void *)boot_umd_a0, boot_umd_topitem, &captured_umd);
		}
	}

	return 0;
}

static int maybe_defer_boot_umd_add(void *a0, int topitem, SceVshItem *item)
{
	SceUID tid;

	if (!boot_umd_defer_active || boot_umd_defer_captured)
		return 0;

	if (strcmp(item->text, "msgshare_umd"))
		return 0;

	memcpy(&captured_umd, item, sizeof(SceVshItem));
	boot_umd_a0 = a0;
	boot_umd_topitem = topitem;
	boot_umd_defer_captured = 1;

	if (!boot_umd_defer_thread_started) {
		boot_umd_defer_thread_started = 1;
		tid = sceKernelCreateThread("xmbih_boot_umd",
			boot_umd_readd_thread, 0x18, 0x1000, 0, NULL);
		if (tid >= 0)
			sceKernelStartThread(tid, 0, NULL);
	}

	return 1;
}

static int path_is_dir(const char *path)
{
	SceUID fd;

	fd = sceIoDopen(path);
	if (fd < 0)
		return 0;

	sceIoDclose(fd);
	return 1;
}

static int maybe_disable_start_at_ms_from_disc_layout(SceVshItem *item, int location)
{
	(void)item;
	(void)location;
	/* An inserted UMD (movie or game) must NOT disable START_AT_MEMORY_STICK or
	   disturb the ARK item ordering -- it only disables the UMD Update entry,
	   which is handled separately. So park-on-Memory-Stick stays active
	   regardless of the disc0 layout. (Previously a pure movie disc cleared the
	   flag here, which is what broke ARK ordering + start_ms with a disc in.) */
	return 0;
}

static void maybe_disable_start_at_ms_for_movie_boot(SceVshItem *item, int location,
	int topitem)
{
	(void)item;
	(void)location;
	(void)topitem;
	/* Keep START_AT_MEMORY_STICK active even when a movie UMD is present and
	   routes into the Movie column. The movie entry still lands in Movie (handled
	   elsewhere); start_ms must not be cleared, or ARK ordering + the MS park
	   target break whenever a disc is inserted. */
}

static void refresh_current_game_top_after_replay(void)
{
	u32 obj = 0;

	if (!scene_ctx)
		return;

	obj = *(u32 *)(scene_ctx + 0xA6C);
	if (!obj)
		return;

	if (*(int *)(obj + 0x338) != game_topitem)
		return;

	/* Don't run the select if we're returning from a crashed game (ctx+0xE74
	   set, error dialog up) -- it would resume the XMB behind the dialog.
	   Same guard as xmb_interactive(), inlined (defined later). */
	if (*(unsigned char *)((char *)scene_ctx + 0xE74)) {
		return;
	}

	UmdGameSelectShiftPatched((void *)scene_ctx, 5);
}

/* The paf scroll/settle functions take their speed as a float in $f12, out of
   band from the integer args (mtc1 at the native callsites). Loading $f12
   right before the call is safe here: no float code in between for GCC to
   schedule against, and volatile asm keeps its order relative to calls. */
static void set_f12(u32 bits)
{
	asm volatile("mtc1 %0, $f12" : : "r"(bits));
}

/* True when it is safe for our async ops (navigate / scroll / select) to
   touch the XMB. The one case where it is NOT is right after a game crashes
   back to the XMB: a bad EBOOT ("The game could not be started (8002013C)")
   leaves an error dialog up and re-runs our START_AT boot machinery, whose
   navigate/select would resume the XMB behind the still-open modal and hand
   it input (circle stops closing the dialog).

   Discriminator: ctx+0xE74 is vshmain's game-launch-context byte -- set to 1
   by the game-launch teardown (FUN_00020368 / FUN_0001D518, which also wipe
   the media columns and stash the launcher id at 0xe78). After a crash-back
   it is still 1 while the dialog is up; on a genuine cold boot or a normal
   sleep/wake it is 0. (Hardware-confirmed: the vanilla config, where no
   START_AT machinery runs, has no bug; and the XMB render refcount
   DAT_00006198 was ruled out -- it reads 1 in both states.) So: act only
   when NOT returning from a game. */
static int xmb_interactive(void)
{
	if (!scene_ctx)
		return 0;

	if (*(unsigned char *)((char *)scene_ctx + 0xE74)) {
		return 0;
	}
	return 1;
}

/* When set, scroll_game_col_to_row moves row-by-row via the native single-step
   animation instead of the bulk paf snap. The snap bulk-relayouts the list and
   skips vshmain's per-row focus update, so a preview bound to the row we leave
   (Game>Resume Game's screenshot) is stranded as a ghost and the icons never
   get their render pass. Stepping fires the same focus update a d-pad press
   does, tearing the old preview down and rendering as it goes. Used by the wake
   park (small moves); boot / re-add keep the snap. */
static int scroll_step_only = 0;

/* Scroll the Game column's paf list to a row, mirroring the native
   UMD-insert jump (FUN_00022bd4): snap when 2+ rows away, animate the last
   step at the native 200.0f speed, settle the focused entry when Game is
   the current column. dispcol is Game's DISPLAYED index. */
static void scroll_game_col_to_row(int dispcol, int row)
{
	u32 obj, arr, list;
	int cur, i;

	/* Race mitigation: this walks and pokes the live per-column list via paf.
	   If it runs while vshmain is rebuilding that list (e.g. entering Bluetooth
	   device settings, USB-mode exit), it can touch half-built state and crash.
	   Small yields here let the rebuild settle first -- confirmed on hardware
	   (the crash only reproduces without them). */
	sceKernelDelayThread(15000);
	if (row < 0 || row > 31)
		return;
	if (!scene_ctx || !ListSetRowFunc || !TopcatPositionFunc)
		return;
	if (!xmb_interactive())
		return;          /* dialog up / XMB suspended: don't touch it */

	obj = *(u32 *)(scene_ctx + 0xA6C);
	if (!obj)
		return;
	arr = *(u32 *)(obj + 0x360);
	if (!arr)
		return;
	list = *(u32 *)(arr + dispcol * 4);
	if (!list)
		return;
	sceKernelDelayThread(15000);   /* race mitigation (see top of function) */

	cur = TopcatPositionFunc((void *)obj, dispcol);
	sceKernelDelayThread(15000);   /* race mitigation (see top of function) */
	if (cur == row)
		return;          /* already parked; keep re-checks free */
	{
		/* Multi-row moves SNAP via paf's set-row-with-relayout (see
		   paf_list_setrow_addr): unlike the bare 0x3F378 snap that left
		   ARK's cells stale, this one relays the list natively -- no
		   dpad-walk feel. Adjacent moves keep the native single-step
		   animation. The step loops below double as verification and
		   fallback: if the snap landed, they no-op. */
		if (!scroll_step_only && paf_list_setrow_addr &&
		    (cur - row < -1 || cur - row > 1))
			((void (*)(void *, int))paf_list_setrow_addr)(
				(void *)list, row);
		for (i = 0; i < 24; i++) {
			if (TopcatPositionFunc((void *)obj, dispcol) >= row)
				break;
			set_f12(0x43480000);   /* 200.0f, native scroll speed */
			((int (*)(void *, int))list_scroll_fwd_addr)((void *)list, 0);
		}
		for (i = 0; i < 24; i++) {
			if (TopcatPositionFunc((void *)obj, dispcol) <= row)
				break;
			set_f12(0x43480000);
			((int (*)(void *, int))list_scroll_back_addr)((void *)list, 0);
		}
	}

	if (*(int *)(obj + 0x338) == dispcol &&
	    ListFocusCheckFunc && ListFocusCheckFunc()) {
		set_f12(0);            /* 0.0f: settle instantly */
		((int (*)(void *))list_focus_addr)((void *)list);
	}
}

/* Read the SceVshItem sitting at display row `row` of top column `col` by
   walking vshmain's own per-column item list -- the same structure the real
   AddVshItem (0x22648) walks: sentinel pointer at ctx + col*0xC + 0xE88, nodes
   linked at +4, node's SceVshItem at +8. Every pointer is range-checked to PSP
   user RAM before deref, so a wrong base/index just yields NULL (caller falls
   back to the snap) rather than risking a wake-time crash. */
static SceVshItem *game_row_item_at(int col, int row)
{
	u32 sent, node;
	int i;

	if (!scene_ctx || col < 0 || row < 0)
		return 0;
	/* sentinel NODE (per AddVshItem @0x22660); first real node is sent->next
	   (+4); node's SceVshItem at +8; list is circular (ends back at sent). */
	/* VALID_PTR: aligned and, ignoring the cached/uncached mirror bits
	   (& 0x1FFFFFFF), inside PSP user RAM. Upper bound is 0x0C800000 -- the
	   Go/2000/3000 have 64MB (real sentinels land at ~0x0A00xxxx), so the old
	   32MB cap of 0x0A000000 wrongly rejected them. Accepts 0x088. (cached)
	   and 0x488. (uncached). */
#define VALID_PTR(p) (((p) & 3) == 0 && ((p) & 0x1FFFFFFF) >= 0x08000000 \
	&& ((p) & 0x1FFFFFFF) < 0x0C800000)
	sent = *(u32 *)(scene_ctx + (u32)col * 0xC + 0xE88);
	if (!VALID_PTR(sent))
		return 0;
	node = *(u32 *)(sent + 4);
	for (i = 0; i <= row && i < 64; i++) {
		if (!VALID_PTR(node))
			return 0;
		if (node == sent)   /* wrapped past the last item: row out of range */
			return 0;
		if (i == row) {
			u32 item = *(u32 *)(node + 8);
			if (!VALID_PTR(item))
				return 0;
			return (SceVshItem *)item;
		}
		node = *(u32 *)(node + 4);
	}
	return 0;
#undef VALID_PTR
}

/* Is the Game-column item at `row` the suspended-game "Resume Game" entry?
   Its text id isn't in vshmain's strings and it persists across sleep (never
   re-added), so we read it live off the list and match the id. A miss just
   returns 0. */
static int game_row_is_resume(int col, int row)
{
	SceVshItem *it = game_row_item_at(col, row);

	if (!it)
		return 0;
	/* Confirmed on hw: the suspended-game "Resume Game" entry is
	   msg_game_hibernation (id 30, relocate low byte 0x06). */
	if (!strcmp(it->text, "msg_game_hibernation"))
		return 1;
	return 0;
}

/* Is the Game-column item at `row` a Memory Stick entry, by identity? The
   media group (relocate low byte) is 7 for BOTH a plain msgshare_ms item and
   Categories Lite's named gcv MS category. Reads the item live off the list --
   no dependency on FindMediaRowFunc, which lags the wake media load on some Go
   units and returns "not found" while a group-7 MS is still registering. */
static int game_row_is_ms(int col, int row)
{
	SceVshItem *it = game_row_item_at(col, row);
	return it && (it->relocate & 0xFF) == MS_MEDIA_GROUP;
}

/* Walk the Game column top-to-bottom and return the row of the FIRST (topmost)
   Memory Stick item (media group 7), or -1 if none is registered yet. This is
   our own group-7 lookup, used in preference to FindMediaRowFunc because the
   latter is unreliable on the Go wake (returns -1 for a stretch even though a
   gcv/msgshare_ms group-7 entry is present in the list). */
static int find_ms_row_by_walk(int col, int count)
{
	int r;
	if (count > 32)
		count = 32;
	for (r = 0; r < count; r++)
		if (game_row_is_ms(col, r))
			return r;
	return -1;
}


/* START_AT_MEMORY_STICK: park the Game cursor back on Memory Stick whenever
   the msgshare_ms item (re)appears in the Game column after boot. Sleep/wake
   ejects the MS/UMD entries and re-adds them on resume, which left the cursor
   wherever it was; a physical MS reinsert behaves the same. Mirrors the
   native UMD-insert jump (FUN_00022bd4): AddVshItem already returned the row
   the item landed on, so snap the paf list there when it's 2+ rows away,
   animate the last step at the native 200.0f speed, and settle the focused
   entry when Game is the current column. When another column is current, the
   reposition happens in the background, same as the boot-time park. Runs on
   vshmain's own thread (called from the AddVshItem funnel), the same context
   the native handler uses. Boot-walk adds are excluded: boot_hide_for_ms is
   still set then, and the boot path parks the cursor by hiding the items
   before Memory Stick instead. */
static void maybe_jump_game_cursor_to_ms(SceVshItem *item, int topitem, int row)
{
	if (!start_at_ms_flag || boot_hide_for_ms)
		return;
	if (strcmp(item->text, "msgshare_ms"))
		return;
	sceKernelDelayThread(15000);   /* race mitigation: let the re-add settle */

	/* A genuine MS (re)insert landing in GAME's displayed column: park on
	   the boot-sampled Memory Stick row (see ms_boot_row above). */
	if (topitem != adjust_topitem_for_hidden_categories(5)) {
		return;
	}

	/* During the wake window the wake worker owns Memory-Stick parking: it waits
	   for the shifting column to settle, then snaps once by identity. A
	   concurrent maybe_jump snap by ROW NUMBER lands on whatever transiently
	   occupies that row (System Storage, Resume Game) and fights the worker --
	   so stand down while it runs. maybe_jump still handles physical MS
	   reinserts when the worker isn't active. */
	if (wake_park_thread_running) {
		return;
	}

	/* The boot MS park/settle (start_at_ms_thread) owns Memory-Stick parking
	   during boot; stand down until it's done so re-adds during the settle
	   don't yank the cursor (or fight the user's navigation) back to MS.
	   maybe_jump resumes afterward for post-boot physical MS reinserts. */
	if (boot_settling) {
		return;
	}

	/* Don't scroll off an open overlay/preview (e.g. Game>Resume Game restored
	   on wake): the MS re-add fires while the cursor is still on Resume Game,
	   and scrolling away leaves its preview stranded as a ghost. 0x14C bit0
	   clear = something is up -- leave the cursor put. (Boot is unaffected:
	   boot_hide_for_ms is still set then and returned above.) */
	if (scene_ctx && !(*(u32 *)((char *)scene_ctx + 0x14C) & 1)) {
		return;
	}

	{
		/* Park onto Memory Stick by IDENTITY (group 7 = msgshare_ms just
		   added), not the stale boot row number: during boot/settle the row
		   shifts, so a fixed row lands on whatever now sits there. */
		int msr = FindMediaRowFunc ?
			FindMediaRowFunc((void *)scene_ctx, topitem, MS_MEDIA_GROUP) : -1;
		if (msr < 0)
			msr = ms_boot_row;
		if (msr >= 0) {
			scroll_game_col_to_row(topitem, msr);
		}
	}
}

/* Post-resume worker: park the cursor on the boot-sampled Memory Stick row
   in stages, so the cursor is already sitting there by the time the entry
   visibly loads back in (~3s) instead of trailing it. First park right
   after resume (the cells persist across sleep, so the row is valid even
   before the icon renders); re-checks bracket the media re-add window
   (~1.6s) and the visible load-in, correcting any shift they caused. A
   re-park only fires when the cursor is off-target, so an untouched cursor
   costs nothing; note the trade-off: navigating inside Game within ~4.5s
   of wake gets pulled back to Memory Stick -- consistent with parking. */
static int wake_park_thread(SceSize args, void *argp)
{
	int dispcol, tick, wake_on_resume, prev_count, settle, ms_stable = 0;
	(void)args; (void)argp;

	dispcol = adjust_topitem_for_hidden_categories(5);
	wake_on_resume = -1;   /* probe once at tick 0, then cache */
	prev_count = -1;       /* wait for the column to stop shifting before acting */
	settle = 0;            /* consecutive ticks with an unchanged cell count */


	/* Poll HARD and fast (~30Hz for ~6s) so the down-snap lands within a
	   frame or two of the Go's wake re-registration dropping the cursor one
	   row above Memory Stick -- at the old 4Hz the cursor sat visibly on the
	   entry above MS for a quarter-second before we corrected it. We bail the
	   instant the user touches the d-pad (stand-down below) or once we've
	   landed on MS, so the tight loop never fights real navigation. */
	for (tick = 0; tick < 200; tick++) {
		u32 obj, arr, list;
		int cur, count;

		sceKernelDelayThread(30000);
		if (ms_boot_row < 0 || !scene_ctx || !TopcatPositionFunc)
			break;
		obj = *(u32 *)(scene_ctx + 0xA6C);
		if (!obj)
			continue;
		arr = *(u32 *)(obj + 0x360);
		if (!arr)
			continue;
		list = *(u32 *)(arr + dispcol * 4);
		if (!list)
			continue;

		count = *(int *)(list + 0x330);
		cur = TopcatPositionFunc((void *)obj, dispcol);

		/* Pre-stage at the bottom while the media cells are still coming
		   back: the visible Memory Stick cell arrives LAST (~5s after
		   resume, vshmain's own mount/scan pipeline -- measured identical
		   with and without Categories Lite). Waiting at row 3 and then
		   animating up to it made the cursor visibly trail the entry;
		   riding the bottom row instead means each insertion is followed
		   within one tick, and the final hop onto Memory Stick lands
		   within a frame of its appearance. */
		{
			int on_game = (*(int *)(obj + 0x338) == dispcol);
			/* An overlay/preview came up after resume (e.g. Game>Resume Game's
			   hover preview restoring a beat after RESUME_COMPLETE, which the
			   spawn gate couldn't see yet): stand down so we never scroll off
			   it and strand a ghost. Bit0 set = bare XMB, clear = overlay. */
			if (!(*(u32 *)((char *)scene_ctx + 0x14C) & 1)) {
				break;
			}

			/* Probe "woke on Resume Game" on the EARLIEST on-Game tick -- the
			   true woke position -- BEFORE the MS-loading early-continues below.
			   Otherwise, on a CL Go where tick 0 bails waiting for Memory Stick,
			   detection slips to a later tick by which the media re-registration
			   has dragged the cursor onto Resume Game, and we misfire the bounce
			   even though the user woke elsewhere. */
			if (on_game && wake_on_resume < 0)
				wake_on_resume = game_row_is_resume(dispcol, cur);

			/* The user is navigating -- stand down. Once a physical d-pad
			   direction is pressed, the wake park has done its job (or the user
			   is deliberately leaving Game>Memory Stick), and continuing to snap
			   would yank them back for the rest of the ~6s window. On a Go wake
			   there's no disc-focus redirect and this is the only poller, so a
			   direct read is safe. */
			{
				SceCtrlData wpad;
				if (sceCtrlPeekBufferPositive(&wpad, 1) > 0 &&
				    (wpad.Buttons & (PSP_CTRL_UP | PSP_CTRL_DOWN |
					PSP_CTRL_LEFT | PSP_CTRL_RIGHT))) {
					break;
				}
			}

			/* Aim at Memory Stick's row (live group-7 lookup, else the
			   boot-sampled row). Only act once that row actually EXISTS in the
			   loaded column (count has grown past it): previously we rode the
			   bottom row (count-1) while MS was still loading, which snapped the
			   cursor onto each transient bottom item and read as a slow
			   step-down. Instead wait, then do a single snap onto Memory Stick
			   the moment it appears. */
			/* Locate the topmost Memory Stick row by IDENTITY (our own
			   group-7 walk), preferring it over FindMediaRowFunc, which lags
			   the Go wake media load and returns -1 while a group-7 MS is still
			   registering -- that stale window is what dropped us onto the
			   ms_boot_row fallback and, ultimately, Resume Game. */
			int walk = find_ms_row_by_walk(dispcol, count);
			int fmr = FindMediaRowFunc ? FindMediaRowFunc((void *)scene_ctx,
					dispcol, MS_MEDIA_GROUP) : -1;
			int live = (walk >= 0) ? walk : fmr;
			int aim = (live >= 0) ? live : ms_boot_row;
			int target, target_is_ms;

			/* Wait for the column to STOP shifting before snapping. On a Go
			   wake the list grows over several ticks and Memory Stick's row
			   climbs with it (live 4->5, count 4->5->6); snapping on every tick
			   chased that row down step-by-step -- and the instant paf snap,
			   fired mid-rebuild, kept failing and falling back to the animated
			   single-step, so the cursor visibly SCROLLED several rows instead
			   of jumping. Hold until the count is stable (unchanged 2 ticks),
			   then do ONE clean snap to the settled row. Fast 30ms polling keeps
			   this to ~60ms past the media settling. */
			if (count != prev_count) {
				prev_count = count;
				settle = 0;
				continue;
			}
			if (++settle < 2)
				continue;

			if (live >= 0) {
				/* Memory Stick located by identity (group 7): the settled row. */
				target = aim;
				target_is_ms = 1;
			} else if (ms_boot_row >= 0 && count > ms_boot_row && settle >= 20) {
				/* No group-7 MS anywhere after ~0.6s: genuine group-8-only
				   layout. Best-effort fall back to the boot-sampled row. */
				target = ms_boot_row;
				target_is_ms = 0;
			} else {
				continue;         /* keep waiting for group-7 MS to register */
			}

			/* Detect Resume Game FIRST (once, on the Game column), so the
			   landed/snap logic below never misfires on it while the live MS-row
			   lookup is momentarily unstable on a non-CL Go (it briefly returns
			   Resume Game's own row). */
			/* Landed on Memory Stick -> done. Never treat Resume Game as landed:
			   target can transiently equal the stale ms_boot_row (a boot-time
			   row number that, with a suspended game present on wake, now points
			   at Resume Game), so cur==target could otherwise break the loop
			   sitting on Resume Game and never finish the hop to Memory Stick. */
			/* Landed only when the cursor is on a CONFIRMED Memory Stick row
			   (group 7 by identity, re-checked live). This is what stops the
			   premature landing: if the row we snapped to later shifts onto
			   Resume Game / another item as the list finishes registering, the
			   identity check fails and we keep correcting instead of exiting on
			   a stale row. The group-8-only fallback can't prove MS by identity,
			   so there it settles for "not Resume Game". */
			if (on_game && wake_on_resume <= 0 && target >= 0 && cur == target &&
			    (target_is_ms ? game_row_is_ms(dispcol, cur)
					  : !game_row_is_resume(dispcol, cur))) {
				/* On Memory Stick -- but the media may still be shuffling with
				   the count unchanged, so this exact row can later flip to
				   Resume Game. Require it to HOLD for ~0.3s before exiting; if
				   the shuffle knocks the cursor off MS meanwhile, the reset
				   below fires and we re-snap. */
				if (++ms_stable >= 50) {   /* ~1.5s: outlast the late re-registration wave */
					break;
				}
				continue;         /* on MS, still settling -- keep watching */
			}
			ms_stable = 0;            /* off MS -- restart the settle timer */

			/* Slept on another column? Once Memory Stick is ready, PRE-POSITION
			   the Game column's cursor onto it in the background first, THEN
			   bring the Game column into view -- so the visible column slide
			   already shows the cursor on Memory Stick, instead of landing on
			   Game and then snapping down to it. (paf lets us set a non-current
			   column's row; the focus-settle inside scroll_game_col_to_row is
			   skipped while Game isn't current, which is fine -- we only need
			   the row set before the reveal.) */
			if (!on_game) {
				if (target >= 0 && !game_row_is_resume(dispcol, target) &&
				    NavigateTopMenuFunc && xmb_interactive()) {
					scroll_game_col_to_row(dispcol, target);
					NavigateTopMenuFunc((void *)scene_ctx, dispcol, target);
				}
				continue;
			}

			/* If the resting cursor is on Game>Resume Game, a snap would strand
			   its preview as a ghost. Inject a REAL d-pad DOWN (through paf's
			   input path) to step off it -- exactly what a physical press does:
			   tears the preview down, no crash. Only tap once, then fall through
			   to the normal snap to finish parking on Memory Stick (the cursor
			   is now off Resume Game, so the snap strands nothing). */
			/* Woke on Game>Resume Game. Walk the cursor to Memory Stick with
			   REAL d-pad taps through paf's input path -- NOT scroll_game_col_
			   to_row, whose paf poke races paf's own tap-driven navigation and
			   crashes (confirmed: the injected tap moves the cursor cleanly, but
			   a scroll on the very next tick collides). One ~60ms tap per tick
			   (below XMB auto-repeat) moves one row and clears the preview;
			   repeat until we reach the target, then hand back to the normal
			   path. MS is below Resume Game, so this is DOWN, but handle both. */
			if (wake_on_resume > 0) {
				/* Woke on Game>Resume Game. The cursor is glued to the
				   hibernation item; there's no input loop during the wake settle
				   to move it, and a paf snap strands its preview as a ghost.
				   Use the BOOT mechanism: a real column ENTRY navigate syncs the
				   selection, tears down the old preview, AND re-renders the
				   carousel (reloading icons). Do the whole bounce ATOMICALLY
				   here -- leave Game, wait until we've actually left, then
				   re-enter at Memory Stick -- so the normal snap never
				   interleaves and races the carousel re-render (which skipped a
				   cell and left one icon blank). The slide is near-instant via
				   the NavigateTopMenuFunc speed patch at init. */
				int other = (dispcol > 0) ? dispcol - 1 : dispcol + 1;
				/* Re-enter at `target`: the column has settled (stability gate
				   above), so the live group-7 lookup now resolves Memory Stick
				   correctly -- never System Storage (group 8) or Resume Game. */
				int r = (target >= 0) ? target : 0;
				u32 o = *(u32 *)(scene_ctx + 0xA6C);
				int w;
				if (o && NavigateTopMenuFunc && xmb_interactive()) {
					NavigateTopMenuFunc((void *)scene_ctx, other, 0);
					for (w = 0; w < 80; w++) {   /* until we've left Game */
						sceKernelDelayThread(10000);
						if (*(int *)(o + 0x338) != dispcol)
							break;
					}
					NavigateTopMenuFunc((void *)scene_ctx, dispcol, r);
					for (w = 0; w < 80; w++) {   /* until we're back on Game */
						sceKernelDelayThread(10000);
						if (*(int *)(o + 0x338) == dispcol)
							break;
					}
					sceKernelDelayThread(120000);   /* let the carousel render */
				}
				wake_on_resume = 0;
				continue;
			}

			/* On Game, not yet on Memory Stick: snap toward it -- but NEVER onto
			   Resume Game. target is only trustworthy as Memory Stick once the
			   live group-7 lookup resolves; until then the ms_boot_row fallback
			   (or a momentarily-unstable live lookup) can point at Resume Game.
			   Skipping the snap in that case keeps the cursor put and lets the
			   next tick snap it STRAIGHT to Memory Stick the instant its real
			   row appears -- no Resume Game detour. */
			if (target >= 0 && cur != target &&
			    !game_row_is_resume(dispcol, target)) {
				scroll_game_col_to_row(dispcol, target);
			}
		}
	}

	wake_park_thread_running = 0;
	return 0;
}

/* The disc-focus dispatcher (FUN_0002220C) decodes the media flags into a
   HARDCODED NATIVE column (5/4/3) and navigates there -- under compaction
   paf clamps that to the rightmost visible category, which is the "boots
   into Network" / hop-to-Network behavior (hardware-confirmed: 0x222FC is
   the ONLY caller of the navigate API). Wrap that callsite: adjust the
   column and recompute the disc row in the ADJUSTED column's model (the
   native row was computed against the wrong, displayed-indexed list).
   With START_AT_MEMORY_STICK, park on Memory Stick instead of the disc. */
/* Disc-focus landing fix: the navigate anchors to the ITEM occupying the
   target row at execution time -- but the disc cell lands (and, for movie
   discs, re-registers up to 3x, each cycle re-inserting the cell) around
   the same moment, so the cursor ends up anchored on Memory Stick one row
   BELOW the disc (hardware log: nav row=0 passed, t0=1 landed). Wait out
   the registration churn, then if the user is still in that column and not
   on the disc row, step onto the disc. */
static volatile int disc_settle_running = 0;
static volatile int disc_settle_col = -1;
static volatile u32 disc_settle_until = 0;

static int disc_focus_settle_thread(SceSize args, void *argp)
{
	int col, row, cur;
	u32 obj;
	(void)args; (void)argp;

	/* Poll fast and correct IMMEDIATELY whenever the anchoring drags the
	   cursor off the disc row, instead of one correction after the full
	   deadline (the old way left a visible ~2s pause on Memory Stick).
	   Pre-insert the disc row holds Memory Stick, so cur==row and nothing
	   fires; the moment the cell lands and shifts the cursor down, the
	   next 100ms tick steps it back up -- through every re-registration
	   in the churn. Only acts while the target column stays focused. */
	while ((int)(disc_settle_until - sceKernelGetSystemTimeLow()) > 0) {
		sceKernelDelayThread(100000);
		col = disc_settle_col;
		if (!scene_ctx || col < 0 || !FindMediaRowFunc || !TopcatPositionFunc)
			continue;
		obj = *(u32 *)(scene_ctx + 0xA6C);
		if (!obj || *(int *)(obj + 0x338) != col)
			continue;
		row = FindMediaRowFunc((void *)scene_ctx, col, 2);
		cur = TopcatPositionFunc((void *)obj, col);
		if (row >= 0 && row != cur) {
			scroll_game_col_to_row(col, row);
		}
	}

	disc_settle_running = 0;
	return 0;
}

static void arm_disc_focus_settle(int col)
{
	disc_settle_col = col;
	disc_settle_until = sceKernelGetSystemTimeLow() + 1800000;
	if (!disc_settle_running) {
		SceUID tid = sceKernelCreateThread("xmbih_discsettle",
			disc_focus_settle_thread, 0x18, 0x1000, 0, NULL);
		if (tid >= 0) {
			disc_settle_running = 1;
			sceKernelStartThread(tid, 0, NULL);
		}
	}
}

static void NavigateTopMenuAdjusted(void *ctx, int topitem, int row)
{
	int adj = adjust_topitem_for_hidden_categories(topitem);
	int game_disp = adjust_topitem_for_hidden_categories(5);
	int live = xmb_interactive();


	/* Our START_AT redirect and the async settle only make sense when the
	   XMB is the interactive foreground. If a dialog is up (bad-EBOOT
	   crash-back re-registers media, re-running this synchronous path),
	   skip our additions and just mirror the native adjusted navigate. */
	if (live && start_at_ms_flag && adj != game_disp &&
	    (int)(ms_park_redirect_until - sceKernelGetSystemTimeLow()) > 0) {
		/* This window redirects the disc's async re-focus back to Game>MS.
		   The disc's own re-registration navigate carries no button press; a
		   user pressing Left/Right to another column holds a direction as it
		   fires. Latch that deliberate move: once the user has left Game, a
		   LATER disc re-registration (movie discs re-register up to 3x, each
		   several seconds apart) must NOT yank them back off the column they
		   chose (fixes: Right into PSN snapping back to Game seconds later).
		   Until the user leaves, keep redirecting the disc focus to MS. */
		SceCtrlData pad;
		if ((sceCtrlPeekBufferPositive(&pad, 1) > 0) &&
		    (pad.Buttons & (PSP_CTRL_LEFT | PSP_CTRL_RIGHT |
			PSP_CTRL_UP | PSP_CTRL_DOWN)))
			user_left_game_during_redirect = 1;
		if (!user_left_game_during_redirect) {
			NavigateTopMenuFunc(ctx, game_disp,
				ms_boot_row >= 0 ? ms_boot_row : 0);
			return;
		}
	}
	if (live)
		arm_disc_focus_settle(adj);

	if (adj != topitem) {
		int r2 = -1;

		if (start_at_ms_flag && adj == adjust_topitem_for_hidden_categories(5) &&
		    ms_boot_row >= 0)
			r2 = ms_boot_row;
		else if (FindMediaRowFunc)
			r2 = FindMediaRowFunc(ctx, adj, 2);
		if (r2 >= 0)
			row = r2;
	} else if (start_at_ms_flag && topitem == 5 && ms_boot_row >= 0) {
		row = ms_boot_row;
	}

	NavigateTopMenuFunc(ctx, adj, row);
}

/* See the declaration comment. Waits out the boot slide / disc-focus race,
   then re-asserts Game via the native navigate until it sticks (the queued
   transition can itself lose one more race). Stops as soon as Game is seen
   focused; gives up after ~4s so a user deliberately navigating away isn't
   fought forever. */
static int boot_focus_thread(SceSize args, void *argp)
{
	u32 obj = 0;
	int i, game_disp, row, cur_col;
	(void)args; (void)argp;

	for (i = 0; i < 400; i++) {        /* wait for the scene, ~10s cap */
		if (scene_ctx) {
			obj = *(u32 *)(scene_ctx + 0xA6C);
			if (obj && *(int *)(obj + 0x334) > 0)
				break;
		}
		sceKernelDelayThread(25000);
	}
	sceKernelDelayThread(1200000);     /* let the boot begin to settle */

	/* Exception (user spec): a movie UMD with START_AT_MEMORY_STICK off
	   keeps the native behavior -- boot lands on Video>UMD. Everything
	   else boots into Game (at Memory Stick with the flag, or at the Game
	   column's natural cursor row without it). */
	if (!start_at_ms_flag && sceUmdCheckMedium() > 0 &&
	    current_umd_is_video() > 0) {
		return 0;
	}

	/* Hold the Game focus through the whole boot window instead of exiting
	   on first success: the boot slide runs LATE and previously bounced the
	   focus to its clamped target (rightmost visible) after we had already
	   declared victory and exited (hardware log). Only correct when the
	   focus sits on that clamp target, so a user deliberately moving to any
	   other column during the window is never fought; the trade-off is that
	   manually selecting the clamp-target column itself within ~10s of boot
	   gets pulled back once. */
	{
		int total_visible = 8 - top_category_hidden_count;
		int slide_target = (total_visible > 5) ? 5 : total_visible - 1;
		int navigated = 0;
		/* Non-START_AT: one preemptive navigate, then a SHORT 3s fuse -- the
		   genuine bounce arrives within ~1.5s and user presses come later.
		   START_AT: the boot slide clamps focus to the rightmost visible
		   column (Network) and can fire LATE -- Categories Lite's index cache
		   delayed it to ~4s after our preemptive navigate on a PSP Go, past
		   the old 3s fuse, leaving the cursor on Network. So poll a longer,
		   faster window, but RELEASE as soon as the slide's bounce is caught
		   instead of holding the whole window and fighting the user. A "bounce"
		   is a departure to the clamp/disc target AFTER Game has settled --
		   distinct from the initial placement, and from a user pressing away
		   toward some other column. The slide bounces once; the user's own
		   presses come after and are left alone. */
		int cap = start_at_ms_flag ? 66 : 12;
		int poll = start_at_ms_flag ? 120000 : 250000;
		int on_game = 0;                   /* Game has settled at least once */
		int bounces = 0;                   /* slide bounces corrected */

		for (i = 0; i < cap; i++) {         /* START_AT ~8s @120ms, else ~3s @250ms */
			int video_disp = -1;

			if (!scene_ctx || !NavigateTopMenuFunc || hide_top_category(5))
				break;
			obj = *(u32 *)(scene_ctx + 0xA6C);
			if (!obj)
				break;
			game_disp = adjust_topitem_for_hidden_categories(5);

			/* Poll the controller directly (reliable, unlike the sporadic
			   read-hook). ANY direction press flags user navigation, which
			   stands the boot MS park / row-hold / column watchdog down -- BUT
			   only ONCE the boot slide has settled. An early press (before the
			   slide clamps to the rightmost column) must NOT release, or the
			   slide wins and lands on Network. The slide is settled when we've
			   caught it (bounces), or Game is itself the slide target (nothing
			   to catch), or a timeout. */
			if (start_at_ms_flag) {
				SceCtrlData pad;
				u32 btn = 0;
				if (sceCtrlPeekBufferPositive(&pad, 1) > 0)
					btn = pad.Buttons;
				if (btn & (PSP_CTRL_UP | PSP_CTRL_DOWN |
					PSP_CTRL_LEFT | PSP_CTRL_RIGHT))
					boot_user_nav = 1;
				/* Release on the user's press ONLY once the boot slide can no
				   longer steal focus: we've caught its bounce (bounces>=1), or
				   Game is itself the rightmost column so there's no slide to
				   catch (game_disp==slide_target), or a safety timeout. Without
				   this, an early press releases us before the LATE slide fires
				   and it clamps the cursor onto Network/PSN with no correction
				   (hardware log: est latched at ~t56, Up at ~t63, slide to PSN
				   at ~t65 after we'd already quit). */
				int slide_settled = (bounces >= 1) ||
					(game_disp == slide_target) || (i >= 50);
				if (boot_user_nav && boot_ms_established && slide_settled) {
					break;
				}
			}
			if (game_disp == slide_target && !start_at_ms_flag)
				break;             /* slide already lands on Game */
			/* START_AT: the boot-time jump-to-video-disc does NOT pass
			   through the wrapped navigate callsite (hardware log: focus
			   reached Video with no `umd focus nav` line), so catch that
			   departure here too. */
			if (start_at_ms_flag && !hide_top_category(4))
				video_disp = adjust_topitem_for_hidden_categories(4);
			cur_col = *(int *)(obj + 0x338);
			/* Latch "Game>Memory Stick established" the first time the cursor is
			   actually on MS in the Game column. On that transition, forget any
			   early press that fired before we landed -- only a fresh press AFTER
			   landing should release the boot holds (an early press must not let
			   the later boot slide win and land on Network). */
			if (start_at_ms_flag && cur_col == game_disp && FindMediaRowFunc &&
			    TopcatPositionFunc) {
				int msr = FindMediaRowFunc((void *)scene_ctx, game_disp,
					MS_MEDIA_GROUP);
				int cr = TopcatPositionFunc((void *)obj, game_disp);
				if (msr >= 0 && cr == msr) {
					if (!boot_ms_established)
						boot_user_nav = 0;
					boot_ms_established = 1;
				}
			}
			if (cur_col == game_disp) {
				on_game = 1;
				/* Row-hold (keeping the cursor on Memory Stick within Game) is
				   done by start_at_ms_thread. boot_focus only holds the COLUMN
				   here -- it must run its full window to catch the LATE boot
				   slide to the rightmost column (Network), so no early exit. */
			} else if (xmb_interactive()) {
				int done = navigated;   /* second nav = bounce fix */
				/* With START_AT the whole point is to always start on
				   Memory Stick, so re-assert Game from ANY column during
				   the window -- a Video UMD's disc-focus + slide can land
				   on Network (col 4, hardware log), which the narrow
				   slide_target/video_disp conditions missed. Without
				   START_AT keep the conservative one-shot: only correct
				   the slide's own clamp-target bounce so a user moving
				   elsewhere isn't fought. */
				/* Correct ONLY the boot slide -- a departure to the slide's
				   clamp target (rightmost visible) or the disc column -- plus the
				   one-time initial placement. Do NOT blanket-correct every
				   departure for START_AT: that fought the user's own moves to
				   other columns (hardware: navigating to col 3 while slide_tgt=5
				   got yanked back). The slide always clamps to slide_target, so
				   this still catches it and lands on Game>MS. */
				int should = !navigated ||
					(cur_col == slide_target &&
					 game_disp != slide_target) ||
					(video_disp >= 0 && cur_col == video_disp);

				if (should) {
					/* Count only a settled departure to the slide's clamp
					   (or the disc target): that's the boot slide, not a
					   user press toward some other column. */
					if (on_game && (cur_col == slide_target ||
							cur_col == video_disp))
						bounces++;
					/* START_AT: re-assert onto Memory Stick BY IDENTITY
					   (group 7), never the boot cursor -- which sits on
					   Resume Game until MS loads, so the old ms_boot_row/
					   cursor fallback pulled the cursor onto Resume Game.
					   Fall back to the boot-sampled row (the CL MS-category
					   case). If MS isn't known yet, SKIP the re-assert rather
					   than yank onto Resume Game. */
					if (start_at_ms_flag) {
						row = FindMediaRowFunc ?
							FindMediaRowFunc((void *)scene_ctx,
								game_disp, MS_MEDIA_GROUP) : -1;
						if (row < 0)
							row = ms_boot_row;
					} else {
						row = TopcatPositionFunc ?
							TopcatPositionFunc((void *)obj,
								game_disp) : -1;
						if (row < 0)
							row = 0;
					}
					if (row >= 0) {
						NavigateTopMenuFunc((void *)scene_ctx,
							game_disp, row);
						navigated = 1;
						on_game = 0;
						if (done && !start_at_ms_flag)
							break;
					}
				}
			}
			/* START_AT: once the boot slide's bounce is corrected, hand
			   control back so the user's own navigation isn't fought. (The
			   user-navigated release at the top of the loop is the robust one --
			   `bounces` can never trip when the slide clamps to Game itself.) */
			if (start_at_ms_flag && bounces >= START_AT_HOLD_BOUNCES)
				break;
			sceKernelDelayThread(poll);
		}
	}

	return 0;
}

static int start_at_ms_thread(SceSize args, void *argp)
{
	u32 obj;
	int i;
	(void)args; (void)argp;

	/* This thread owns Memory-Stick parking through the whole boot settle;
	   keep maybe_jump out of the way until we're done (cleared at the end). */
	boot_settling = 1;

	/* Wait for the XMB-ready signal: the scene's current top-category field
	   (obj+0x338) reaches the Game column's displayed index when Game is
	   focused, which happens just after the cursor is placed -- so re-adding
	   here keeps the cursor on Memory Stick. start_ms_game_index accounts for
	   any hidden pre-Game categories (Game is 5 only if none are hidden).
	   Poll until then, with a ~10s safety cap so a signal that never matches
	   (unexpected layout/firmware) can't spin forever or leave the items
	   hidden -- on timeout we re-add anyway; the cursor is long placed by
	   then, so it still stays on Memory Stick. */
	for (i = 0; i < 400; i++) {        /* 400 * 25ms = ~10s cap */
		if (!boot_hide_for_ms)
			break;
		if (scene_ctx) {
			obj = *(u32 *)(scene_ctx + 0xA6C);
			if (obj && *(int *)(obj + 0x338) == start_ms_game_index)
				break;
		}
		sceKernelDelayThread(25000);   /* 25ms */
	}

	/* Stop hiding, then re-add the saved copies directly -- incrementally,
	   the way a real disc/card insert adds an item -- using the container +
	   topitem captured during boot. No "media changed" re-scan, so the Game
	   row isn't rebuilt and there's no blink. (The calls route through our
	   filter via the prologue patch; boot_hide_for_ms is now clear so they
	   pass through and get added.) */
	boot_hide_for_ms = 0;
	if (game_ctx_captured) {
		int k;
		/* Re-add the relocated ARK CFW items FIRST so they land at the TOP of
		   Game (before Game Sharing); the fixed Game items go in after them.
		   All deferred to here, after the cursor is placed, so it stays on MS. */
		for (k = 0; k < captured_ark_count; k++)
			AddVshItem((void *)game_a0, game_topitem, &captured_ark[k]);
		if (umd_captured) {
			AddVshItem((void *)game_a0, game_topitem, &captured_umd);
			refresh_current_game_top_after_replay();
		}
		if (gamedl_captured)
			AddVshItem((void *)game_a0, game_topitem, &captured_gamedl);
		if (savedata_captured)
			AddVshItem((void *)game_a0, game_topitem, &captured_savedata);
		if (savedata_ef_captured)
			AddVshItem((void *)game_a0, game_topitem, &captured_savedata_ef);
	}

	/* Record Memory Stick's display row -- the wake/reinsert park target.
	   Ask vshmain for the row by media group (MS_MEDIA_GROUP) rather than
	   sampling the cursor: on a PSP Go the Game column also carries a System
	   Storage (ef0, "msg_em") entry one row BELOW Memory Stick, and the boot
	   cursor can end up riding THAT one, so a cursor sample caught the wrong
	   row and every re-park then bounced to System Storage. FindMediaRowFunc
	   returns Memory Stick's row by identity (group 7 = msgshare_ms; group 8
	   = System Storage). Fall back to the old cursor sample only if the media
	   lookup is unavailable. */
	/* Settle window: instead of a blind wait, watch for the cursor to land on
	   Memory Stick naturally (the boot-hide usually leaves it there on non-Go)
	   and latch "established" the instant it does -- BEFORE the park below runs
	   and before an early user press can move the cursor. Otherwise the late
	   park overrides that press (hardware log: cursor reaches MS at ~t27, user
	   presses Up at ~t33 onto UMD, park still fires at ~t40 and drags back).
	   If MS never comes up under the cursor (e.g. a Go with System Storage
	   sitting under it, or a CL layout with no group-7 item), we fall through
	   with established still 0 and the park does its normal job. */
	if (scene_ctx && FindMediaRowFunc && TopcatPositionFunc) {
		int gcol0 = adjust_topitem_for_hidden_categories(5);
		int w;
		for (w = 0; w < 28; w++) {          /* ~0.85s @ 30ms */
			u32 obj0 = *(u32 *)(scene_ctx + 0xA6C);
			int msr0 = FindMediaRowFunc((void *)scene_ctx, gcol0,
				MS_MEDIA_GROUP);
			int cr0 = obj0 ? TopcatPositionFunc((void *)obj0, gcol0) : -1;
			if (msr0 >= 0 && cr0 == msr0) {
				ms_boot_row = msr0;
				boot_ms_established = 1;
				break;
			}
			sceKernelDelayThread(30000);
		}
	} else {
		sceKernelDelayThread(800000);
	}
	if (scene_ctx) {
		int gcol = adjust_topitem_for_hidden_categories(5);
		int ms_row = -1, i;

		/* POLL for Memory Stick instead of sampling once: on a Go with many
		   Game items (ARK Extras + both storages) msgshare_ms can arrive well
		   after the fixed delay, and the old single sample then fell back to the
		   boot cursor -- which sits on Resume Game -- and parked there forever
		   (group 7 never loaded in time). Wait for group 7 to appear; but a CL
		   layout has NO group-7 item (MS is a category), so bail as soon as the
		   boot cursor is off Resume Game (CL suppression already left it on the
		   MS category). */
		for (i = 0; i < 45; i++) {          /* ~4.5s cap after the 0.8s settle */
			ms_row = FindMediaRowFunc ?
				FindMediaRowFunc((void *)scene_ctx, gcol, MS_MEDIA_GROUP) : -1;
			if (ms_row >= 0)
				break;
			obj = *(u32 *)(scene_ctx + 0xA6C);
			{
				int cr = (obj && TopcatPositionFunc) ?
					TopcatPositionFunc((void *)obj, gcol) : -1;
				if (cr >= 0 && !game_row_is_resume(gcol, cr))
					break;
			}
			sceKernelDelayThread(100000);
		}

		if (ms_row >= 0) {
			ms_boot_row = ms_row;
			/* Actively park onto Memory Stick. The boot cursor rode up to
			   whatever sat at row 0 (System Storage / Resume Game on a Go), so
			   it isn't on MS yet -- move it there now. Skip only if the user is
			   already navigating post-landing; an early press still gets parked
			   to establish Game>MS. Latch "established" right here (we just put
			   the cursor on MS) and forget any early press, so the row-hold
			   below drift-gates instead of force-parking the user's Up/Down. */
			/* Only park if the cursor hasn't already reached MS. Once
			   boot_ms_established is set (boot_focus latches it the moment
			   the cursor is on Game>MS, or we set it right below), the park's
			   job is done -- firing it again would override the user's own
			   navigation (their d-pad press can land before boot_focus's
			   coarse poll notices, so we must not rely on boot_user_nav here). */
			if (!boot_ms_established) {
				scroll_game_col_to_row(gcol, ms_row);
				boot_ms_established = 1;
			}
		} else {
			obj = *(u32 *)(scene_ctx + 0xA6C);
			if (obj && TopcatPositionFunc)
				ms_boot_row = TopcatPositionFunc((void *)obj, gcol);
		}

		/* HOLD Memory Stick through the media re-registration settle. A beat
		   after the initial park the msgshare_ms cell re-registers and the
		   cursor drops onto an adjacent item (System Storage below MS, Resume
		   Game above). Re-park to MS by identity (group 7) on any drift, and
		   keep running until the column count has stopped changing (settle
		   done) AND the cursor is on MS -- don't exit on brief early stability,
		   the drift comes seconds in. */
		if (ms_row >= 0) {
			int prevc = -1, stable = 0, j;
			/* When NO categories left of Game are hidden, the XMB's native
			   snap-to-UMD fires a beat after boot and would steal focus from
			   Game>MS onto Game>UMD. In that layout the user wants Memory Stick
			   to win, so for a short window we also treat the UMD item as a
			   drift target and re-snap off it -- and keep holding through user
			   input, so the disc snap (and an Up onto UMD) is pulled back to MS.
			   When categories left of Game ARE hidden, keep_ms is 0 and the
			   loop keeps its normal pushback-preventing behavior (Up to UMD is
			   respected, only System Storage drift is corrected). */
			int keep_ms = (count_hidden_top_categories_before(5) == 0);
			int ms_win = keep_ms ? 40 : 0;   /* ~4s of MS-wins hold in config A */
			for (j = 0; j < 100; j++) {   /* ~10s cap */
				u32 ar, ls;
				int cnt, msr, c;
				/* Stop holding MS once the user is navigating AND the slide has
				   settled. An early press before settle keeps holding so Game>MS
				   is established first. In config A, also keep holding through
				   the MS-wins window so the native disc snap is countered. */
				if (boot_user_nav && boot_ms_established && j >= ms_win)
					break;
				obj = *(u32 *)(scene_ctx + 0xA6C);
				ar = obj ? *(u32 *)(obj + 0x360) : 0;
				ls = ar ? *(u32 *)(ar + gcol * 4) : 0;
				cnt = ls ? *(int *)(ls + 0x330) : -1;
				msr = FindMediaRowFunc ?
					FindMediaRowFunc((void *)scene_ctx, gcol,
						MS_MEDIA_GROUP) : -1;
				c = (obj && TopcatPositionFunc) ?
					TopcatPositionFunc((void *)obj, gcol) : -1;
				/* Before the slide settles, FORCE Memory Stick (re-park any non-MS
				   row) to establish it against an early press / the boot slide.
				   After settle, re-park ONLY on a known drift target (System
				   Storage below MS on a Go; plus UMD in config A during the
				   MS-wins window). Up/Down to other Game items is the user
				   navigating and is left alone. */
				if (msr >= 0 && c >= 0 && c != msr) {
					int force = !boot_ms_established;
					SceVshItem *it = force ? (SceVshItem *)0 :
						game_row_item_at(gcol, c);
					if (force || (it && (!strcmp(it->text, "msg_em") ||
					    (keep_ms && j < ms_win &&
					     !strcmp(it->text, "msgshare_umd"))))) {
						ms_boot_row = msr;
						scroll_game_col_to_row(gcol, msr);
					}
				}
				if (cnt != prevc) {
					prevc = cnt;
					stable = 0;
				} else {
					stable++;
				}
				if (stable >= 15 && msr >= 0 && c == msr && j >= ms_win)
					break;                    /* ~1.5s settled + on MS */
				sceKernelDelayThread(100000);
			}
		}

		/* Cover the tail of the media re-registration even if the loop above
		   exited early (user navigated), then hand control to maybe_jump for
		   post-boot physical MS reinserts. */
		sceKernelDelayThread(5000000);
	}
	boot_settling = 0;
	return 0;
}

STMOD_HANDLER previous;

char *global_category = "Global";
char *settings_category = "Settings";
char *extras_category = "Extras";
char *photo_category = "Photo";
char *music_category = "Music";
char *video_category = "Video";
char *game_category = "Game";
char *network_category = "Network";
/* UTF-8 ® (0xC2 0xAE) -- matches the [PlayStation®Network] header in the
   shipped ini. The old value used a lone Latin-1 0xAE, which is a DIFFERENT
   byte length, so minIni's section match (which checks name length first)
   never found the section and every PSN item flag read as 0 -- nothing could
   be hidden. See cfg_psn() for the Latin-1 fallback. */
char *playstation_network_category = "PlayStation\xC2\xAENetwork";

typedef struct
{
	char icon_id[4];
	char focus_id[4];
	char unk_id[4];
	char text[0x18];
} XmbTopCategory;

static const char *top_category_names[8] = {
	"msgshare_settings",
	"msgtop_extras",
	"msgtop_photo",
	"msgtop_music",
	"msgtop_video",
	"msgshare_game",
	"msgtop_network",
	"msg_psn"
};

void ClearCaches()
{
	sceKernelDcacheWritebackAll();
	kuKernelIcacheInvalidateAll();
}

int cfg(char *category, char *fmt)
{
	return ini_getlhex(category, fmt, 0, ini_path);
}

/* Read a [PlayStation®Network] key. The ® in that section header is
   byte-fragile: the shipped ini is UTF-8 (0xC2 0xAE) but a text editor can
   re-save it Latin-1 (0xAE). Try the UTF-8 header first; if the section isn't
   found (distinguished by a -1 default), fall back to the Latin-1 header. Both
   ASCII sibling sections match fine -- only this one carries a non-ASCII char. */
int cfg_psn(char *fmt)
{
	int v = ini_getlhex(playstation_network_category, fmt, -1, ini_path);
	if (v < 0)
		v = ini_getlhex("PlayStation\xAENetwork", fmt, 0, ini_path);
	return v;
}

static int text_field_matches(const char *field, const char *name, int size)
{
	int i;

	for (i = 0; i < size; i++) {
		if (field[i] != name[i])
			return 0;
		if (name[i] == 0)
			return 1;
	}

	return 0;
}

static XmbTopCategory *find_top_category_table(u32 text_addr)
{
	char *base = (char *)text_addr;
	int limit = 0x56000 - (8 * (int)sizeof(XmbTopCategory));
	int i, j;

	for (i = 0; i < limit; i++) {
		XmbTopCategory *table = (XmbTopCategory *)(base + i);

		if (!text_field_matches(table[0].text, top_category_names[0], sizeof(table[0].text)))
			continue;

		for (j = 1; j < 8; j++) {
			if (!text_field_matches(table[j].text, top_category_names[j], sizeof(table[j].text)))
				break;
		}

		if (j == 8)
			return table;
	}

	return NULL;
}

/*
 * Extras/ARK handling:
 *   HIDE_ALL_EXTRAS = 2 is clamped to 1 by the plugin. Fully removing the
 *   Extras top category is only supported through the safe fake-region path.
 *
 *   MOVE_ARK_EXTRAS = 1 reuses the old split-routing logic, but does NOT hide
 *   Extras or its non-ARK items: Custom Firmware Settings + Plugins Manager go
 *   to PSP Settings, while the remaining ARK items go to Game.
 *
 * On a fake VSH region that already drops Extras (Latin America/Hong Kong/
 * Taiwan/Russia/China/Debug I), ARK itself moves the CFW items to Game and
 * the category list keeps its full layout, so XMBIH must NOT also count-patch
 * Extras (that would double-hide and desync the Game-item paths). We still
 * detect the region so ARK items can be routed by name independent of how
 * Extras became hidden.
 */
#define FAKE_REGION_LATIN_AMERICA  6
#define FAKE_REGION_HONGKONG       8
#define FAKE_REGION_TAIWAN         9
#define FAKE_REGION_RUSSIA       10
#define FAKE_REGION_CHINA        11
#define FAKE_REGION_DEBUG_TYPE_I 12
#define SE_CONFIG_EX_NID     0x8E426F09

typedef SEConfig *(*GetSEConfigExFunc)(SEConfig *config, int size);

static int fake_region_loaded = 0;
static int fake_region_hides_extras = 0;

static int fake_region_value_hides_extras(int vshregion)
{
	/* Regions known to drop the Extras column (kept in sync with
	   game-categories-lite). */
	return vshregion == FAKE_REGION_LATIN_AMERICA ||
	       vshregion == FAKE_REGION_HONGKONG ||
	       vshregion == FAKE_REGION_TAIWAN ||
	       vshregion == FAKE_REGION_RUSSIA ||
	       vshregion == FAKE_REGION_CHINA ||
	       vshregion == FAKE_REGION_DEBUG_TYPE_I;
}

static int extras_hidden_by_region(void)
{
	if (!fake_region_loaded) {
		GetSEConfigExFunc get_se_config_ex;

		fake_region_loaded = 1;
		get_se_config_ex = (GetSEConfigExFunc)sctrlHENFindFunction(
			"SystemCtrlForUser", "SystemCtrlForUser", SE_CONFIG_EX_NID);
		if (get_se_config_ex) {
			SEConfig se_config;
			memset(&se_config, 0, sizeof(se_config));
			if (get_se_config_ex(&se_config, sizeof(se_config)))
				fake_region_hides_extras =
					fake_region_value_hides_extras(se_config.vshregion);
		}
	}

	return fake_region_hides_extras;
}

/* set[57]==1: relocate ARK's Extras items without hiding the Extras column. */
static int move_extra_items_mode(void)
{
	return set[57];
}

/* Extras column is gone this boot only when the active fake VSH region hides
   it. MOVE_ARK_EXTRAS does not hide it; it only changes where ARK's own
   injected items land. */
static int extras_is_hidden(void)
{
	return extras_hidden_by_region();
}

static int top_category_requested_hidden(int index)
{
	if (set[54] == 2) {
		switch (index) {
			case 0:
			case 1:
			case 2:
			case 3:
			case 4:
			case 5:
			case 6:
			case 7:
				return 1;
			default:
				return 0;
		}
	}

	switch (index) {
		case 1:
			/* The plugin no longer hides the Extras top category via
			   HIDE_ALL_EXTRAS=2. Use a fake VSH region if the whole column
			   should disappear. */
			return 0;
		case 2:
			return set[2] == 2;
		case 3:
			return set[3] == 2;
		case 4:
			return set[56] == 2;
		case 5:
			return set[4] == 2;
		case 6:
			return set[5] == 2;
		case 7:
			return set[6] == 2;
	}

	return 0;
}

static int hide_top_category(int index)
{
	return top_category_requested_hidden(index);
}


static int get_top_category_hidden_count(void)
{
	int count = 0;
	int i;

	for (i = 0; i < 8; i++) {
		if (top_category_requested_hidden(i))
			count++;
	}

	return count;
}

static int AdjustTopCategoryCountAndGetCount(void *ctx)
{
	u32 obj;
	int count;
	int new_count;

	scene_ctx = (u32)ctx;   /* expose scene context to the START_AT_MEMORY_STICK thread */

	obj = *(u32 *)((char *)ctx + 0xA6C);
	if (!obj)
		return 0;

	count = *(int *)(obj + 0x334);
	if (top_category_hidden_count <= 0)
		return count;


	new_count = count - top_category_hidden_count;
	if (new_count < 1)
		new_count = 1;

	if (count > new_count) {
		if (hide_top_category(0))
			return count;

		*(int *)(obj + 0x334) = new_count;
		return new_count;
	}

	return count;
}

static void PatchTopCategories(u32 text_addr)
{
	u32 *meta0;
	u32 *meta1;
	u32 *meta2;
	XmbTopCategory *table;
	u32 filtered_meta0[8];
	u32 filtered_meta1[8];
	u32 filtered_meta2[8];
	XmbTopCategory filtered[8];
	int i;
	int out = 0;
	int changed = 0;

	if (top_category_hidden_count <= 0) {
		return;
	}

	table = find_top_category_table(text_addr);
	if (!table) {
		return;
	}

	/* Compact the XmbTopCategory icon/text table AND the three parallel
	   u32[8] arrays sitting immediately before it (wad11656's
	   "meta0/meta1/meta2"). These hold per-category state that vshmain
	   reads alongside the visual table when it builds each column, so they
	   must stay in lock-step with the table -- otherwise every column from
	   the hidden index onward shows one category's label/items wrapped
	   around the previous category's behaviour, which is the "everything
	   after the hidden category is broken" bug.

	   Preserve the original tail-slot meta data instead of zeroing it.
	   vshmain still performs some hardcoded lookups by original category
	   index, so those backing arrays need valid data in their original
	   slots.

	   Preserve the table tail too. Network/UMD-related firmware paths still
	   appear to resolve some per-category visual state by the original
	   pre-compaction index; leaving the original entries in the unused tail
	   is safer than blanking them. The runtime count patch trims the visible
	   category count separately, so these tail entries are not rendered. */

	meta0 = (u32 *)((char *)table - (8 * sizeof(u32) * 3));
	meta1 = (u32 *)((char *)table - (8 * sizeof(u32) * 2));
	meta2 = (u32 *)((char *)table - (8 * sizeof(u32) * 1));

	for (i = 0; i < 8; i++) {
		int hide = hide_top_category(i);
		if (hide) {
			changed = 1;
			continue;
		}

		filtered_meta0[out] = meta0[i];
		filtered_meta1[out] = meta1[i];
		filtered_meta2[out] = meta2[i];
		memcpy(&filtered[out], &table[i], sizeof(filtered[out]));
		out++;
	}

	if (!changed) {
		return;
	}

	/* Fill the unused tail slots [out..8).

	   Normally we preserve each original entry at its physical slot so
	   vshmain's hardcoded per-original-index lookups (notably network=6,
	   PSN=7) still resolve from the tail.

	   But when EXACTLY ONE category is hidden the visible range runs all the
	   way through slot 6, so PSN (original index 7) is compacted into slot 6
	   AND this tail-fill would copy the original PSN into slot 7 -- a verbatim
	   duplicate. vshmain then walks the slot-7 PSN as a phantom 8th category
	   (the doubled "psn state"), which crashes the boot. There is only one
	   tail slot in that case, so we cannot preserve both network(6) and PSN(7)
	   anyway; drop the hidden category's original into the tail instead so no
	   visible category is duplicated. (>=2 hidden keeps the original behaviour:
	   it has >=2 tail slots and never duplicates a visible category.) */
	if (top_category_hidden_count == 1) {
		for (i = 0; i < 8 && out < 8; i++) {
			if (!hide_top_category(i))
				continue;
			filtered_meta0[out] = meta0[i];
			filtered_meta1[out] = meta1[i];
			filtered_meta2[out] = meta2[i];
			memcpy(&filtered[out], &table[i], sizeof(filtered[out]));
			out++;
		}
	} else {
		while (out < 8) {
			filtered_meta0[out] = meta0[out];
			filtered_meta1[out] = meta1[out];
			filtered_meta2[out] = meta2[out];
			memcpy(&filtered[out], &table[out], sizeof(filtered[out]));
			out++;
		}
	}

	memcpy(meta0, filtered_meta0, sizeof(filtered_meta0));
	memcpy(meta1, filtered_meta1, sizeof(filtered_meta1));
	memcpy(meta2, filtered_meta2, sizeof(filtered_meta2));
	memcpy(table, filtered, sizeof(filtered));
}

int skip(SceVshItem *item, int location)
{
	int idnm(char *name)
	{
		return strcmp(item->text, name);
	}

	/* Single-hide boot stabilizer. With the count==1 layout the initial XMB
	   item walk overruns vshmain's asynchronous icon/texture loading and faults
	   before the XMB appears. A brief yield per item throttles the walk so the
	   async loads drain -- count==1 only, and only for the first ~200 items (the
	   boot walk), so navigation and all other layouts are untouched. */
	{
		static volatile int boot_throttle_n;
		if (top_category_hidden_count == 1 && boot_throttle_n < 200) {
			boot_throttle_n++;
			sceKernelDelayThread(4000);
		}
	}

	/* START_AT_MEMORY_STICK: lazily spawn the show-savedata thread the first time
	   skip() runs. skip() is called from vshmain's AddVshItem walk, i.e.
	   from vshmain's runtime context -- the same context the (working)
	   scene-dump thread was created from. Creating it from OnModuleStart
	   (much earlier) produced a thread that never ran. */
	if (start_at_ms_flag && !ms_thread_started) {
		SceUID tid;
		ms_thread_started = 1;
		tid = sceKernelCreateThread("xmbih_show_savedata",
			start_at_ms_thread, 0x18, 0x1000, 0, NULL);
		if (tid >= 0)
			sceKernelStartThread(tid, 0, NULL);
	}

	/* Boot-into-Game (see boot_focus_thread): spawned from the same vshmain
	   runtime context as the thread above, for the same reason. Needed only
	   when (a) categories LEFT of Game are hidden -- the boot slide then
	   clamps right of Game unless Network/PSN are hidden too, which the
	   thread's slide-target check handles -- or (b) START_AT_MEMORY_STICK
	   is set (assert Game>MS over the boot-time jump-to-video-disc). All
	   other configs boot into Game natively. */
	if ((count_hidden_top_categories_before(5) > 0 || start_at_ms_flag) &&
	    !boot_focus_thread_started) {
		SceUID tid;
		boot_focus_thread_started = 1;
		tid = sceKernelCreateThread("xmbih_boot_focus",
			boot_focus_thread, 0x18, 0x1000, 0, NULL);
		if (tid >= 0)
			sceKernelStartThread(tid, 0, NULL);
	}

	/* START_AT_MEMORY_STICK boot-hide. During the boot pass, force-hide the fixed
	   Game items that precede Memory Stick (Game Sharing, Saved Data Utility,
	   and -- when a disc is inserted -- the UMD item) so the cursor lands on
	   MS. We capture a copy of each first; once the XMB is ready the post-boot
	   thread clears boot_hide_for_ms and re-adds them directly via AddVshItem
	   (see start_at_ms_thread). */
	/* A movie UMD whose Movie column is still visible belongs in Movie, so it
	   must NOT enter the START_AT_MEMORY_STICK boot-hide (which would hide it and
	   re-add it into Game). Only boot-hide the UMD when Game is its real home: a
	   game UMD, or a movie UMD with the Movie category hidden. */
	if (boot_hide_for_ms &&
	    (!idnm("msgtop_game_savedata") || !idnm("msgtop_game_gamedl") ||
	     (!idnm("msgshare_umd") &&
	      !(current_umd_is_video() > 0 && !hide_top_category(4))))) {
		if (maybe_disable_start_at_ms_from_disc_layout(item, location))
			return 1;
		if (!idnm("msgtop_game_gamedl") && !gamedl_captured) {
			memcpy(&captured_gamedl, item, sizeof(SceVshItem));
			gamedl_captured = 1;
		}
		if (!idnm("msgtop_game_savedata")) {
			/* A PSP Go has TWO Saved Data Utility entries: Memory Stick
			   (location 5) and System Storage / ef0 (location 6). Capture
			   each into its own slot so both are re-added -- otherwise the
			   single latch dropped the ef0 one and it vanished. */
			if (location == 6) {
				if (!savedata_ef_captured) {
					memcpy(&captured_savedata_ef, item, sizeof(SceVshItem));
					savedata_ef_captured = 1;
				}
			} else if (!savedata_captured) {
				memcpy(&captured_savedata, item, sizeof(SceVshItem));
				savedata_captured = 1;
			}
		}
		if (!idnm("msgshare_umd") && !umd_captured) {
			memcpy(&captured_umd, item, sizeof(SceVshItem));
			umd_captured = 1;
		}
		return 0;
	}

	if (!idnm("msg_signup") || !idnm("msg_ps_store") || !idnm("msg_information_board")) {
	}

	if (!idnm("msg_game_hibernation")) {
	}

	if((!(
	((devkit >= FW(0x620) ? !idnm("msg_system_update") : !idnm("msgtop_sysconf_update")) && (set[7] || set[0])) ||
	(!idnm("msgtop_sysconf_usb") && (set[8] || set[0])) ||
	(!idnm("msgtop_sysconf_video") && (set[9] || set[0])) ||
	(!idnm("msgtop_sysconf_photo") && (set[10] || set[0])) ||
	(!idnm("msgtop_sysconf_console") && (set[11] || set[0])) ||
	(!idnm("msgtop_sysconf_theme") && (set[12] || set[0])) ||
	(!idnm("msgtop_sysconf_date") && (set[13] || set[0])) ||
	(!idnm("msgtop_sysconf_powersave") && (set[14] || set[0])) ||
	(!idnm("msg_bt_device_settings") && (set[15] || set[0])) ||
	(!idnm("msg_display_setting") && (set[16] || set[0])) ||
	(!idnm("msgtop_sysconf_sound") && (set[17] || set[0])) ||
	(!idnm("msgtop_sysconf_security") && (set[18] || set[0])) ||
	(!idnm("msgtop_sysconf_rss") && (set[19] || set[0])) ||
	(!idnm("msgtop_sysconf_network") && (set[20] || set[0])) ||
	(!idnm("msg_1seg") && (set[21] || set[1])) ||
	(!idnm("msg_tdmb") && (set[22] || set[1])) ||
	(!idnm("msg_bookreader") && (set[23] || set[1])) ||
	(!idnm("msg_digitalcomics") && (set[24] || set[1])) ||
	(!idnm("msg_music_unlimited") && (set[25] || set[3])) ||
	(!idnm("msg_xradar_portable") && (set[26] || set[1])) ||
	(!idnm("msgtop_camera") && (set[27] || set[2])) ||
	(!idnm("msgshare_ms") && (set[28] || set[2]) && location == 1) ||
	(!idnm("msg_em") && (set[29] || set[2]) && location == 1) ||
	(!idnm("msg_sensme_channels") && (set[30] || set[3])) ||
	(!idnm("msgshare_ms") && (set[31] || set[3]) && location == 2) ||
	(!idnm("msg_em") && (set[32] || set[3]) && location == 2) ||
	(!idnm("msgshare_ms") && (set[33] || set[56]) && location == 3) ||
	(!idnm("msg_em") && (set[34] || set[56]) && location == 3) ||
	(!idnm("msgtop_game_gamedl") && (set[35] || set[4])) ||
	(!idnm("msgtop_game_savedata") && (set[36] || set[4]) && (psp_model == 4 ? location == 5 : location == 0)) ||
	(!idnm("msgtop_game_savedata") && (set[37] || set[4]) && psp_model == 4 && location == 6) ||
	(!idnm("msg_game_hibernation") && (set[38] || set[4])) ||
	(!idnm("msgshare_ms") && (set[39] || set[4]) && location == 4) ||
	(!idnm("msg_em") && (set[40] || set[4]) && location == 4) ||
	(!idnm("msg_onlinemanual") && (set[41] || set[5])) ||
	(!idnm("msgtop_network_lftv") && (set[42] || set[5])) ||
	(!idnm("msg_skype") && (set[43] || set[5])) ||
	(!idnm("msg_ps3_connection") && (set[44] || set[5])) ||
	(!idnm("msg_internetradio") && (set[45] || set[5])) ||
	(!idnm("msgtop_network_rss") && (set[46] || set[5])) ||
	(!idnm("msgtop_network_browser") && (set[47] || set[5])) ||
	(!idnm("msg_internet_search") && (set[48] || set[5])) ||
	(!idnm("msg_psspot") && (set[49] || set[5])) ||
	(!idnm("msg_gomessenger") && (set[50] || set[5])) ||
	((!idnm("msg_signup") || !idnm("msg_account_manage")) && (set[51] || set[6])) ||
	(!idnm("msg_ps_store") && (set[52] || set[6])) ||
	(!idnm("msg_information_board") && (set[53] || set[6]))
	)) && !set[54])
		return 1;
		
	return 0;
}

static int keep_item(int loc, SceVshItem *item)
{
	return skip(item, loc);
}

static int count_hidden_top_categories_before(int topitem)
{
	int shift = 0;
	int i;

	for (i = 0; i < topitem && i < 8; i++) {
		if (hide_top_category(i))
			shift++;
	}

	return shift;
}

static int adjust_topitem_for_hidden_categories(int topitem)
{
	topitem -= count_hidden_top_categories_before(topitem);
	if (topitem < 0)
		topitem = 0;

	return topitem;
}

static int need_network_icon_prime(void)
{
	/* The lazy prime is required in two known layouts:
	   1. Network shifts left because something before it is hidden.
	   2. PSN is the only hidden top category. Network stays at slot 6 there,
	      but Remote Play still comes up blank until the column is scrolled. */
	if (hide_top_category(6))
		return 0;

	if (count_hidden_top_categories_before(6) > 0)
		return 1;

	return top_category_hidden_count == 1 && hide_top_category(7);
}

static int is_ark_custom_item(const char *text)
{
	return !strcmp(text, "xmbmsgtop_sysconf_configuration") ||
		!strcmp(text, "xmbmsgtop_sysconf_plugins") ||
		!strcmp(text, "xmbmsgtop_custom_launcher") ||
		!strcmp(text, "xmbmsgtop_custom_app") ||
		!strcmp(text, "xmbmsgtop_150_reboot");
}

/* Per-item hide flags for ARK's injected CFW menu items (ini [Extras]).
   The text keys are identical on ARK-4 and ARK-5, so one mapping covers both.
     CUSTOM_FIRMWARE_SETTINGS -> xmbmsgtop_sysconf_configuration (set[59])
     PLUGINS_MANAGER          -> xmbmsgtop_sysconf_plugins       (set[60])
     CUSTOM_LAUNCHER          -> xmbmsgtop_custom_launcher       (set[61])
   Returns nonzero when the user has set that item to 1 (hide). */
static int is_ark_item_hidden(const char *text)
{
	if (!strcmp(text, "xmbmsgtop_sysconf_configuration"))
		return set[59];
	if (!strcmp(text, "xmbmsgtop_sysconf_plugins"))
		return set[60];
	if (!strcmp(text, "xmbmsgtop_custom_launcher"))
		return set[61];
	return 0;
}

static int is_game_resume_item(const char *text)
{
	return !strcmp(text, "msg_game_hibernation");
}

static int remap_ark_topitem(const char *text, int incoming_topitem, int *out_topitem);
static int is_settings_bound_ark_item(const char *text);

/* Rewrite the 3 XMB icon-layer keys. They resolve against a per-column atlas,
   so an item moved to a new column needs keys that exist there. */
static void reloc_set_icon(SceVshItem *item, const char *img, const char *sh,
	const char *gl)
{
	strcpy(item->image, img);
	strcpy(item->image_shadow, sh);
	strcpy(item->image_glow, gl);
}

/* MOVE_ARK_EXTRAS sends CFW Settings + Plugins into the Settings column, whose
   atlas differs from the Network/PSN one ARK's templates use -- repoint to
   Settings keys so they don't render as garbage. (System gear / Theme; derived
   on 6.61.) */
static void reloc_fix_settings_icon(SceVshItem *item)
{
	if (!strcmp(item->text, "xmbmsgtop_sysconf_configuration"))
		reloc_set_icon(item, "BX", "CL", "CZ");
	else if (!strcmp(item->text, "xmbmsgtop_sysconf_plugins"))
		reloc_set_icon(item, "BY", "CM", "DA");
}

static int prepare_topitem_for_item(SceVshItem *item, int incoming_topitem,
	int *out_topitem, const char *source)
{
	if (is_ark_custom_item(item->text)) {
		/* User hid this CFW item (ini [Extras]): drop it outright. This takes
		   priority over MOVE_ARK_EXTRAS -- a hidden item is never relocated,
		   just removed -- and, because we return before the boot-hide capture
		   in the filter, it's never re-added by the START_AT worker either. */
		if (is_ark_item_hidden(item->text))
			return 0;

		if (!remap_ark_topitem(item->text, incoming_topitem, out_topitem)) {
			return 0;
		}

		/* When these two items move into Settings, fix their icon keys for
		   that column's atlas. */
		if (move_extra_items_mode() && is_settings_bound_ark_item(item->text))
			reloc_fix_settings_icon(item);

		return 1;
		}

	*out_topitem = adjust_topitem_for_hidden_categories(incoming_topitem);

	if (is_game_resume_item(item->text)) {
	}

	return 1;
}

/* The two "settings-like" CFW items that MOVE_ARK_EXTRAS sends to PSP Settings. */
static int is_settings_bound_ark_item(const char *text)
{
	return !strcmp(text, "xmbmsgtop_sysconf_configuration") ||
	       !strcmp(text, "xmbmsgtop_sysconf_plugins");
}

static int remap_ark_topitem(const char *text, int incoming_topitem, int *out_topitem)
{
	/*
	 * Decide where an ARK CFW item lands. Routing is by item NAME and is
	 * checked in priority order so it works regardless of HOW (or whether)
	 * Extras is hidden -- which matters for the Russia workaround, where the
	 * region hides Extras and ARK injects the items into Game (topitem 5):
	 *
	 *  1. MOVE_ARK_EXTRAS: CFW Settings + Plugins -> PSP Settings
	 *  2. MOVE_ARK_EXTRAS: Launcher/app/reboot -> Game
	 *  3. Extras still visible, and MOVE_ARK_EXTRAS is off, and Extras-path
	 *     injection -> keep in Extras
	 *  4. destination category not visible -> drop
	 */
	if (move_extra_items_mode() && is_settings_bound_ark_item(text)) {
		if (!hide_top_category(0)) {
			*out_topitem = adjust_topitem_for_hidden_categories(0);
			return 1;
		}
		/* Settings column hidden too -> fall through to Game. */
	}

	if (move_extra_items_mode()) {
		if (!hide_top_category(5)) {
			*out_topitem = adjust_topitem_for_hidden_categories(5);
			return 1;
		}
		return 0;
	}

	if (!extras_is_hidden() && incoming_topitem == 1) {
		*out_topitem = adjust_topitem_for_hidden_categories(1);
		return 1;
	}

	if (!hide_top_category(5)) {
		int base_topitem = incoming_topitem < 5 ? 5 : incoming_topitem;
		*out_topitem = adjust_topitem_for_hidden_categories(base_topitem);
		return 1;
	}

	return 0;
}

/* xmbctrl injects its CFW menu items the first time it sees one of these
   text keys ("items_added" static flag in xmbctrl flips on first match).
   If our wrapper hides any of them via skip(), xmbctrl never gets the
   item, the flag stays 0, and the CFW menus never appear. To keep them
   visible we force-forward trigger items regardless of the user's hide
   flag. The user can no longer hide GAME_SHARING / SAVED_DATA_UTILITY_MS
   / DIGITAL_COMICS / 1SEG / T-DMB / JP_BOOKREADER / X-RADAR_PORTABLE on
   ARK-5 -- that's the tradeoff. */
static int is_xmbctrl_trigger(const char *text)
{
	return  !strcmp(text, "msgtop_game_gamedl")  ||
	        !strcmp(text, "msgtop_game_savedata") ||
	        !strcmp(text, "msg_digitalcomics")    ||
	        !strcmp(text, "msg_bookreader")       ||
	        !strcmp(text, "msg_1seg")             ||
	        !strcmp(text, "msg_xradar_portable")  ||
	        !strcmp(text, "msg_tdmb");
}

int AddVshItemPatched(void *a0, int topitem, SceVshItem *item)
{
	int incoming_topitem = topitem;
	int add_topitem;

	{
		int adjusted_topitem;

		if (!prepare_topitem_for_item(item, topitem, &adjusted_topitem, "wrapper"))
			return 0;

		topitem = adjusted_topitem;
	}

	maybe_disable_start_at_ms_for_movie_boot(item, 0, topitem);
	add_topitem = should_preserve_network_topitem(incoming_topitem, topitem, item) ?
		incoming_topitem : topitem;
	if (add_topitem != topitem) {
	}

	/* START_AT_MEMORY_STICK: capture the container + Game topitem the first time we
	   see one of the items we're hiding, so the worker thread can re-add the
	   saved copies into the same place later without a re-scan.
	   NOTE: this is the location-0 (phat/slim) path. On a PSP Go, Saved Data
	   is walked via a separate call site (AddVshItemPatchedGameSavedataMs/Ef),
	   so its native context isn't captured here and it'd be re-added with Game
	   Sharing's context -- untested on Go. */
	if (boot_hide_for_ms && !game_ctx_captured &&
	    (!strcmp(item->text, "msgtop_game_gamedl") ||
	     !strcmp(item->text, "msgtop_game_savedata") ||
	     !strcmp(item->text, "msgshare_umd"))) {
		game_a0 = a0;
		game_topitem = topitem;
		game_ctx_captured = 1;
	}

	/* Force-forward only the FIRST xmbctrl trigger we see -- that flips
	   xmbctrl's items_added flag, so the CFW menu items get injected.
	   Subsequent triggers fall through to keep_item (skip()) so the user
	   can hide them. The prologue patch above, if it takes effect, also
	   filters this first trigger's forward so even it gets hidden. */
	int force_trigger = is_xmbctrl_trigger(item->text) && !xmbctrl_triggered;
	if(force_trigger) xmbctrl_triggered = 1;
	if(force_trigger || keep_item(0, item))
		return AddVshItem(a0, add_topitem, item);

	return 0;
}

int AddVshItemPatchedPhoto(void *a0, int topitem, SceVshItem *item)
{
	topitem = adjust_topitem_for_hidden_categories(topitem);
	maybe_disable_start_at_ms_for_movie_boot(item, 1, topitem);
	if (maybe_suppress_media_readd(item, 1, topitem))
		return 0;
	int force_trigger = is_xmbctrl_trigger(item->text) && !xmbctrl_triggered;
	if(force_trigger) xmbctrl_triggered = 1;
	if(force_trigger || keep_item(1, item))
		return AddVshItem(a0, topitem, item);

	return 0;
}

int AddVshItemPatchedMusic(void *a0, int topitem, SceVshItem *item)
{
	topitem = adjust_topitem_for_hidden_categories(topitem);
	maybe_disable_start_at_ms_for_movie_boot(item, 2, topitem);
	if (maybe_suppress_media_readd(item, 2, topitem))
		return 0;
	int force_trigger = is_xmbctrl_trigger(item->text) && !xmbctrl_triggered;
	if(force_trigger) xmbctrl_triggered = 1;
	if(force_trigger || keep_item(2, item))
		return AddVshItem(a0, topitem, item);

	return 0;
}

int AddVshItemPatchedVideo(void *a0, int topitem, SceVshItem *item)
{
	topitem = adjust_topitem_for_hidden_categories(topitem);
	maybe_disable_start_at_ms_for_movie_boot(item, 3, topitem);
	if (maybe_suppress_media_readd(item, 3, topitem))
		return 0;
	int force_trigger = is_xmbctrl_trigger(item->text) && !xmbctrl_triggered;
	if(force_trigger) xmbctrl_triggered = 1;
	if(force_trigger || keep_item(3, item))
		return AddVshItem(a0, topitem, item);

	return 0;
}

int AddVshItemPatchedGame(void *a0, int topitem, SceVshItem *item)
{
	topitem = adjust_topitem_for_hidden_categories(topitem);
	maybe_disable_start_at_ms_for_movie_boot(item, 4, topitem);
	if (maybe_suppress_media_readd(item, 4, topitem))
		return 0;
	int force_trigger = is_xmbctrl_trigger(item->text) && !xmbctrl_triggered;
	if(force_trigger) xmbctrl_triggered = 1;
	if(force_trigger || keep_item(4, item))
		return AddVshItem(a0, topitem, item);

	return 0;
}

int AddVshItemPatchedGameSavedataMs(void *a0, int topitem, SceVshItem *item)
{
	topitem = adjust_topitem_for_hidden_categories(topitem);
	maybe_disable_start_at_ms_for_movie_boot(item, 5, topitem);
	if (maybe_suppress_media_readd(item, 5, topitem))
		return 0;
	int force_trigger = is_xmbctrl_trigger(item->text) && !xmbctrl_triggered;
	if(force_trigger) xmbctrl_triggered = 1;
	if(force_trigger || keep_item(5, item))
		return AddVshItem(a0, topitem, item);

	return 0;
}

int AddVshItemPatchedGameSavedataEf(void *a0, int topitem, SceVshItem *item)
{
	topitem = adjust_topitem_for_hidden_categories(topitem);
	maybe_disable_start_at_ms_for_movie_boot(item, 6, topitem);
	if (maybe_suppress_media_readd(item, 6, topitem))
		return 0;
	int force_trigger = is_xmbctrl_trigger(item->text) && !xmbctrl_triggered;
	if(force_trigger) xmbctrl_triggered = 1;
	if(force_trigger || keep_item(6, item))
		return AddVshItem(a0, topitem, item);

	return 0;
}

/* The trimmed top-category wrappers above do not cover vshmain's dedicated UMD
   preview add/select helpers, so when categories left of Game are hidden with
   mode 2 those paths still use the original untrimmed indices. Patch the 6.60
   UMD-specific callsites too so physical Game/Movie UMD entries shift left in
   lock-step with the visible top-category layout. */
static int UmdVideoAddPatchedRet(void *a0, int topitem, SceVshItem *item)
{
	int adjusted_topitem = adjust_topitem_for_hidden_categories(topitem);

	if (maybe_defer_boot_umd_add(a0, adjusted_topitem, item))
		return 0;
	if (maybe_suppress_media_readd(item, 3, adjusted_topitem))
		return 0;

	return AddVshItem(a0, adjusted_topitem, item);
}

static int UmdGameAddPatchedRet(void *a0, int topitem, SceVshItem *item)
{
	int adjusted_topitem = adjust_topitem_for_hidden_categories(topitem);

	if (maybe_defer_boot_umd_add(a0, adjusted_topitem, item))
		return 0;
	if (maybe_suppress_media_readd(item, 4, adjusted_topitem))
		return 0;

	return AddVshItem(a0, adjusted_topitem, item);
}

static int UmdVideoSelectShiftPatched(void *ctx, int topitem)
{
	return TopcatSelectShiftedFunc(ctx,
		adjust_topitem_for_hidden_categories(topitem), topitem);
}

static int UmdGameSelectShiftPatched(void *ctx, int topitem)
{
	if (should_suppress_umd_select_route(topitem)) {
		return 0;
	}
	return TopcatSelectShiftedFunc(ctx,
		adjust_topitem_for_hidden_categories(topitem), topitem);
}

static int UmdTopcatPositionShiftPatched(void *obj, int topitem)
{
	int adjusted_topitem = adjust_topitem_for_hidden_categories(topitem);

	return TopcatPositionFunc(obj, adjusted_topitem);
}

/* FIX: force ICON0 (asset 2) to report "needs loading" in the compacted layout
   so the disc-preview refresh actually attempts the real load instead of
   skipping it on a stale state slot. Re-fires each refresh until it sticks. */
static int DiscAssetNeededWrap(void *ctx, int asset)
{
	int ret = DiscAssetNeeded(ctx, asset);
	if (asset == 2 && top_category_hidden_count > 0 && ret != 0) {
		if (ClearDiscAsset)
			ClearDiscAsset(ctx, asset);
		return 0;
	}
	return ret;
}

/* Wrap vshmain's media remover so a compacted layout also clears the shifted
   column. The callers pass hardcoded NATIVE columns; the item actually sits at
   adjust_topitem_for_hidden_categories(native), so remove there too. Catches
   every removal path (full clear, per-eject UMD/MS) since they all funnel here.
   Runs on vshmain's own thread (same context as the native removal). */
static int RemoveMediaShiftWrap(void *ctx, int topitem, int group)
{
	int ret = RemoveMediaByRelocate(ctx, topitem, group);
	if (ctx && top_category_hidden_count > 0) {
		int adj = adjust_topitem_for_hidden_categories(topitem);
		if (adj != topitem) {
			int r2 = RemoveMediaByRelocate(ctx, adj, group);
			if (r2 > 0)
				ret += r2;
		}
	}
	return ret;
}


static int NetworkDispatchPatched(void *ctx, int a1, int a2, int a3)
{
	int t0_arg;
	int mapped_a3 = a3;
	int adjusted_network_topitem;
	int ret;

	asm volatile("move %0, $t0" : "=r"(t0_arg));

	adjusted_network_topitem = adjust_topitem_for_hidden_categories(6);

	if (!hide_top_category(6) &&
	    count_hidden_top_categories_before(6) > 0 &&
	    a3 == adjusted_network_topitem) {
		mapped_a3 = 6;
	}

	/* Single-hide: PSN's visible column equals Network's native index 6, so the
	   stock handler FUN_0002df34 (hardcoded: param_4==6 -> Network table, and if
	   param_2/a1==6 -> the col-6 network scroll FUN_0002ddbc) would treat the PSN
	   column and a PSN-resting cursor as Network -- running the network scroll on
	   PSN's stale state and freezing during navigation. Remap PSN's visible
	   position to its native index 7 (which FUN_0002df34 ignores) in both the
	   cursor (a1) and target (a3) so the network block is skipped for PSN. */
	if (top_category_hidden_count == 1) {
		int psn_visible = adjust_topitem_for_hidden_categories(7);

		if (a3 == psn_visible)
			mapped_a3 = 7;
		if (a1 == psn_visible)
			a1 = 7;
	}

	asm volatile(
		"move $a0, %1\n"
		"move $a1, %2\n"
		"move $a2, %3\n"
		"move $a3, %4\n"
		"move $t0, %5\n"
		"jalr %6\n"
		"nop\n"
		"move %0, $v0\n"
		: "=r"(ret)
		: "r"(ctx), "r"(a1), "r"(a2), "r"(mapped_a3), "r"(t0_arg),
		  "r"(NetworkDispatchFunc)
		: "memory");

	return ret;
}


static int IconGetTexWrap(void *buf, void *atlas, void *entry)
{
	u32 off = (u32)entry - (u32)atlas;
	u32 entry_addr = (u32)entry;
	void *record = 0;
	u32 mod;
	int loaded = *(int *)((char *)entry + 0x18);


	/* Learn the icon plane layout from the live atlas. The visible-icon priming
	   below depends on the icon_layout_* globals populated here, so this runs
	   unconditionally -- without it the network category icons stay invisible. */
	if (loaded == 0) {
		if (icon_layout_atlas != (u32)atlas) {
			icon_layout_atlas = (u32)atlas;
			icon_layout_main_mod = -1;
			icon_layout_shadow_mod = -1;
			icon_layout_glow_mod = -1;
			icon_layout_prev2_off = 0;
			icon_layout_prev1_off = 0;
		}
		if (icon_layout_prev1_off == off - 0x1C &&
		    icon_layout_prev2_off == off - 0x38 &&
		    icon_layout_main_mod < 0) {
			icon_layout_main_mod = icon_layout_prev2_off % 0x58;
			icon_layout_shadow_mod = icon_layout_prev1_off % 0x58;
			icon_layout_glow_mod = off % 0x58;
		}
		icon_layout_prev2_off = icon_layout_prev1_off;
		icon_layout_prev1_off = off;
	}

	/* The visible icon resolver receives one 0x1C plane block (main/shadow/glow)
	   from a larger 0x58 icon record. Learn that plane layout directly from the
	   live atlas, then lazily ask the stock ensure helper to load the containing
	   record whenever the renderer hits a blank entry. This avoids depending on
	   the wrong global base pointer that the earlier attempt used. */
	if (vsh_text_addr &&
	    EnsureIconEntryFunc &&
	    need_network_icon_prime() &&
	    *(int *)((char *)entry + 0x18) == 0 &&
	    icon_layout_atlas == (u32)atlas &&
	    icon_layout_main_mod >= 0) {
		mod = off % 0x58;
		if (mod == (u32)icon_layout_main_mod)
			record = (void *)(entry_addr - 0x04);
		else if (mod == (u32)icon_layout_shadow_mod)
			record = (void *)(entry_addr - 0x20);
		else if (mod == (u32)icon_layout_glow_mod)
			record = (void *)(entry_addr - 0x3C);

		if (record) {
			EnsureIconEntryFunc(atlas, record);
		}
	}

	return IconGetTex(buf, atlas, entry);
}


void PatchVshMain(u32 text_addr)
{
	vsh_text_addr = text_addr;

	/* Capture whatever the patch[1] JAL currently points at -- the real
	   AddVshItem on stock firmware, or xmbctrl's wrapper on ARK-5/ARK-4
	   (because OnModuleStart has already chained to `previous` before us,
	   so xmbctrl has finished patching). All our wrappers call this
	   pointer, so xmbctrl's CFW item injection stays in the chain. */
	{
		u32 site = text_addr + patch[1];
		u32 jal  = _lw(site);
		AddVshItem = (void *)(((jal & 0x03FFFFFF) << 2) | (site & 0xF0000000));
	}


	/* Prologue-patch the real AddVshItem (Layer 2). Filters xmbctrl's
	   forwarded trigger item so even the first trigger is hideable. */
	xmbctrl_triggered = 0;
	{
		u32 real_addvsh = text_addr + patch[0];
		add_vsh_trampoline[0] = _lw(real_addvsh);
		add_vsh_trampoline[1] = _lw(real_addvsh + 4);
		add_vsh_trampoline[2] = 0x08000000 | (((real_addvsh + 8) >> 2) & 0x03FFFFFF);
		add_vsh_trampoline[3] = 0;

		_sw(0x08000000 | (((u32)AddVshItemFilter >> 2) & 0x03FFFFFF), real_addvsh);
		_sw(0, real_addvsh + 4);
	}

	/* Permanently items */
	MAKE_CALL(text_addr + patch[1], AddVshItemPatched);

	if (topcat_count_patch) {
		MAKE_CALL(text_addr + topcat_count_patch, AdjustTopCategoryCountAndGetCount);
		_sw(0x02402021, text_addr + topcat_count_patch + 4);
	}

	if (devkit == FW(0x660)) {
		TopcatSelectShiftedFunc = (int (*)(void *, int, int))(text_addr + 0x22998);
		TopcatPositionFunc = (int (*)(void *, int))(text_addr + 0x3F4E0);
		/* paf list primitives for the post-boot park-on-Memory-Stick jump
		   (see maybe_jump_game_cursor_to_ms). Same import-stub block as
		   TopcatPositionFunc, resolved by the loader at runtime. */
		ListSetRowFunc = (int (*)(void *, int, int))(text_addr + 0x3F378);
		ListFocusCheckFunc = (int (*)(void))(text_addr + 0x3FB90);
		list_scroll_fwd_addr = text_addr + 0x3F230;
		list_scroll_back_addr = text_addr + 0x3F798;
		list_focus_addr = text_addr + 0x3F640;
		NavigateTopMenuFunc = (void (*)(void *, int, int))(text_addr + 0x2240C);
		FindMediaRowFunc = (int (*)(void *, int, int))(text_addr + 0x2128C);
		if (start_at_ms_flag) {
			/* Speed up NavigateTopMenuFunc's column-slide animation so the wake
			   resume re-entry bounce (see wake_park) is near-instant. 0x22478 is
			   `lui at,0x4284` (66.0f) feeding $f12 to the transition (0x3F6C0) --
			   this is the animation DURATION (bigger = slower; 2000.0f never
			   completed in the wake window). LOWER it to ~8.0f. Only programmatic
			   navigates use 0x2240C (the user's left/right go through paf
			   directly), so normal XMB feel is unchanged. */
			_sw(0x3C014100, text_addr + 0x22478);   /* lui at,0x4100 (8.0f) */
			sceKernelDcacheWritebackAll();
			kuKernelIcacheInvalidateAll();
		}
		/* Disc-focus column fix: the only 0x2240C caller (see
		   NavigateTopMenuAdjusted). */
		MAKE_CALL(text_addr + 0x222FC, NavigateTopMenuAdjusted);
		/* NOTE: adjusting the FUN_0002128C row-lookup callsites inside
		   FUN_0002220C (0x22254/0x22310/0x22338) made the compacted
		   disc-focus dispatch survive, but broke the UMD video ICON0
		   preview -- 0x2128C is apparently stateful beyond the row
		   lookup (native-col keyed disc/preview state). REVERTED; the
		   compacted disc-focus stays non-dispatching (its historical
		   behavior), covered by the boot watchdog / wake park instead. */
		/* Resolve paf's set-row-with-relayout for clean snaps (see the
		   declaration). paf is 6.60's scePaf_Module, loaded before
		   vshmain. */
		{
			SceModule2 paf_mod;
			if (kuKernelFindModuleByName("scePaf_Module", &paf_mod) >= 0 &&
			    paf_mod.text_addr) {
				paf_list_setrow_addr = paf_mod.text_addr + 0x10DD1C;
			}
		}

		/* Boot window for the START_AT disc-focus redirect -- armed only
		   when a disc is ALREADY in the drive: with an empty tray, any
		   later disc focus is a genuine hot-insert and must jump to the
		   disc natively no matter how early it happens (hardware-hit:
		   inserting a movie disc 17s after boot got mis-redirected). */
		if (start_at_ms_flag && sceUmdCheckMedium() > 0) {
			ms_park_redirect_until =
				sceKernelGetSystemTimeLow() + 45000000;
			user_left_game_during_redirect = 0;
		}
		TrackColumnIconKey = (int (*)(void *, int, const char *))(text_addr + 0x2EDBC);
		NetworkDispatchFunc = (int (*)(void *, int, int, int))(text_addr + 0x2DF34);
		EnsureIconEntryFunc = (int (*)(void *, void *))(text_addr + 0x2EA7C);
		FinalizeIconEntryFunc = (int (*)(void *, void *, int, int))(text_addr + 0x2EBE4);
		MAKE_CALL(text_addr + 0x22C7C, UmdVideoAddPatchedRet);
		MAKE_CALL(text_addr + 0x22C8C, UmdVideoSelectShiftPatched);
		MAKE_CALL(text_addr + 0x22CDC, UmdVideoAddPatchedRet);
		MAKE_CALL(text_addr + 0x22CEC, UmdVideoSelectShiftPatched);
		MAKE_CALL(text_addr + 0x22D5C, UmdGameAddPatchedRet);
		MAKE_CALL(text_addr + 0x22DA4, UmdGameSelectShiftPatched);
		MAKE_CALL(text_addr + 0x22EC0, UmdGameAddPatchedRet);
		MAKE_CALL(text_addr + 0x22ED0, UmdGameSelectShiftPatched);
		MAKE_CALL(text_addr + 0x22DF0, UmdTopcatPositionShiftPatched);
		MAKE_CALL(text_addr + 0x22E0C, UmdTopcatPositionShiftPatched);
		MAKE_CALL(text_addr + 0x22E28, UmdTopcatPositionShiftPatched);
		MAKE_CALL(text_addr + 0x22E60, UmdTopcatPositionShiftPatched);
		MAKE_CALL(text_addr + 0x22F28, UmdTopcatPositionShiftPatched);
		MAKE_CALL(text_addr + 0x22F44, UmdTopcatPositionShiftPatched);
		MAKE_CALL(text_addr + 0x22F60, UmdTopcatPositionShiftPatched);
		MAKE_CALL(text_addr + 0x22F98, UmdTopcatPositionShiftPatched);
		MAKE_CALL(text_addr + 0x23028, UmdTopcatPositionShiftPatched);
		MAKE_CALL(text_addr + 0x23048, UmdTopcatPositionShiftPatched);
		MAKE_CALL(text_addr + 0x23064, UmdTopcatPositionShiftPatched);
		MAKE_CALL(text_addr + 0x230A0, UmdTopcatPositionShiftPatched);
		MAKE_CALL(text_addr + 0x23138, UmdTopcatPositionShiftPatched);
		MAKE_CALL(text_addr + 0x23154, UmdTopcatPositionShiftPatched);
		MAKE_CALL(text_addr + 0x23170, UmdTopcatPositionShiftPatched);
		MAKE_CALL(text_addr + 0x231A8, UmdTopcatPositionShiftPatched);
		MAKE_CALL(text_addr + 0x2261C, TrackColumnIconKeyPatched);

		/* Both Network-only preload loops are hardcoded to a 5-slot window.
		   When left-side category compaction shifts Network, the active early
		   loop starts at the queue tail and only Browser/Search ever get
		   textures. Move both the slot counter and the queue pointer back to
		   the queue head. Network resolves 7 usable entries on this firmware
		   (indices 252,253,255,256,257,258,259), so extend the loader window
		   to 7 now that it starts from the correct head. */
		_sw(0x00609021, text_addr + 0x2CE30);  /* move   s2, v1 */
		_sw(0x24100000, text_addr + 0x2CE40);  /* addiu  s0, zr, 0 */
		_sw(0x00A03821, text_addr + 0x2CE78);  /* move   a3, a1 */
		_sw(0x2A220007, text_addr + 0x2CE94);  /* slti  v0, s1, 7 */
		/* THE single-hide crash: FUN_0002cc9c (this same hardcoded col-6 network
		   icon-finalize loop, called right after the 0x208xx column renderer) is
		   what actually freezes the boot. With network hidden AND exactly one
		   category hidden, the runtime count is 7 so the loop treats column 6 as
		   active, but Network is the hidden category -- its icon queue is
		   stale/empty and the widened (above) window walks it into a wild
		   FinalizeIconEntry. The loop's only exit is `beqz bound, 0x2cea8` at
		   0x2CE58; force it unconditional (`b 0x2cea8`) so the loop is skipped
		   entirely. This applies to ANY single hide (count==1): whether Network
		   is the hidden category (no column to draw) or merely shifted (photo/
		   music/video hidden), the network icon queue (text+0x7320/0x7330/0x7344)
		   comes up as garbage in the count==7 layout, so walking it crashes
		   either way. Multi-hide (count<=6) keeps the stock conditional branch. */
		/* FUN_0002e2bc's category-6 (network) icon-finalize block: widen its
		   window to 7 from the queue head so a visible-but-shifted Network
		   resolves all its icons. */
		_sw(0x00608821, text_addr + 0x2E854);  /* move   s1, v1 */
		_sw(0x24100000, text_addr + 0x2E858);  /* addiu  s0, zr, 0 */
		_sw(0x00A03821, text_addr + 0x2E89C);  /* move   a3, a1 */
		_sw(0x2A820007, text_addr + 0x2E8B4);  /* slti  v0, s4, 7 */
		/* Single-hide (count==1): the network icon queue is garbage in the
		   count==7 layout, so skip BOTH hardcoded col-6 network finalize loops
		   that would walk it -- FUN_0002cc9c (boot crash) at 0x2CE58 and the
		   analogous FUN_0002e2bc loop (reached on navigation) at 0x2E878 -- by
		   forcing each loop's bound-check branch to its exit unconditionally. */
		if (top_category_hidden_count == 1) {
			_sw(0x10000013, text_addr + 0x2CE58);  /* b 0x2cea8 (FUN_0002cc9c) */
			_sw(0x10000013, text_addr + 0x2E878);  /* b 0x2e8c8 (FUN_0002e2bc) */
		}
		/* Only hook the network dispatcher when categories are actually hidden:
		   both of NetworkDispatchPatched's remap branches require a hidden
		   category, so at count==0 it's a pure passthrough whose hand-written
		   asm trampoline (undeclared clobbers + cross-module jalr) wedges
		   vshmain right after it returns during a theme rebuild -- the crash
		   confirmed by the heartbeat freezing on mark=NET-ret. Leaving the stock
		   jal in place at count==0 is exactly the passthrough behaviour, minus
		   the crash. */
		if (top_category_hidden_count > 0)
			MAKE_CALL(text_addr + 0x16538, NetworkDispatchPatched);
		/* Make media-item removal compaction-aware: wrap every call site of the
		   remover (FUN_00023468) so it also clears the shifted column. */
		RemoveMediaByRelocate = (int (*)(void *, int, int))(text_addr + 0x23468);
		MAKE_CALL(text_addr + 0x203c8, RemoveMediaShiftWrap);
		MAKE_CALL(text_addr + 0x203d8, RemoveMediaShiftWrap);
		MAKE_CALL(text_addr + 0x203e8, RemoveMediaShiftWrap);
		MAKE_CALL(text_addr + 0x203f8, RemoveMediaShiftWrap);
		MAKE_CALL(text_addr + 0x20408, RemoveMediaShiftWrap);
		MAKE_CALL(text_addr + 0x20418, RemoveMediaShiftWrap);
		MAKE_CALL(text_addr + 0x20428, RemoveMediaShiftWrap);
		MAKE_CALL(text_addr + 0x20438, RemoveMediaShiftWrap);
		MAKE_CALL(text_addr + 0x20448, RemoveMediaShiftWrap);
		MAKE_CALL(text_addr + 0x205d4, RemoveMediaShiftWrap);
		MAKE_CALL(text_addr + 0x205e4, RemoveMediaShiftWrap);
		MAKE_CALL(text_addr + 0x205f4, RemoveMediaShiftWrap);
		MAKE_CALL(text_addr + 0x20604, RemoveMediaShiftWrap);
		MAKE_CALL(text_addr + 0x20614, RemoveMediaShiftWrap);
		MAKE_CALL(text_addr + 0x23680, RemoveMediaShiftWrap);
		MAKE_CALL(text_addr + 0x23690, RemoveMediaShiftWrap);
		MAKE_CALL(text_addr + 0x236a0, RemoveMediaShiftWrap);
		MAKE_CALL(text_addr + 0x24138, RemoveMediaShiftWrap);
		MAKE_CALL(text_addr + 0x24148, RemoveMediaShiftWrap);
		MAKE_CALL(text_addr + 0x24158, RemoveMediaShiftWrap);
		MAKE_CALL(text_addr + 0x24168, RemoveMediaShiftWrap);
		MAKE_CALL(text_addr + 0x24178, RemoveMediaShiftWrap);
		MAKE_CALL(text_addr + 0x2a3e8, RemoveMediaShiftWrap);
		MAKE_CALL(text_addr + 0x2a3f8, RemoveMediaShiftWrap);
		MAKE_CALL(text_addr + 0x2a408, RemoveMediaShiftWrap);
		MAKE_CALL(text_addr + 0x2a418, RemoveMediaShiftWrap);
		MAKE_CALL(text_addr + 0x2a428, RemoveMediaShiftWrap);
		MAKE_CALL(text_addr + 0x2ae4c, RemoveMediaShiftWrap);
		/* FIX: wrap the disc-preview "asset needed?" check at all 12 sites to
		   force the ICON0 load when categories are hidden. */
		DiscAssetNeeded = (int (*)(void *, int))(text_addr + 0x26334);
		ClearDiscAsset = (int (*)(void *, int))(text_addr + 0x290B4);
		MAKE_CALL(text_addr + 0x1aae0, DiscAssetNeededWrap);
		MAKE_CALL(text_addr + 0x1ab34, DiscAssetNeededWrap);
		MAKE_CALL(text_addr + 0x1ac84, DiscAssetNeededWrap);
		MAKE_CALL(text_addr + 0x1acc0, DiscAssetNeededWrap);
		MAKE_CALL(text_addr + 0x1ad64, DiscAssetNeededWrap);
		MAKE_CALL(text_addr + 0x1adbc, DiscAssetNeededWrap);
		MAKE_CALL(text_addr + 0x1ae18, DiscAssetNeededWrap);
		MAKE_CALL(text_addr + 0x1ae74, DiscAssetNeededWrap);
		MAKE_CALL(text_addr + 0x1aeac, DiscAssetNeededWrap);
		MAKE_CALL(text_addr + 0x1aee4, DiscAssetNeededWrap);
		MAKE_CALL(text_addr + 0x1af1c, DiscAssetNeededWrap);
		MAKE_CALL(text_addr + 0x1af54, DiscAssetNeededWrap);
		IconGetTex = (int (*)(void *, void *, void *))(text_addr + 0x2D7D4);
		MAKE_CALL(text_addr + 0x2D8A0, IconGetTexWrap);

	}

	/* Photo Memory Stick */
	MAKE_CALL(text_addr + patch[2], AddVshItemPatchedPhoto);

	/* Music Memory Stick */
	MAKE_CALL(text_addr + patch[3], AddVshItemPatchedMusic);

	/* Video Memory Stick */
	MAKE_CALL(text_addr + patch[4], AddVshItemPatchedVideo);

	/* Game Memory Stick */
	MAKE_CALL(text_addr + patch[5], AddVshItemPatchedGame);

	if(psp_model == 4)
	{
		/* Go Game Extra Flash (ef) SaveData */
		MAKE_CALL(text_addr + patch[6], AddVshItemPatchedGameSavedataMs);

		/* Go Photo Extra Flash (ef) */
		MAKE_CALL(text_addr + patch[7], AddVshItemPatchedPhoto);

		/* Go Music Extra Flash (ef) */
		MAKE_CALL(text_addr + patch[8], AddVshItemPatchedMusic);

		/* Go Video Extra Flash (ef) */
		MAKE_CALL(text_addr + patch[9], AddVshItemPatchedVideo);

		/* Go Game Extra Flash (ef)*/
		MAKE_CALL(text_addr + patch[10], AddVshItemPatchedGame);

		/* Go Game Memory Stick SaveData */
		MAKE_CALL(text_addr + patch[11], AddVshItemPatchedGameSavedataEf);

		/* Go Game Resume Game */
		MAKE_CALL(text_addr + patch[12], AddVshItemPatched);
	}
	else
	{
		/* Hide UMD Update Icon (from UVMR by TN).
		   umdIoOpenPatched is defined at file scope (see top of file); the
		   original nested-function form crashed on modern psp-gcc because
		   the function pointer was a stack-resident trampoline that the
		   isofs driver kept calling after PatchVshMain's frame went away. */
		PspIoDrv* umddrv = sctrlHENFindDriver("isofs");
		umdIoOpen = umddrv->funcs->IoOpen;
		umddrv->funcs->IoOpen = umdIoOpenPatched;
	}

	PatchTopCategories(text_addr);

	ClearCaches();
}

int OnModuleStart(SceModule2 *mod)
{
	/* Chain to `previous` FIRST. ARK-5's xmbctrl registers later than
	   us (it's loaded by VSHControl's StartModuleHandler right as
	   vsh_module is starting), so without this our PatchVshMain would
	   run before xmbctrl had hooked the JAL sites and our captured
	   `next` pointers would be the original AddVshItem rather than
	   xmbctrl's wrapper -- the CFW menu injection would still get
	   skipped. */
	int ret = previous ? previous(mod) : 0;

	char *modname = mod->modname;
	u32 text_addr = mod->text_addr;

	if(strcmp(modname, "vsh_module") == 0) {
		vsh_seg1_addr = mod->segmentaddr[1];
		vsh_seg1_size = mod->segmentsize[1];
		PatchVshMain(text_addr);
	}

	return ret;
}

int module_start(SceSize args, void *argp)
{
	devkit = sceKernelDevkitVersion();
	psp_model = kuKernelGetModel();

	/* Make ini Path */
	strcpy(ini_path, argp);
	strrchr(ini_path, '/')[1] = 0;
	strcpy(ini_path + strlen(ini_path), "xmbih.ini");





	start_power_callbacks();

	/* Global */
	set[0] = cfg(global_category, "HIDE_ALL_SETTINGS");
	set[1] = cfg(global_category, "HIDE_ALL_EXTRAS");
	if (set[1] == 2) {
		set[1] = 1;
	}
	set[2] = cfg(global_category, "HIDE_ALL_PHOTO");
	set[3] = cfg(global_category, "HIDE_ALL_MUSIC");
	set[4] = cfg(global_category, "HIDE_ALL_GAME");
	set[5] = cfg(global_category, "HIDE_ALL_NETWORK");
	set[6] = cfg(global_category, "HIDE_ALL_PSN");	
	set[56] = cfg(global_category, "HIDE_ALL_VIDEO");
	set[54] = cfg(global_category, "HIDE_ALL");
	set[55] = cfg(global_category, "USE_PLUGIN");
	set[57] = cfg(global_category, "MOVE_ARK_EXTRAS");
	start_at_ms_flag = cfg(global_category, "START_AT_MEMORY_STICK");
	if (start_at_ms_flag) {
		/* START_AT_MEMORY_STICK stays active regardless of an inserted UMD
		   (movie or game). A disc only disables the UMD Update entry, not the
		   Memory-Stick park target or the ARK item ordering. */
		boot_hide_for_ms = 1;
		/* Game's displayed index = 5 minus any hidden pre-Game categories,
		   so the XMB-ready signal matches regardless of what's hidden. */
		start_ms_game_index = adjust_topitem_for_hidden_categories(5);

	}
	boot_umd_defer_active = should_defer_boot_umd_add() && sceUmdCheckMedium() > 0;
	boot_umd_defer_captured = 0;
	boot_umd_defer_thread_started = 0;
	boot_umd_a0 = 0;
	boot_umd_topitem = 0;

	/* Settings */
	set[7] = cfg(settings_category, "SYSTEM_UPDATE");
	set[8] = cfg(settings_category, "USB");
	set[9] = cfg(settings_category, "VIDEO");
	set[10] = cfg(settings_category, "PHOTO");
	set[11] = cfg(settings_category, "SYSTEM");
	set[12] = cfg(settings_category, "THEME");
	set[13] = cfg(settings_category, "DATE");
	set[14] = cfg(settings_category, "POWERSAVE");
	set[15] = cfg(settings_category, "BLUETOOTH");
	set[16] = cfg(settings_category, "DISPLAY");
	set[17] = cfg(settings_category, "SOUND");
	set[18] = cfg(settings_category, "SECURITY");
	set[19] = cfg(settings_category, "RSS");
	set[20] = cfg(settings_category, "NETWORK");

	/* Extras */
	set[21] = cfg(extras_category, "1SEG");
	set[22] = cfg(extras_category, "T-DMB");
	set[23] = cfg(extras_category, "JP_BOOKREADER");
	set[24] = cfg(extras_category, "DIGITAL_COMICS");
	set[26] = cfg(extras_category, "X-RADAR_PORTABLE");
	/* ARK-4/ARK-5 CFW menu items (same text keys on both). Set to 1 to hide;
	   a hidden item is never relocated by MOVE_ARK_EXTRAS. */
	set[59] = cfg(extras_category, "CUSTOM_FIRMWARE_SETTINGS");
	set[60] = cfg(extras_category, "PLUGINS_MANAGER");
	set[61] = cfg(extras_category, "CUSTOM_LAUNCHER");

	/* Photo */
	set[27] = cfg(photo_category, "CAMERA");
	set[28] = cfg(photo_category, "MEMORY_STICK");
	set[29] = cfg(photo_category, "SYSTEM_STORAGE");

	/* Music */
	set[25] = cfg(music_category, "MUSIC_UNLIMITED");
	set[30] = cfg(music_category, "SENSME_CHANNELS");
	set[31] = cfg(music_category, "MEMORY_STICK");
	set[32] = cfg(music_category, "SYSTEM_STORAGE");

	/* Video */
	set[33] = cfg(video_category, "MEMORY_STICK");
	set[34] = cfg(video_category, "SYSTEM_STORAGE");

	/* Game */
	set[35] = cfg(game_category, "GAME_SHARING");
	set[58] = cfg(game_category, "UMD_UPDATE");
	set[36] = cfg(game_category, "SAVED_DATA_UTILITY_MS");
	set[37] = cfg(game_category, "SAVED_DATA_UTILITY_EF");
	set[38] = cfg(game_category, "RESUME_GAME");
	set[39] = cfg(game_category, "MEMORY_STICK");
	set[40] = cfg(game_category, "SYSTEM_STORAGE");

	/* Network */
	set[41] = cfg(network_category, "ONLINE_MANUAL");
	set[42] = cfg(network_category, "LOCATION_FREE_PLAYER");
	set[43] = cfg(network_category, "SKYPE");
	set[44] = cfg(network_category, "REMOTE_PLAY");
	set[45] = cfg(network_category, "INTERNET_RADIO");
	set[46] = cfg(network_category, "RSS_CHANNEL");
	set[47] = cfg(network_category, "INTERNET_BROWSER");
	set[48] = cfg(network_category, "INTERNET_SEARCH");
	set[49] = cfg(network_category, "PLAYSTATION_SPOT");
	set[50] = cfg(network_category, "GO_MESSENGER");

	/* PlayStation�Network */
	set[51] = cfg_psn("SIGN_UP_OR_ACCOUNT_MANAGEMENT");
	set[52] = cfg_psn("PLAYSTATION_STORE");
	set[53] = cfg_psn("INFORMATION_BOARD");

	/** 
	* Offsets for patches (for update use prxtool)
	* @ patch[0] - AddVshItem
	* @ patch[1] - AddVshItemPatched
	*/

	switch(devkit)
	{
		case FW(0x500):
		case FW(0x502):
		case FW(0x503):
			/* Frostegater */
			patch[0] = 0x1C468; 
			patch[1] = 0x1AF40;
			patch[2] = 0x1D878;
			patch[3] = 0x1D958;
			patch[4] = 0x1DA38;
			patch[5] = 0x1DB18;
			break;
				
		case FW(0x550):
			/* Frostegater */
			patch[0] = 0x1D35C;
			patch[1] = 0x1BD24;
			patch[2] = 0x1E67C;
			patch[3] = 0x1E75C;
			patch[4] = 0x1E83C;
			patch[5] = 0x1E91C;
			break;

		case FW(0x620):
			/* Total_Noob */
			patch[0] = 0x21E18;
			patch[1] = 0x206F8;
			/* Frostegater */
			patch[2] = 0x231E8;
			patch[3] = 0x232C8;
			patch[4] = 0x233A8;
			patch[5] = 0x2348C;
			patch[6] = 0x235EC;
			patch[7] = 0x29944;
			patch[8] = 0x29954;
			patch[9] = 0x29964;
			patch[10] = 0x29978;
			patch[11] = 0x29988;
			patch[12] = 0x2A4A8;
			break;

		case FW(0x635):
		case FW(0x636):
		case FW(0x637):
		case FW(0x638):		
		case FW(0x639):
			/* Total_Noob */
			patch[0] = 0x22608;
			patch[1] = 0x20EBC;
			/* Frostegater */
			patch[2] = 0x239D8;
			patch[3] = 0x23AB8;
			patch[4] = 0x23B98;
			patch[5] = 0x23C7C;
			patch[6] = 0x23DDC;
			patch[7] = 0x2A1B4;
			patch[8] = 0x2A1C4;
			patch[9] = 0x2A1D4;
			patch[10] = 0x2A1E8;
			patch[11] = 0x2A1F8;
			patch[12] = 0x2AD18;
			break;

		case FW(0x660):
			/* codestation */
			patch[0] = 0x22648;
			patch[1] = 0x20EFC;
			topcat_count_patch = 0x20890;
			/* Frostegater */
			patch[2] = 0x23A44;
			patch[3] = 0x23B24;
			patch[4] = 0x23C04;
			patch[5] = 0x23CE8;
			patch[6] = 0x23E48;
			patch[7] = 0x2A240;
			patch[8] = 0x2A250;
			patch[9] = 0x2A260;
			patch[10] = 0x2A274;
			patch[11] = 0x2A284;
			patch[12] = 0x2ADA4;
			break;
			
		default:
			return -1;
	}

	top_category_hidden_count = get_top_category_hidden_count();
	top_category_runtime_obj = 0;

	if (!set[55]) {
		return 0;
	}

	previous = sctrlHENSetStartModuleHandler(OnModuleStart);


	return 0;
}

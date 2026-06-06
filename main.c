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
#include <systemctrl.h>
#include <systemctrl_se.h>
#include <main.h>
#include <kubridge.h>
#include <string.h>
#include <stdarg.h>
#include "minGlue.h"
#include "minIni.h"

PSP_MODULE_INFO("XMBIH", 0x0007, 1, 3);

static struct {
	volatile unsigned char guard[0x200];
	volatile unsigned char flags[59];
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

static void xlog_raw_both(const char *msg)
{
	(void)msg;
}

static void xlog(const char *fmt, ...)
{
	(void)fmt;
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

/* Used by AddVshItemFilter below (START_AT_MEMORY_STICK), but the rest of the
   feature's state is defined further down. */
static int adjust_topitem_for_hidden_categories(int topitem);  /* defined below */
static volatile int boot_hide_for_ms = 0;
static SceVshItem captured_ark[5];
static volatile int captured_ark_count = 0;

int AddVshItemFilter(void *a0, int topitem, SceVshItem *item)
{
	int adjusted_topitem = topitem;

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
		return trampoline(a0, adjusted_topitem, item);
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
static SceVshItem captured_umd;            /* inserted UMD disc (msgshare_umd) */
static volatile int gamedl_captured = 0;
static volatile int savedata_captured = 0;
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

static int start_at_ms_thread(SceSize args, void *argp)
{
	u32 obj;
	int i;
	(void)args; (void)argp;

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
		if (umd_captured)
			AddVshItem((void *)game_a0, game_topitem, &captured_umd);
		if (gamedl_captured)
			AddVshItem((void *)game_a0, game_topitem, &captured_gamedl);
		if (savedata_captured)
			AddVshItem((void *)game_a0, game_topitem, &captured_savedata);
	}
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
char *playstation_network_category = "PlayStation\xAENetwork";

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

static int top_category_hidden_count;
static int top_category_count_logged;
static u32 top_category_runtime_obj;
static int top_category_runtime_return_override_logged;

void ClearCaches()
{
	sceKernelDcacheWritebackAll();
	kuKernelIcacheInvalidateAll();
}

int cfg(char *category, char *fmt)
{
	return ini_getlhex(category, fmt, 0, ini_path);
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
 *   HIDE_ALL_EXTRAS = 2 hides the Extras top category and relocates ARK's
 *   injected CFW items to Game (wad's behavior).
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

/* Extras column is gone this boot -- either =2's count-patch hides it, or the
   active fake VSH region does. MOVE_ARK_EXTRAS does not hide it; it only
   changes where ARK's own injected items land. */
static int extras_is_hidden(void)
{
	return set[1] == 2 || extras_hidden_by_region();
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
			/* Only =2 hides the Extras column via the count-patch (and only
			   when a fake region isn't already hiding it). MOVE_ARK_EXTRAS
			   never count-patches: it only relocates ARK's own items. */
			return (set[1] == 2) && !extras_hidden_by_region();
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

static int top_category_mode(int index)
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
				return 2;
			default:
				return 0;
		}
	}

	switch (index) {
		case 1:
			return set[1];
		case 2:
			return set[2];
		case 3:
			return set[3];
		case 4:
			return set[56];
		case 5:
			return set[4];
		case 6:
			return set[5];
		case 7:
			return set[6];
	}

	return 0;
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
	u32 *slots;
	int count;
	int new_count;

	scene_ctx = (u32)ctx;   /* expose scene context to the START_AT_MEMORY_STICK thread */

	obj = *(u32 *)((char *)ctx + 0xA6C);
	if (!obj)
		return 0;

	count = *(int *)(obj + 0x334);
	if (top_category_hidden_count <= 0)
		return count;

	if (obj != top_category_runtime_obj) {
		u32 arr330;

		slots = (u32 *)(obj + 0x360);
		arr330 = *(u32 *)(obj + 0x330);
		xlog("topcat: obj hdr 330=%08X 334=%08X 338=%08X 33C=%08X 340=%08X 344=%08X 348=%08X 34C=%08X 350=%08X 354=%08X 358=%08X 35C=%08X\n",
			arr330,
			*(u32 *)(obj + 0x334),
			*(u32 *)(obj + 0x338),
			*(u32 *)(obj + 0x33C),
			*(u32 *)(obj + 0x340),
			*(u32 *)(obj + 0x344),
			*(u32 *)(obj + 0x348),
			*(u32 *)(obj + 0x34C),
			*(u32 *)(obj + 0x350),
			*(u32 *)(obj + 0x354),
			*(u32 *)(obj + 0x358),
			*(u32 *)(obj + 0x35C));
		if (arr330) {
			xlog("topcat: arr330=%08X [%08X,%08X,%08X,%08X,%08X,%08X,%08X,%08X]\n",
				arr330,
				*(u32 *)(arr330 + 0x00),
				*(u32 *)(arr330 + 0x04),
				*(u32 *)(arr330 + 0x08),
				*(u32 *)(arr330 + 0x0C),
				*(u32 *)(arr330 + 0x10),
				*(u32 *)(arr330 + 0x14),
				*(u32 *)(arr330 + 0x18),
				*(u32 *)(arr330 + 0x1C));
		}
		xlog("topcat: slots=%08X [%08X,%08X,%08X,%08X,%08X,%08X,%08X,%08X]\n",
			(u32)slots,
			slots[0], slots[1], slots[2], slots[3],
			slots[4], slots[5], slots[6], slots[7]);
		top_category_runtime_obj = obj;
		xlog("topcat: runtime slots left unchanged obj=0x%08X slots=0x%08X\n",
			obj, (u32)slots);
		xlog("topcat: ctx e70=%08X,%08X,%08X,%08X,%08X,%08X\n",
			*(u32 *)((char *)ctx + 0xE70),
			*(u32 *)((char *)ctx + 0xE74),
			*(u32 *)((char *)ctx + 0xE78),
			*(u32 *)((char *)ctx + 0xE7C),
			*(u32 *)((char *)ctx + 0xE80),
			*(u32 *)((char *)ctx + 0xE84));
		xlog("topcat: ctx e88=%08X,%08X,%08X,%08X\n",
			*(u32 *)((char *)ctx + 0xE88),
			*(u32 *)((char *)ctx + 0xE94),
			*(u32 *)((char *)ctx + 0xEA0),
			*(u32 *)((char *)ctx + 0xEAC));
	}

	new_count = count - top_category_hidden_count;
	if (new_count < 1)
		new_count = 1;

	if (count > new_count) {
		if (hide_top_category(0)) {
			if (!top_category_runtime_return_override_logged) {
				top_category_runtime_return_override_logged = 1;
				xlog("topcat: settings-hidden skip runtime count patch %d -> %d\n",
					count, new_count);
			}
			return count;
		}

		*(int *)(obj + 0x334) = new_count;
		if (!top_category_count_logged) {
			top_category_count_logged = 1;
			xlog("topcat: runtime count patched %d -> %d\n", count, new_count);
		}
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
		xlog("topcat: table not found\n");
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
	   vshmain's UMD-preview path still performs some hardcoded lookups by
	   original category index, so those backing arrays need valid data in
	   their original slots.

	   Keep the visible table tail blanked, though. If the runtime count
	   trim does not fire on a given setup, preserving table tail entries
	   makes a full category hide look like an ordinary empty category.
	   Zeroing only the visible table tail preserves "2 = hide category"
	   while still keeping the meta arrays available for original-index
	   firmware lookups. */

	meta0 = (u32 *)((char *)table - (8 * sizeof(u32) * 3));
	meta1 = (u32 *)((char *)table - (8 * sizeof(u32) * 2));
	meta2 = (u32 *)((char *)table - (8 * sizeof(u32) * 1));

	for (i = 0; i < 8; i++) {
		int hide = hide_top_category(i);
		if (hide) {
			changed = 1;
			xlog("topcat: hiding '%s' mode=%d\n", table[i].text,
				top_category_mode(i));
			continue;
		}

		filtered_meta0[out] = meta0[i];
		filtered_meta1[out] = meta1[i];
		filtered_meta2[out] = meta2[i];
		memcpy(&filtered[out], &table[i], sizeof(filtered[out]));
		out++;
	}

	if (!changed) {
		xlog("topcat: no supported category set to 2\n");
		return;
	}

	while (out < 8) {
		filtered_meta0[out] = meta0[out];
		filtered_meta1[out] = meta1[out];
		filtered_meta2[out] = meta2[out];
		memset(&filtered[out], 0, sizeof(filtered[out]));
		out++;
	}

	memcpy(meta0, filtered_meta0, sizeof(filtered_meta0));
	memcpy(meta1, filtered_meta1, sizeof(filtered_meta1));
	memcpy(meta2, filtered_meta2, sizeof(filtered_meta2));
	memcpy(table, filtered, sizeof(filtered));
	xlog("topcat: meta0=0x%08X meta1=0x%08X meta2=0x%08X table=0x%08X compacted\n",
		(u32)meta0, (u32)meta1, (u32)meta2, (u32)table);
}

int skip(SceVshItem *item, int location)
{
	int idnm(char *name)
	{
		return strcmp(item->text, name);
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

	/* START_AT_MEMORY_STICK boot-hide. During the boot pass, force-hide the fixed
	   Game items that precede Memory Stick (Game Sharing, Saved Data Utility,
	   and -- when a disc is inserted -- the UMD item) so the cursor lands on
	   MS. We capture a copy of each first; once the XMB is ready the post-boot
	   thread clears boot_hide_for_ms and re-adds them directly via AddVshItem
	   (see start_at_ms_thread). */
	if (boot_hide_for_ms &&
	    (!idnm("msgtop_game_savedata") || !idnm("msgtop_game_gamedl") ||
	     !idnm("msgshare_umd"))) {
		if (!idnm("msgtop_game_gamedl") && !gamedl_captured) {
			memcpy(&captured_gamedl, item, sizeof(SceVshItem));
			gamedl_captured = 1;
		}
		if (!idnm("msgtop_game_savedata") && !savedata_captured) {
			memcpy(&captured_savedata, item, sizeof(SceVshItem));
			savedata_captured = 1;
		}
		if (!idnm("msgshare_umd") && !umd_captured) {
			memcpy(&captured_umd, item, sizeof(SceVshItem));
			umd_captured = 1;
		}
		return 0;
	}

	if (!idnm("msg_signup") || !idnm("msg_ps_store") || !idnm("msg_information_board")) {
		xlog("psn state: text='%s' loc=%d s6=%d s51=%d s52=%d s53=%d s54=%d\n",
			item->text, location, set[6], set[51], set[52], set[53], set[54]);
	}

	if (!idnm("msg_game_hibernation")) {
		xlog("hibernation state: loc=%d s38=%d s4=%d s54=%d\n",
			location, set[38], set[4], set[54]);
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

static int xlog_hook(int loc, SceVshItem *item)
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

static int is_ark_custom_item(const char *text)
{
	return !strcmp(text, "xmbmsgtop_sysconf_configuration") ||
		!strcmp(text, "xmbmsgtop_sysconf_plugins") ||
		!strcmp(text, "xmbmsgtop_custom_launcher") ||
		!strcmp(text, "xmbmsgtop_custom_app") ||
		!strcmp(text, "xmbmsgtop_150_reboot");
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
		if (!remap_ark_topitem(item->text, incoming_topitem, out_topitem)) {
			xlog("%s ark item: text='%s' topitem=%d dropped\n",
				source, item->text, incoming_topitem);
			return 0;
		}

		/* When these two items move into Settings, fix their icon keys for
		   that column's atlas. */
		if (move_extra_items_mode() && is_settings_bound_ark_item(item->text))
			reloc_fix_settings_icon(item);

		xlog("%s ark item: text='%s' topitem=%d adjusted=%d move=%d\n",
			source, item->text, incoming_topitem, *out_topitem,
			move_extra_items_mode());
		return 1;
		}

	*out_topitem = adjust_topitem_for_hidden_categories(incoming_topitem);

	if (is_game_resume_item(item->text)) {
		xlog("%s resume item: text='%s' topitem=%d adjusted=%d\n",
			source, item->text, incoming_topitem, *out_topitem);
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
	{
		int adjusted_topitem;

		if (!prepare_topitem_for_item(item, topitem, &adjusted_topitem, "wrapper"))
			return 0;

		topitem = adjusted_topitem;
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
	   Subsequent triggers fall through to xlog_hook (skip()) so the user
	   can hide them. The prologue patch above, if it takes effect, also
	   filters this first trigger's forward so even it gets hidden. */
	int force_trigger = is_xmbctrl_trigger(item->text) && !xmbctrl_triggered;
	if(force_trigger) xmbctrl_triggered = 1;
	if(force_trigger || xlog_hook(0, item))
		return AddVshItem(a0, topitem, item);

	return 0;
}

int AddVshItemPatchedPhoto(void *a0, int topitem, SceVshItem *item)
{
	topitem = adjust_topitem_for_hidden_categories(topitem);
	int force_trigger = is_xmbctrl_trigger(item->text) && !xmbctrl_triggered;
	if(force_trigger) xmbctrl_triggered = 1;
	if(force_trigger || xlog_hook(1, item))
		return AddVshItem(a0, topitem, item);

	return 0;
}

int AddVshItemPatchedMusic(void *a0, int topitem, SceVshItem *item)
{
	topitem = adjust_topitem_for_hidden_categories(topitem);
	int force_trigger = is_xmbctrl_trigger(item->text) && !xmbctrl_triggered;
	if(force_trigger) xmbctrl_triggered = 1;
	if(force_trigger || xlog_hook(2, item))
		return AddVshItem(a0, topitem, item);

	return 0;
}

int AddVshItemPatchedVideo(void *a0, int topitem, SceVshItem *item)
{
	topitem = adjust_topitem_for_hidden_categories(topitem);
	int force_trigger = is_xmbctrl_trigger(item->text) && !xmbctrl_triggered;
	if(force_trigger) xmbctrl_triggered = 1;
	if(force_trigger || xlog_hook(3, item))
		return AddVshItem(a0, topitem, item);

	return 0;
}

int AddVshItemPatchedGame(void *a0, int topitem, SceVshItem *item)
{
	topitem = adjust_topitem_for_hidden_categories(topitem);
	int force_trigger = is_xmbctrl_trigger(item->text) && !xmbctrl_triggered;
	if(force_trigger) xmbctrl_triggered = 1;
	if(force_trigger || xlog_hook(4, item))
		return AddVshItem(a0, topitem, item);

	return 0;
}

int AddVshItemPatchedGameSavedataMs(void *a0, int topitem, SceVshItem *item)
{
	topitem = adjust_topitem_for_hidden_categories(topitem);
	int force_trigger = is_xmbctrl_trigger(item->text) && !xmbctrl_triggered;
	if(force_trigger) xmbctrl_triggered = 1;
	if(force_trigger || xlog_hook(5, item))
		return AddVshItem(a0, topitem, item);

	return 0;
}

int AddVshItemPatchedGameSavedataEf(void *a0, int topitem, SceVshItem *item)
{
	topitem = adjust_topitem_for_hidden_categories(topitem);
	int force_trigger = is_xmbctrl_trigger(item->text) && !xmbctrl_triggered;
	if(force_trigger) xmbctrl_triggered = 1;
	if(force_trigger || xlog_hook(6, item))
		return AddVshItem(a0, topitem, item);

	return 0;
}

void PatchVshMain(u32 text_addr)
{
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

	xlog("PatchVshMain: text=0x%08X AddVshItem=0x%08X\n", text_addr, (u32)AddVshItem);
	xlog("  patch[0]=0x%X first4@AddVshItem=0x%08X\n", patch[0], *(u32 *)AddVshItem);
	{
		int i;
		for (i = 1; i <= 12; i++) {
			u32 addr = text_addr + patch[i];
			u32 pre = *(u32 *)addr;
			xlog("  patch[%d]=0x%X site=0x%08X pre=0x%08X (op=0x%02X)\n",
				i, patch[i], addr, pre, (pre >> 26) & 0x3F);
		}
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
	xlog("  patch[1] post=0x%08X\n", *(u32 *)(text_addr + patch[1]));

	if (topcat_count_patch) {
		MAKE_CALL(text_addr + topcat_count_patch, AdjustTopCategoryCountAndGetCount);
		_sw(0x02402021, text_addr + topcat_count_patch + 4);
		xlog("  topcat count patch=0x%X site=0x%08X post=0x%08X delay=0x%08X\n",
			topcat_count_patch, text_addr + topcat_count_patch,
			*(u32 *)(text_addr + topcat_count_patch),
			*(u32 *)(text_addr + topcat_count_patch + 4));
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
		xlog("OnModuleStart: vsh_module text=0x%08X patching s6=%d s51=%d s52=%d s53=%d s38=%d\n",
			text_addr, set[6], set[51], set[52], set[53], set[38]);
		PatchVshMain(text_addr);
	}

	return ret;
}

int module_start(SceSize args, void *argp)
{
	xlog_raw_both("xmbih: module_start entry\n");
	if (argp) {
		xlog_raw_both("xmbih: argp=");
		xlog_raw_both((const char *)argp);
		xlog_raw_both("\n");
	} else {
		xlog_raw_both("xmbih: argp=NULL\n");
	}

	xlog_raw_both("ck1: pre-devkit\n");
	devkit = sceKernelDevkitVersion();
	xlog_raw_both("ck2: post-devkit\n");
	psp_model = kuKernelGetModel();
	xlog_raw_both("ck3: post-kuKernelGetModel\n");

	/* Make ini Path */
	strcpy(ini_path, argp);
	strrchr(ini_path, '/')[1] = 0;
	strcpy(ini_path + strlen(ini_path), "xmbih.ini");
	xlog_raw_both("ck4: ini_path=");
	xlog_raw_both(ini_path);
	xlog_raw_both("\n");

	xlog("xmbih: devkit=0x%X psp_model=%d\n", devkit, psp_model);
	xlog_raw_both("ck5: post-first-xlog\n");

	xlog_raw_both("cfg: global 0\n");
	/* Global */
	set[0] = cfg(global_category, "HIDE_ALL_SETTINGS");
	set[1] = cfg(global_category, "HIDE_ALL_EXTRAS");
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
		boot_hide_for_ms = 1;
		/* Game's displayed index = 5 minus any hidden pre-Game categories,
		   so the XMB-ready signal matches regardless of what's hidden. */
		start_ms_game_index = adjust_topitem_for_hidden_categories(5);
	}

	xlog_raw_both("cfg: settings 1\n");
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

	xlog_raw_both("cfg: extras 2\n");
	/* Extras */
	set[21] = cfg(extras_category, "1SEG");
	set[22] = cfg(extras_category, "T-DMB");
	set[23] = cfg(extras_category, "JP_BOOKREADER");
	set[24] = cfg(extras_category, "DIGITAL_COMICS");
	set[26] = cfg(extras_category, "X-RADAR_PORTABLE");

	xlog_raw_both("cfg: photo 3\n");
	/* Photo */
	set[27] = cfg(photo_category, "CAMERA");
	set[28] = cfg(photo_category, "MEMORY_STICK");
	set[29] = cfg(photo_category, "SYSTEM_STORAGE");

	xlog_raw_both("cfg: music 4\n");
	/* Music */
	set[25] = cfg(music_category, "MUSIC_UNLIMITED");
	set[30] = cfg(music_category, "SENSME_CHANNELS");
	set[31] = cfg(music_category, "MEMORY_STICK");
	set[32] = cfg(music_category, "SYSTEM_STORAGE");

	xlog_raw_both("cfg: video 5\n");
	/* Video */
	set[33] = cfg(video_category, "MEMORY_STICK");
	set[34] = cfg(video_category, "SYSTEM_STORAGE");

	xlog_raw_both("cfg: game 6\n");
	/* Game */
	set[35] = cfg(game_category, "GAME_SHARING");
	set[58] = cfg(game_category, "UMD_UPDATE");
	set[36] = cfg(game_category, "SAVED_DATA_UTILITY_MS");
	set[37] = cfg(game_category, "SAVED_DATA_UTILITY_EF");
	set[38] = cfg(game_category, "RESUME_GAME");
	set[39] = cfg(game_category, "MEMORY_STICK");
	set[40] = cfg(game_category, "SYSTEM_STORAGE");

	xlog_raw_both("cfg: network 7\n");
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

	xlog_raw_both("cfg: psn 8\n");
	/* PlayStation�Network */
	set[51] = cfg(playstation_network_category, "SIGN_UP_OR_ACCOUNT_MANAGEMENT");
	set[52] = cfg(playstation_network_category, "PLAYSTATION_STORE");
	set[53] = cfg(playstation_network_category, "INFORMATION_BOARD");
	xlog_raw_both("cfg: done 9\n");

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
	top_category_count_logged = 0;
	top_category_runtime_obj = 0;

	xlog_raw_both("ck6: post-ini-parse\n");
	xlog("settings: USE_PLUGIN=%d HIDE_ALL=%d PSN=%d MOVE_ARK_EXTRAS=%d\n",
		set[55], set[54], set[6], set[57]);
	if (set[0] == 2)
		xlog("topcat: HIDE_ALL_SETTINGS=2 not supported; ignoring top-category hide\n");
	if (set[54] == 2)
		xlog("topcat: HIDE_ALL=2 blank-row mode enabled\n");
	xlog("topcat: hidden count=%d\n", top_category_hidden_count);
	xlog("MS: &set=0x%08X &set[55]=0x%08X *(&set[55])=%d\n",
		(unsigned int)&set[0], (unsigned int)&set[55], set[55]);
	if (!set[55]) {
		xlog("USE_PLUGIN=0 at module_start, not installing handler\n");
		return 0;
	}

	xlog_raw_both("ck7: pre-sctrlHENSetStartModuleHandler\n");
	previous = sctrlHENSetStartModuleHandler(OnModuleStart);
	xlog_raw_both("ck8: post-sctrlHENSetStartModuleHandler\n");

	xlog("module_start done; handler installed\n");

	return 0;
}

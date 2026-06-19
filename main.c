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
#ifndef XLOG_ENABLED
#define XLOG_ENABLED 0
#endif

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
	SceUID fd;

#if !XLOG_ENABLED
	(void)msg;
	return;
#endif

	fd = sceIoOpen("ms0:/xmbih.log", PSP_O_WRONLY | PSP_O_CREAT | PSP_O_APPEND, 0777);
	if (fd < 0)
		return;

	sceIoWrite(fd, msg, strlen(msg));
	sceIoClose(fd);
}

static void xlog_append_char(char *buf, int *pos, int max, char ch)
{
	if (*pos < max - 1)
		buf[(*pos)++] = ch;
}

static void xlog_append_string(char *buf, int *pos, int max, const char *s)
{
	if (!s)
		s = "(null)";

	while (*s)
		xlog_append_char(buf, pos, max, *s++);
}

static void xlog_append_uint(char *buf, int *pos, int max, unsigned int value,
	unsigned int base, int width, int zero_pad, int uppercase)
{
	char tmp[16];
	int len = 0;
	const char *digits = uppercase ? "0123456789ABCDEF" : "0123456789abcdef";

	if (base < 2 || base > 16)
		return;

	do {
		tmp[len++] = digits[value % base];
		value /= base;
	} while (value && len < (int)sizeof(tmp));

	while (len < width && len < (int)sizeof(tmp))
		tmp[len++] = zero_pad ? '0' : ' ';

	while (len > 0)
		xlog_append_char(buf, pos, max, tmp[--len]);
}

static void xlog_append_int(char *buf, int *pos, int max, int value,
	int width, int zero_pad)
{
	unsigned int magnitude;

	if (value < 0) {
		xlog_append_char(buf, pos, max, '-');
		magnitude = (unsigned int)(-value);
		if (width > 0)
			width--;
	} else {
		magnitude = (unsigned int)value;
	}

	xlog_append_uint(buf, pos, max, magnitude, 10, width, zero_pad, 0);
}

static void xlog(const char *fmt, ...)
{
	char buf[256];
	int pos = 0;
	va_list ap;

	va_start(ap, fmt);
	while (*fmt) {
		if (*fmt != '%') {
			xlog_append_char(buf, &pos, sizeof(buf), *fmt++);
			continue;
		}

		fmt++;
		if (*fmt == '%') {
			xlog_append_char(buf, &pos, sizeof(buf), *fmt++);
			continue;
		}

		{
			int zero_pad = 0;
			int width = 0;

			if (*fmt == '0') {
				zero_pad = 1;
				fmt++;
			}
			while (*fmt >= '0' && *fmt <= '9') {
				width = (width * 10) + (*fmt - '0');
				fmt++;
			}

			switch (*fmt) {
				case 'd':
					xlog_append_int(buf, &pos, sizeof(buf), va_arg(ap, int),
						width, zero_pad);
					break;
				case 'u':
					xlog_append_uint(buf, &pos, sizeof(buf), va_arg(ap, unsigned int),
						10, width, zero_pad, 0);
					break;
				case 'x':
					xlog_append_uint(buf, &pos, sizeof(buf), va_arg(ap, unsigned int),
						16, width, zero_pad, 0);
					break;
				case 'X':
					xlog_append_uint(buf, &pos, sizeof(buf), va_arg(ap, unsigned int),
						16, width, zero_pad, 1);
					break;
				case 's':
					xlog_append_string(buf, &pos, sizeof(buf), va_arg(ap, const char *));
					break;
				case 'c':
					xlog_append_char(buf, &pos, sizeof(buf), (char)va_arg(ap, int));
					break;
				default:
					xlog_append_char(buf, &pos, sizeof(buf), '%');
					xlog_append_char(buf, &pos, sizeof(buf), *fmt);
					break;
			}
			if (*fmt)
				fmt++;
		}
	}
	va_end(ap);

	buf[pos] = 0;
	xlog_raw_both(buf);
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
static int (*ResolveIconKeyHelper)(const char *icon_key, int *out0, int *out1,
	int *out2);
static int (*IconGetTex)(void *buf, void *atlas, void *entry);
static int (*EnsureIconEntryFunc)(void *ctx, void *entry);
static int (*FinalizeIconEntryFunc)(void *ctx, void *entry, int topitem, int slot);
static int hide_top_category(int index);
static volatile u32 vsh_text_addr = 0;
static volatile u32 vsh_seg1_addr = 0;
static volatile u32 vsh_seg1_size = 0;
static volatile u32 icon_layout_atlas = 0;
static volatile int icon_layout_main_mod = -1;
static volatile int icon_layout_shadow_mod = -1;
static volatile int icon_layout_glow_mod = -1;

#if XLOG_ENABLED
static volatile int network_track_probe_count;
static volatile int network_resolve_probe_active;
static const char *network_resolve_probe_key;
static volatile int network_dispatch_probe_count;
static volatile int network_dispatch_state_probe_count;
static volatile int network_visible_prime_probe_count;
static volatile u32 icon_layout_prev2_off;
static volatile u32 icon_layout_prev1_off;
#endif

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
static volatile void *boot_umd_a0 = 0;
static volatile int boot_umd_topitem = 0;
static volatile int boot_umd_defer_active = 0;
static volatile int boot_umd_defer_captured = 0;
static volatile int boot_umd_defer_thread_started = 0;
static int top_category_hidden_count;
static int top_category_count_logged;
static u32 top_category_runtime_obj;
static int top_category_runtime_return_override_logged;
static SceUID power_callback_uid = -1;
static int power_callback_slot = -1;
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

static int path_is_dir(const char *path);
static int current_umd_is_video(void);
static int preferred_umd_location(const char *text);

static void xlog_scene_state(const char *tag)
{
	u32 obj = 0;
	u32 arr330 = 0;

#if !XLOG_ENABLED
	(void)tag;
	return;
#endif

	if (scene_ctx)
		obj = *(u32 *)(scene_ctx + 0xA6C);

	if (obj)
		arr330 = *(u32 *)(obj + 0x330);

	xlog("scene[%s]: ctx=0x%08X obj=0x%08X hidden=%d boot_hide=%d start_ms=%d game_idx=%d captured=%d/%d/%d ark=%d\n",
		tag, (u32)scene_ctx, obj, top_category_hidden_count, boot_hide_for_ms,
		start_at_ms_flag, start_ms_game_index, gamedl_captured,
		savedata_captured, umd_captured, captured_ark_count);

	if (!obj)
		return;

	xlog("scene[%s]: 330=0x%08X 334=%d 338=%d 33C=%d 340=%d 344=%d 348=%d 34C=%d 350=%d 354=%d 358=%d 35C=%d\n",
		tag, arr330, *(int *)(obj + 0x334), *(int *)(obj + 0x338),
		*(int *)(obj + 0x33C), *(int *)(obj + 0x340), *(int *)(obj + 0x344),
		*(int *)(obj + 0x348), *(int *)(obj + 0x34C), *(int *)(obj + 0x350),
		*(int *)(obj + 0x354), *(int *)(obj + 0x358), *(int *)(obj + 0x35C));

	if (arr330) {
		xlog("scene[%s]: arr330[%08X,%08X,%08X,%08X,%08X,%08X,%08X,%08X]\n",
			tag, *(u32 *)(arr330 + 0x00), *(u32 *)(arr330 + 0x04),
			*(u32 *)(arr330 + 0x08), *(u32 *)(arr330 + 0x0C),
			*(u32 *)(arr330 + 0x10), *(u32 *)(arr330 + 0x14),
			*(u32 *)(arr330 + 0x18), *(u32 *)(arr330 + 0x1C));
	}
}

static int media_duplicate_risk_active(void)
{
	if (!scene_ctx)
		return 0;

	return hide_top_category(1) ||
	       hide_top_category(2) ||
	       hide_top_category(3) ||
	       hide_top_category(4);
}

static void arm_media_readd_suppression(volatile u32 *until, u32 duration_us, const char *tag)
{
	if (!media_duplicate_risk_active())
		return;

	*until = sceKernelGetSystemTimeLow() + duration_us;
	xlog("%s: suppress window %u us\n", tag, duration_us);
}

static void arm_game_ms_readd_suppression(const char *tag)
{
	if (!media_duplicate_risk_active() || !scene_ctx)
		return;

	suppress_next_game_ms_add = 1;
	xlog("%s: suppress next game ms add\n", tag);
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

static void arm_leading_ms_readd_suppression(const char *tag)
{
	int location;

	if (!leading_ms_duplicate_risk_active())
		return;

	location = first_visible_media_location_after_settings();
	suppress_next_leading_ms_add = location;
	xlog("%s: suppress next leading ms add loc=%d\n", tag, location);
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

static void reset_leading_umd_seen(const char *tag)
{
	if (!seen_leading_video_umd)
		return;

	xlog("%s: clear seen leading video umd=%d\n", tag, seen_leading_video_umd);
	seen_leading_video_umd = 0;
}

static int is_game_umd_duplicate_item(const char *text)
{
	return !strcmp(text, "msgshare_umd") ||
	       !strcmp(text, "msg_system_update") ||
	       !strcmp(text, "msgtop_sysconf_update");
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

static void reset_game_umd_seen(const char *tag)
{
	if (!seen_game_umd_items)
		return;

	xlog("%s: clear seen game umd mask=0x%X\n", tag, seen_game_umd_items);
	seen_game_umd_items = 0;
}

static void arm_game_umd_readd_suppression(const char *tag, int count)
{
	if (!media_duplicate_risk_active() || !scene_ctx || count <= 0)
		return;

	if (suppress_next_game_umd_adds < count)
		suppress_next_game_umd_adds = count;
	xlog("%s: suppress next game umd adds=%d\n", tag,
		suppress_next_game_umd_adds);
}

static int maybe_suppress_media_readd(SceVshItem *item, int location, int topitem)
{
	volatile u32 *until = NULL;
	u32 now;
	int should_suppress = 0;

	if (!strcmp(item->text, "msgshare_ms")) {
		if (location == suppress_next_leading_ms_add &&
		    location >= 1 && location <= 3 &&
		    topitem == adjust_topitem_for_hidden_categories(location + 1)) {
			suppress_next_leading_ms_add = 0;
			xlog("media suppress: text='%s' loc=%d top=%d leading=1\n",
				item->text, location, topitem);
			return 1;
		}
		if (location != 4 ||
		    topitem != adjust_topitem_for_hidden_categories(5) ||
		    !suppress_next_game_ms_add)
			return 0;

		suppress_next_game_ms_add = 0;
		xlog("media suppress: text='%s' loc=%d top=%d\n",
			item->text, location, topitem);
		return 1;
	}
	if (is_game_umd_duplicate_item(item->text)) {
		if (!strcmp(item->text, "msgshare_umd") &&
		    location == 3 &&
		    topitem == adjust_topitem_for_hidden_categories(4) &&
		    leading_umd_duplicate_risk_active() &&
		    preferred_umd_location(item->text) == 3) {
			if (seen_leading_video_umd) {
				xlog("media suppress: text='%s' loc=%d top=%d leading_umd=1\n",
					item->text, location, topitem);
				return 1;
			}
			seen_leading_video_umd = 1;
			xlog("media allow: text='%s' loc=%d top=%d leading_umd=1\n",
				item->text, location, topitem);
		}
		if ((location == 3 || location == 4) &&
		    (topitem == first_visible_media_topitem() ||
		     (hide_top_category(4) &&
		      topitem == adjust_topitem_for_hidden_categories(5)))) {
			int preferred_location = preferred_umd_location(item->text);
			int mask = game_umd_item_mask(item->text, location);

			if (preferred_location && location != preferred_location) {
				xlog("media suppress: text='%s' loc=%d top=%d preferred=%d seen_umd=0x%X\n",
					item->text, location, topitem, preferred_location,
					seen_game_umd_items);
				return 1;
			}

			if (mask && (seen_game_umd_items & mask)) {
				xlog("media suppress: text='%s' loc=%d top=%d seen_umd=0x%X\n",
					item->text, location, topitem, seen_game_umd_items);
				return 1;
			}
			if (!mask && suppress_next_game_umd_adds > 0) {
				suppress_next_game_umd_adds--;
				xlog("media suppress: text='%s' loc=%d top=%d remaining_umd=%d\n",
					item->text, location, topitem, suppress_next_game_umd_adds);
				return 1;
			}
			if (mask) {
				seen_game_umd_items |= mask;
				xlog("media allow: text='%s' loc=%d top=%d seen_umd=0x%X\n",
					item->text, location, topitem, seen_game_umd_items);
			}
		}
	}
	if (!strcmp(item->text, "msgshare_umd")) {
		until = &suppress_umd_adds_until;
		should_suppress = topitem == 1;
	}
	else {
		return 0;
	}

	now = sceKernelGetSystemTimeLow();
	if (!*until || (int)(*until - now) <= 0 || !should_suppress)
		return 0;

	*until = 0;
	xlog("media suppress: text='%s' loc=%d top=%d\n",
		item->text, location, topitem);
	return 1;
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
	xlog("boot_umd: mark seen umd top=%d mask=0x%X\n",
		topitem, seen_game_umd_items);
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

static void log_msgshare_ms_add(const char *source, int location, int topitem)
{
	xlog("%s msgshare_ms: loc=%d top=%d game_top=%d video_hidden=%d ms_inserted=%d usb=0x%X suppress_next=%d now=%u\n",
		source,
		location,
		topitem,
		adjust_topitem_for_hidden_categories(5),
		hide_top_category(4),
		MScmIsMediumInserted(),
		sceUsbGetState(),
		suppress_next_game_ms_add,
		sceKernelGetSystemTimeLow());
}

static int is_network_item_text(const char *text)
{
	return !strcmp(text, "msg_onlinemanual") ||
	       !strcmp(text, "msgtop_network_lftv") ||
	       !strcmp(text, "msg_skype") ||
	       !strcmp(text, "msg_ps3_connection") ||
	       !strcmp(text, "msg_internetradio") ||
	       !strcmp(text, "msgtop_network_rss") ||
	       !strcmp(text, "msgtop_network_browser") ||
	       !strcmp(text, "msg_internet_search") ||
	       !strcmp(text, "msg_psspot") ||
	       !strcmp(text, "msg_gomessenger");
}

/* Items from columns that shift left under compaction, used to compare a
   working shifted column (Game/PSN) against the blank Network one. */
static int is_icon_debug_item(const char *text)
{
	return is_network_item_text(text) ||
	       !strcmp(text, "msgtop_game_gamedl") ||
	       !strcmp(text, "msgtop_game_savedata") ||
	       !strcmp(text, "msg_game_hibernation") ||
	       !strcmp(text, "msg_signup") ||
	       !strcmp(text, "msg_ps_store") ||
	       !strcmp(text, "msg_information_board");
}

static void log_network_item_add(const char *source, int location, int incoming_topitem,
	int adjusted_topitem, SceVshItem *item)
{
	u32 obj = 0;
	int current_top = -1;

	if (!is_icon_debug_item(item->text))
		return;

	if (scene_ctx)
		obj = *(u32 *)(scene_ctx + 0xA6C);
	if (obj)
		current_top = *(int *)(obj + 0x338);

	/* Dump every field that could drive the per-item icon/plane lookup so a
	   blank Network item can be compared to a rendering Game/PSN item. */
	xlog("icon dump %s: text='%s' loc=%d top=%d adj=%d cur=%d id=%d reloc=%d act=%d arg=%d ctx=0x%08X sub=0x%08X img='%s' sh='%s' glow='%s'\n",
		source,
		item->text,
		location,
		incoming_topitem,
		adjusted_topitem,
		current_top,
		item->id,
		item->relocate,
		item->action,
		item->action_arg,
		(u32)item->context,
		(u32)item->subtitle,
		item->image,
		item->image_shadow,
		item->image_glow);
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
			xlog("network icon track remap: key='%s' top=%d track=%d\n",
				icon_key, topitem, tracked_topitem);
		}
	}

#if XLOG_ENABLED
	if (tracked_topitem == 6 &&
	    count_hidden_top_categories_before(6) > 0) {
		network_resolve_probe_active = 1;
		network_resolve_probe_key = icon_key;
	}
#endif

	ret = TrackColumnIconKey(ctx, tracked_topitem, icon_key);

#if XLOG_ENABLED
	if (tracked_topitem == 6 &&
	    count_hidden_top_categories_before(6) > 0 &&
	    vsh_text_addr &&
	    network_track_probe_count < 16) {
		int count_a = *(int *)(vsh_text_addr + 0x7330);
		int *queue_a = (int *)(vsh_text_addr + 0x72D8);
		int count_b = *(int *)(vsh_text_addr + 0x7334);
		int *queue_b = (int *)(vsh_text_addr + 0x7310);

		xlog("network track state: key='%s' ret=%d A(count=%d q=%d,%d,%d,%d,%d,%d,%d,%d) B(count=%d q=%d,%d,%d,%d,%d,%d,%d,%d)\n",
			icon_key, ret,
			count_a,
			queue_a[0], queue_a[1], queue_a[2], queue_a[3],
			queue_a[4], queue_a[5], queue_a[6], queue_a[7],
			count_b,
			queue_b[0], queue_b[1], queue_b[2], queue_b[3],
			queue_b[4], queue_b[5], queue_b[6], queue_b[7]);
		network_track_probe_count++;
	}
	network_resolve_probe_active = 0;
	network_resolve_probe_key = 0;
#endif
	return ret;
}

static int power_callback(int unknown, int powerInfo, void *arg)
{
	(void)arg;

	xlog("power: cb unk=%d info=0x%08X\n", unknown, powerInfo);
	if (powerInfo & PSP_POWER_CB_SUSPENDING)
		xlog_scene_state("suspending");
	if (powerInfo & PSP_POWER_CB_RESUMING)
		xlog_scene_state("resuming");
	if (powerInfo & PSP_POWER_CB_RESUME_COMPLETE)
		xlog_scene_state("resume_complete");
	if (powerInfo & PSP_POWER_CB_STANDBY)
		xlog("power: standby flag set\n");
	if (powerInfo & PSP_POWER_CB_POWER_SWITCH)
		xlog("power: power-switch flag set\n");
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
		arm_leading_ms_readd_suppression("power lead ms");
		arm_game_ms_readd_suppression("power ms");
		arm_media_readd_suppression(&suppress_umd_adds_until, 3000000, "power umd");
		if (sceUmdCheckMedium() > 0)
			arm_game_umd_readd_suppression("power game umd", 2);
	}

	return 0;
}

static int umd_callback(int unknown, int event, void *arg)
{
	(void)unknown;
	(void)arg;

	xlog("umd cb: event=0x%08X\n", event);
	if (event & (PSP_UMD_NOT_PRESENT | PSP_UMD_CHANGED)) {
		reset_leading_umd_seen("umd lead");
		reset_game_umd_seen("umd game");
		arm_media_readd_suppression(&suppress_umd_adds_until, 3000000, "umd");
		arm_game_umd_readd_suppression("umd game", 2);
	}

	return 0;
}

static int ms_callback(int unknown, int event, void *arg)
{
	(void)unknown;
	(void)arg;

	xlog("ms cb: event=%d inserted=%d usb=0x%X suppress_next=%d now=%u\n",
		event,
		MScmIsMediumInserted(),
		sceUsbGetState(),
		suppress_next_game_ms_add,
		sceKernelGetSystemTimeLow());
	if (event == MS_CB_EVENT_EJECTED) {
		arm_leading_ms_readd_suppression("ms eject lead");
		arm_game_ms_readd_suppression("ms eject");
	}
	else if (event == MS_CB_EVENT_INSERTED) {
		arm_leading_ms_readd_suppression("ms insert lead");
		arm_game_ms_readd_suppression("ms insert");
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
			xlog("ms poll: inserted=%d usb=0x%X prev_inserted=%d prev_usb=0x%X\n",
				inserted, usb_state, last_ms_inserted_state, last_usb_state);
		}

		if (last_ms_inserted_state >= -1 && inserted != last_ms_inserted_state) {
			if (inserted <= 0) {
				arm_leading_ms_readd_suppression("ms poll eject lead");
				arm_game_ms_readd_suppression("ms poll eject");
			}
			else {
				arm_leading_ms_readd_suppression("ms poll insert lead");
				arm_game_ms_readd_suppression("ms poll insert");
			}
		}
		else if (scene_ctx && inserted > 0 && usb_drop) {
			arm_leading_ms_readd_suppression("ms usb drop lead");
			arm_game_ms_readd_suppression("ms usb drop");
		}
		else if (scene_ctx && inserted > 0 && usb_rise) {
			arm_leading_ms_readd_suppression("ms usb rise lead");
			arm_game_ms_readd_suppression("ms usb rise");
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
		xlog("power: sceKernelCreateCallback failed 0x%08X\n",
			(unsigned int)power_callback_uid);
		return 0;
	}

	slot = scePowerRegisterCallback(-1, power_callback_uid);
	power_callback_slot = slot;
	xlog("power: register callback uid=0x%08X slot=%d\n",
		(unsigned int)power_callback_uid, slot);
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

static void start_power_debugging(void)
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
		xlog("power: sceKernelCreateThread failed 0x%08X\n",
			(unsigned int)power_callback_thread_uid);
		return;
	}

	sceKernelStartThread(power_callback_thread_uid, 0, NULL);
	xlog("power: callback thread uid=0x%08X started\n",
		(unsigned int)power_callback_thread_uid);

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
		xlog("media: sceKernelCreateThread failed 0x%08X\n",
			(unsigned int)media_watch_thread_uid);
		return;
	}

	sceKernelStartThread(media_watch_thread_uid, 0, NULL);
	xlog("media: watcher thread uid=0x%08X started\n",
		(unsigned int)media_watch_thread_uid);
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
			xlog("boot_umd: replay skipped, already present top=%d\n",
				boot_umd_topitem);
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
	xlog("boot_umd: deferred initial add top=%d\n", topitem);

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

static int detect_boot_umd_video(void)
{
	pspUmdInfo info;
	int ret;

	if (sceUmdCheckMedium() <= 0)
		return 0;

	memset(&info, 0, sizeof(info));
	info.size = sizeof(info);

	ret = sceUmdGetDiscInfo(&info);
	if (ret < 0) {
		/* During VSH startup the drive can still be initializing; give it a short
		   chance to settle so START_AT_MEMORY_STICK can be disabled before the
		   XMB item walk begins on movie boots. */
		sceUmdWaitDriveStatWithTimer(PSP_UMD_READY, 250000);
		ret = sceUmdGetDiscInfo(&info);
	}

	if (ret < 0) {
		xlog("start_ms: umd info unavailable ret=0x%08X\n", ret);
		return 0;
	}

	xlog("start_ms: umd type=0x%X\n", info.type);
	return (info.type & PSP_UMD_TYPE_VIDEO) != 0;
}

static int detect_mounted_umd_video(void)
{
	int has_video;
	int has_game;

	has_video = path_is_dir("disc0:/UMD_VIDEO");
	has_game = path_is_dir("disc0:/PSP_GAME");

	if (has_video || has_game)
		xlog("start_ms: disc0 probe video=%d game=%d\n", has_video, has_game);

	return has_video;
}

static int maybe_disable_start_at_ms_from_disc_layout(SceVshItem *item, int location)
{
	if (!start_at_ms_flag || !boot_hide_for_ms)
		return 0;

	if (!detect_mounted_umd_video())
		return 0;

	/* vshmain has mounted a movie-disc layout on disc0:, so the START_AT
	   Game/Memory Stick override is wrong for this boot. Stop hiding now.
	   If the worker thread already started, clearing boot_hide_for_ms lets it
	   exit its wait loop on the next poll and restore any Game items we had
	   already captured before the movie probe became available. */
	boot_hide_for_ms = 0;
	start_at_ms_flag = 0;
	umd_captured = 0;
	xlog("start_ms: disabled via disc0 probe text='%s' loc=%d\n",
		item->text, location);
	return 1;
}

static void maybe_disable_start_at_ms_for_movie_boot(SceVshItem *item, int location,
	int topitem)
{
	int video_topitem;

	if (!start_at_ms_flag || !boot_hide_for_ms)
		return;

	if (strcmp(item->text, "msgshare_umd"))
		return;

	video_topitem = adjust_topitem_for_hidden_categories(4);
	xlog("start_ms: msgshare_umd loc=%d topitem=%d video_topitem=%d\n",
		location, topitem, video_topitem);
	if (location != 3 && topitem != video_topitem)
		return;

	/* A UMD movie is being routed into the Video/Movie column, so the XMB's
	   natural boot target is Movie rather than Game. Disable the boot-hide
	   override before this item reaches skip(), otherwise it'll get captured
	   and later re-added into Game. */
	boot_hide_for_ms = 0;
	start_at_ms_flag = 0;
	xlog("start_ms: disabled for movie boot loc=%d topitem=%d\n",
		location, topitem);
}

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
		memcpy(&filtered[out], &table[out], sizeof(filtered[out]));
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
		if (maybe_disable_start_at_ms_from_disc_layout(item, location))
			return 1;
		xlog("start_ms: boot-hide capture text='%s' loc=%d\n",
			item->text, location);
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
	int incoming_topitem = topitem;
	int add_topitem;

	{
		int adjusted_topitem;

		if (!prepare_topitem_for_item(item, topitem, &adjusted_topitem, "wrapper"))
			return 0;

		topitem = adjusted_topitem;
	}

	maybe_disable_start_at_ms_for_movie_boot(item, 0, topitem);
	log_network_item_add("wrapper", 0, incoming_topitem, topitem, item);
	add_topitem = should_preserve_network_topitem(incoming_topitem, topitem, item) ?
		incoming_topitem : topitem;
	if (add_topitem != topitem) {
		xlog("network preserve topitem: text='%s' orig=%d adj=%d add=%d\n",
			item->text, incoming_topitem, topitem, add_topitem);
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
		return AddVshItem(a0, add_topitem, item);

	return 0;
}

int AddVshItemPatchedPhoto(void *a0, int topitem, SceVshItem *item)
{
	int incoming_topitem = topitem;
	topitem = adjust_topitem_for_hidden_categories(topitem);
	maybe_disable_start_at_ms_for_movie_boot(item, 1, topitem);
	log_network_item_add("photo", 1, incoming_topitem, topitem, item);
	if (!strcmp(item->text, "msgshare_ms"))
		log_msgshare_ms_add("photo", 1, topitem);
	if (maybe_suppress_media_readd(item, 1, topitem))
		return 0;
	int force_trigger = is_xmbctrl_trigger(item->text) && !xmbctrl_triggered;
	if(force_trigger) xmbctrl_triggered = 1;
	if(force_trigger || xlog_hook(1, item))
		return AddVshItem(a0, topitem, item);

	return 0;
}

int AddVshItemPatchedMusic(void *a0, int topitem, SceVshItem *item)
{
	int incoming_topitem = topitem;
	topitem = adjust_topitem_for_hidden_categories(topitem);
	maybe_disable_start_at_ms_for_movie_boot(item, 2, topitem);
	log_network_item_add("music", 2, incoming_topitem, topitem, item);
	if (!strcmp(item->text, "msgshare_ms"))
		log_msgshare_ms_add("music", 2, topitem);
	if (maybe_suppress_media_readd(item, 2, topitem))
		return 0;
	int force_trigger = is_xmbctrl_trigger(item->text) && !xmbctrl_triggered;
	if(force_trigger) xmbctrl_triggered = 1;
	if(force_trigger || xlog_hook(2, item))
		return AddVshItem(a0, topitem, item);

	return 0;
}

int AddVshItemPatchedVideo(void *a0, int topitem, SceVshItem *item)
{
	int incoming_topitem = topitem;
	topitem = adjust_topitem_for_hidden_categories(topitem);
	maybe_disable_start_at_ms_for_movie_boot(item, 3, topitem);
	log_network_item_add("video", 3, incoming_topitem, topitem, item);
	if (!strcmp(item->text, "msgshare_ms"))
		log_msgshare_ms_add("video", 3, topitem);
	if (maybe_suppress_media_readd(item, 3, topitem))
		return 0;
	int force_trigger = is_xmbctrl_trigger(item->text) && !xmbctrl_triggered;
	if(force_trigger) xmbctrl_triggered = 1;
	if(force_trigger || xlog_hook(3, item))
		return AddVshItem(a0, topitem, item);

	return 0;
}

int AddVshItemPatchedGame(void *a0, int topitem, SceVshItem *item)
{
	int incoming_topitem = topitem;
	topitem = adjust_topitem_for_hidden_categories(topitem);
	maybe_disable_start_at_ms_for_movie_boot(item, 4, topitem);
	log_network_item_add("game", 4, incoming_topitem, topitem, item);
	if (!strcmp(item->text, "msgshare_ms")) {
		log_msgshare_ms_add("game", 4, topitem);
	}
	if (maybe_suppress_media_readd(item, 4, topitem))
		return 0;
	int force_trigger = is_xmbctrl_trigger(item->text) && !xmbctrl_triggered;
	if(force_trigger) xmbctrl_triggered = 1;
	if(force_trigger || xlog_hook(4, item))
		return AddVshItem(a0, topitem, item);

	return 0;
}

int AddVshItemPatchedGameSavedataMs(void *a0, int topitem, SceVshItem *item)
{
	int incoming_topitem = topitem;
	topitem = adjust_topitem_for_hidden_categories(topitem);
	maybe_disable_start_at_ms_for_movie_boot(item, 5, topitem);
	log_network_item_add("gms", 5, incoming_topitem, topitem, item);
	if (maybe_suppress_media_readd(item, 5, topitem))
		return 0;
	int force_trigger = is_xmbctrl_trigger(item->text) && !xmbctrl_triggered;
	if(force_trigger) xmbctrl_triggered = 1;
	if(force_trigger || xlog_hook(5, item))
		return AddVshItem(a0, topitem, item);

	return 0;
}

int AddVshItemPatchedGameSavedataEf(void *a0, int topitem, SceVshItem *item)
{
	int incoming_topitem = topitem;
	topitem = adjust_topitem_for_hidden_categories(topitem);
	maybe_disable_start_at_ms_for_movie_boot(item, 6, topitem);
	log_network_item_add("gef", 6, incoming_topitem, topitem, item);
	if (maybe_suppress_media_readd(item, 6, topitem))
		return 0;
	int force_trigger = is_xmbctrl_trigger(item->text) && !xmbctrl_triggered;
	if(force_trigger) xmbctrl_triggered = 1;
	if(force_trigger || xlog_hook(6, item))
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

	xlog("umd video add text='%s' top=%d adj=%d\n",
		item->text, topitem, adjusted_topitem);
	return AddVshItem(a0, adjusted_topitem, item);
}

static int UmdGameAddPatchedRet(void *a0, int topitem, SceVshItem *item)
{
	int adjusted_topitem = adjust_topitem_for_hidden_categories(topitem);

	if (maybe_defer_boot_umd_add(a0, adjusted_topitem, item))
		return 0;
	if (maybe_suppress_media_readd(item, 4, adjusted_topitem))
		return 0;

	xlog("umd game add text='%s' top=%d adj=%d\n",
		item->text, topitem, adjusted_topitem);
	return AddVshItem(a0, adjusted_topitem, item);
}

static int UmdVideoSelectShiftPatched(void *ctx, int topitem)
{
	xlog("umd video select top=%d adj=%d\n", topitem,
		adjust_topitem_for_hidden_categories(topitem));
	return TopcatSelectShiftedFunc(ctx,
		adjust_topitem_for_hidden_categories(topitem), topitem);
}

static int UmdGameSelectShiftPatched(void *ctx, int topitem)
{
	if (should_suppress_umd_select_route(topitem)) {
		xlog("umd game select suppressed top=%d adj=%d\n", topitem,
			adjust_topitem_for_hidden_categories(topitem));
		return 0;
	}
	xlog("umd game select top=%d adj=%d\n", topitem,
		adjust_topitem_for_hidden_categories(topitem));
	return TopcatSelectShiftedFunc(ctx,
		adjust_topitem_for_hidden_categories(topitem), topitem);
}

static int UmdTopcatPositionShiftPatched(void *obj, int topitem)
{
	int adjusted_topitem = adjust_topitem_for_hidden_categories(topitem);

	xlog("umd pos orig=%d adj=%d\n", topitem, adjusted_topitem);
	return TopcatPositionFunc(obj, adjusted_topitem);
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

#if XLOG_ENABLED
	if (network_dispatch_probe_count < 32 &&
	    (a3 == 6 || a3 == adjusted_network_topitem ||
	     a1 == 6 || a1 == adjusted_network_topitem)) {
		xlog("network dispatch: a1=%d a2=%d a3=%d t0=%d mapped=%d\n",
			a1, a2, a3, t0_arg, mapped_a3);
		network_dispatch_probe_count++;
	}
#endif

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

#if XLOG_ENABLED
	if (mapped_a3 == 6 &&
	    network_dispatch_state_probe_count < 8) {
		u32 table = *(u32 *)((char *)ctx + 0x10);
		int count = *(int *)((char *)ctx + 0x14);
		u32 arr = *(u32 *)((char *)ctx + 0x18);
		int v0 = -1, v1 = -1, v2 = -1, v3 = -1, v4 = -1;
		int v5 = -1, v6 = -1, v7 = -1, v8 = -1, v9 = -1;

		if (arr && count > 0) v0 = *(int *)(arr + (0 * 4));
		if (arr && count > 1) v1 = *(int *)(arr + (1 * 4));
		if (arr && count > 2) v2 = *(int *)(arr + (2 * 4));
		if (arr && count > 3) v3 = *(int *)(arr + (3 * 4));
		if (arr && count > 4) v4 = *(int *)(arr + (4 * 4));
		if (arr && count > 5) v5 = *(int *)(arr + (5 * 4));
		if (arr && count > 6) v6 = *(int *)(arr + (6 * 4));
		if (arr && count > 7) v7 = *(int *)(arr + (7 * 4));
		if (arr && count > 8) v8 = *(int *)(arr + (8 * 4));
		if (arr && count > 9) v9 = *(int *)(arr + (9 * 4));

		xlog("network dispatch state: ctx=0x%08X table=0x%08X count=%d arr=0x%08X vals=%d,%d,%d,%d,%d,%d,%d,%d,%d,%d\n",
			(u32)ctx, table, count, arr,
			v0, v1, v2, v3, v4, v5, v6, v7, v8, v9);
		network_dispatch_state_probe_count++;
	}
#endif

	return ret;
}

#if XLOG_ENABLED
static volatile u32 icon_probe_seen[256];
static volatile int icon_probe_seen_count;
#endif

static int IconResolveProbe(void *buf, void *atlas, void *entry)
{
#if XLOG_ENABLED
	int loaded;
	u32 off = (u32)entry - (u32)atlas;
	u32 key;
	int i, dup = 0;
#endif
	u32 entry_addr = (u32)entry;
	void *record = 0;
	u32 mod;

#if XLOG_ENABLED
	loaded = *(int *)((char *)entry + 0x18);
	key = (off & 0x0FFFFFFF) | (loaded ? 0x80000000u : 0u);
	for (i = 0; i < icon_probe_seen_count; i++) {
		if (icon_probe_seen[i] == key) {
			dup = 1;
			break;
		}
	}
	if (!dup && icon_probe_seen_count < 256) {
		icon_probe_seen[icon_probe_seen_count++] = key;
		xlog("ic: atlas=0x%08X off=0x%X loaded=%d\n", (u32)atlas, off, loaded);
	}

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
			xlog("icon layout: atlas=0x%08X mods=%X/%X/%X\n",
				(u32)atlas,
				icon_layout_main_mod,
				icon_layout_shadow_mod,
				icon_layout_glow_mod);
		}
		icon_layout_prev2_off = icon_layout_prev1_off;
		icon_layout_prev1_off = off;
	}
#endif

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
#if XLOG_ENABLED
			if (network_visible_prime_probe_count < 24) {
				xlog("network visible prime: off=0x%X mod=0x%X rec=0x%08X post=%d\n",
					off, mod, (u32)record,
					*(int *)((char *)entry + 0x18));
				network_visible_prime_probe_count++;
			}
#endif
		}
	}

	return IconGetTex(buf, atlas, entry);
}

#if XLOG_ENABLED
static int ResolveIconKeyProbePatched(const char *icon_key, int *out0, int *out1,
	int *out2)
{
	int ret = ResolveIconKeyHelper(icon_key, out0, out1, out2);

	if (network_resolve_probe_active &&
	    network_resolve_probe_key &&
	    network_track_probe_count < 32) {
		xlog("network resolve key: key='%s' ret=%d out0=%d out1=%d out2=%d arg='%s'\n",
			network_resolve_probe_key,
			ret,
			out0 ? *out0 : -1,
			out1 ? *out1 : -1,
			out2 ? *out2 : -1,
			icon_key ? icon_key : "(null)");
	}

	return ret;
}
#endif

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

	if (devkit == FW(0x660)) {
		TopcatSelectShiftedFunc = (int (*)(void *, int, int))(text_addr + 0x22998);
		TopcatPositionFunc = (int (*)(void *, int))(text_addr + 0x3F4E0);
		TrackColumnIconKey = (int (*)(void *, int, const char *))(text_addr + 0x2EDBC);
		NetworkDispatchFunc = (int (*)(void *, int, int, int))(text_addr + 0x2DF34);
		ResolveIconKeyHelper = (int (*)(const char *, int *, int *, int *))(text_addr + 0x2F0F8);
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
		_sw(0x00608821, text_addr + 0x2E854);  /* move   s1, v1 */
		_sw(0x24100000, text_addr + 0x2E858);  /* addiu  s0, zr, 0 */
		_sw(0x00A03821, text_addr + 0x2E89C);  /* move   a3, a1 */
		_sw(0x2A820007, text_addr + 0x2E8B4);  /* slti  v0, s4, 7 */
		MAKE_CALL(text_addr + 0x16538, NetworkDispatchPatched);
		IconGetTex = (int (*)(void *, void *, void *))(text_addr + 0x2D7D4);
		MAKE_CALL(text_addr + 0x2D8A0, IconResolveProbe);

#if XLOG_ENABLED
		/* DIAGNOSTIC: hook the icon resolver's load-check (0x2d7d4, called from
		   0x2d820 at 0x2d8a0). It receives (buf, atlas, entry); *(entry+0x18)==0
		   means the texture isn't loaded -> the icon renders blank. Log distinct
		   (atlas, entry-offset, loaded) tuples so we can see, for the render's
		   item-icon resolves, which atlas Network uses and whether its entries
		   are loaded -- shifted vs at native index 6. */
		MAKE_CALL(text_addr + 0x2EDF4, ResolveIconKeyProbePatched);
#endif
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
		xlog("OnModuleStart: vsh_module text=0x%08X patching s6=%d s51=%d s52=%d s53=%d s38=%d\n",
			text_addr, set[6], set[51], set[52], set[53], set[38]);
		xlog("OnModuleStart: vsh seg0=0x%08X size0=0x%08X seg1=0x%08X size1=0x%08X\n",
			mod->segmentaddr[0], mod->segmentsize[0],
			mod->segmentaddr[1], mod->segmentsize[1]);
		PatchVshMain(text_addr);
	}

	return ret;
}

int module_start(SceSize args, void *argp)
{
	SceUID logfd;

#if XLOG_ENABLED
	logfd = sceIoOpen("ms0:/xmbih.log", PSP_O_WRONLY | PSP_O_CREAT | PSP_O_TRUNC, 0777);
	if (logfd >= 0)
		sceIoClose(logfd);
#else
	(void)logfd;
#endif

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
	start_power_debugging();

	xlog_raw_both("cfg: global 0\n");
	/* Global */
	set[0] = cfg(global_category, "HIDE_ALL_SETTINGS");
	set[1] = cfg(global_category, "HIDE_ALL_EXTRAS");
	if (set[1] == 2) {
		set[1] = 1;
		xlog("topcat: HIDE_ALL_EXTRAS=2 not supported; forcing content-only hide\n");
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
		if (detect_boot_umd_video()) {
			start_at_ms_flag = 0;
			xlog("start_ms: disabled at boot for physical UMD movie\n");
		} else {
			boot_hide_for_ms = 1;
			/* Game's displayed index = 5 minus any hidden pre-Game categories,
			   so the XMB-ready signal matches regardless of what's hidden. */
			start_ms_game_index = adjust_topitem_for_hidden_categories(5);
		}
	}
	boot_umd_defer_active = should_defer_boot_umd_add() && sceUmdCheckMedium() > 0;
	boot_umd_defer_captured = 0;
	boot_umd_defer_thread_started = 0;
	boot_umd_a0 = 0;
	boot_umd_topitem = 0;
	if (boot_umd_defer_active)
		xlog("boot_umd: defer active\n");

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
	xlog_scene_state("module_start_done");

	return 0;
}

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
#include <main.h>
#include <kubridge.h>
#include <string.h>
#include <stdarg.h>
#include "minGlue.h"
#include "minIni.h"

PSP_MODULE_INFO("XMBIH", 0x0007, 1, 3);

static struct {
	volatile unsigned char guard[0x200];
	volatile unsigned char flags[57];
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
extern char *game_category;     /* defined further down */
extern char *global_category;
extern int   cfg(char *category, char *fmt);
static int umdIoOpenPatched(PspIoDrvFileArg* arg, char* file, int flags, SceMode mode)
{
	return (strcmp(file, "/PSP_GAME/SYSDIR/UPDATE/PARAM.SFO") == 0 &&
	        (cfg(game_category, "UMD_UPDATE") ||
	         cfg(global_category, "HIDE_ALL") ||
	         set[4]))
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

int AddVshItemFilter(void *a0, int topitem, SceVshItem *item)
{
	/* Items reaching us here either came from xmbctrl forwarding (the
	   trigger item, or its CFW item insertions) or any other caller of
	   the real AddVshItem. Apply the user's hide rules and forward via
	   the trampoline if allowed. Location 0 is the default; the
	   trigger items and CFW item names don't depend on location, so
	   that's fine. */
	if(skip(item, 0)) {
		int (*trampoline)(void *, int, SceVshItem *) =
			(int(*)(void *, int, SceVshItem *))add_vsh_trampoline;
		return trampoline(a0, topitem, item);
	}
	return 0;
}
u32 topcat_count_patch;

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
			return set[1] == 2;
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
	XmbTopCategory *table;
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

	/* Compact the XmbTopCategory entries (icons + text) only. We previously
	   also mutated three u32[8] arrays sitting immediately before the
	   table (wad11656's "meta0/meta1/meta2"), but their purpose is
	   unknown -- and on ARK-5 the UMD-preview code path indexes into one
	   of them by the original (pre-compaction) category index, so
	   zero-padding the tail crashed the system when a UMD was inserted
	   with any category hidden. Leaving them alone fixes the crash, and
	   the visual category hide still works because the runtime count
	   patch (AdjustTopCategoryCountAndGetCount) is what actually shrinks
	   the column count vshmain renders. */

	for (i = 0; i < 8; i++) {
		int hide = hide_top_category(i);
		if (hide) {
			changed = 1;
			xlog("topcat: hiding '%s' mode=%d\n", table[i].text,
				top_category_mode(i));
			continue;
		}

		memcpy(&filtered[out], &table[i], sizeof(filtered[out]));
		out++;
	}

	if (!changed) {
		xlog("topcat: no supported category set to 2\n");
		return;
	}

	while (out < 8) {
		memset(&filtered[out], 0, sizeof(filtered[out]));
		out++;
	}

	memcpy(table, filtered, sizeof(filtered));
	xlog("topcat: table=0x%08X compacted\n", (u32)table);
}

int skip(SceVshItem *item, int location)
{
	int idnm(char *name)
	{
		return strcmp(item->text, name);
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

static int remap_ark_topitem(int incoming_topitem, int *out_topitem)
{
	/*
	 * Keep ARK's behavior constrained to its intended homes:
	 * - Extras, when ARK is injecting through the Extras path and Extras is visible
	 * - Game, when Extras is hidden and Game is visible
	 * - nothing, when neither destination is visible
	 */
	if (incoming_topitem == 1) {
		if (!hide_top_category(1)) {
			*out_topitem = adjust_topitem_for_hidden_categories(incoming_topitem);
			return 1;
		}

		if (!hide_top_category(5)) {
			*out_topitem = adjust_topitem_for_hidden_categories(5);
			return 1;
		}

		return 0;
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
	if (is_ark_custom_item(item->text)) {
		int original_topitem = topitem;
		int mapped_topitem = topitem;

		if (!remap_ark_topitem(topitem, &mapped_topitem)) {
			xlog("ark item: text='%s' topitem=%d dropped\n",
				item->text, original_topitem);
			return 0;
		}

		topitem = mapped_topitem;

		xlog("ark item: text='%s' topitem=%d adjusted=%d\n",
			item->text, original_topitem, topitem);
	}
	else if (is_game_resume_item(item->text)) {
		int original_topitem = topitem;

		topitem = adjust_topitem_for_hidden_categories(topitem);
		xlog("resume item: text='%s' topitem=%d adjusted=%d\n",
			item->text, original_topitem, topitem);
	}
	else {
		topitem = adjust_topitem_for_hidden_categories(topitem);
	}

	/* Force-forward only the FIRST xmbctrl trigger we see -- that flips
	   xmbctrl's items_added flag, so the CFW menu items get injected.
	   Subsequent triggers fall through to xlog_hook (skip()) so the user
	   can hide them. The prologue patch above, if it takes effect, also
	   filters this first trigger's forward so even it gets hidden. */
	int force_trigger = is_xmbctrl_trigger(item->text) && !xmbctrl_triggered;
	if(force_trigger) xmbctrl_triggered = 1;
	if(force_trigger || xlog_hook(0, item))
		AddVshItem(a0, topitem, item);

	return 0;
}

int AddVshItemPatchedPhoto(void *a0, int topitem, SceVshItem *item)
{
	topitem = adjust_topitem_for_hidden_categories(topitem);
	int force_trigger = is_xmbctrl_trigger(item->text) && !xmbctrl_triggered;
	if(force_trigger) xmbctrl_triggered = 1;
	if(force_trigger || xlog_hook(1, item))
		AddVshItem(a0, topitem, item);

	return 0;
}

int AddVshItemPatchedMusic(void *a0, int topitem, SceVshItem *item)
{
	topitem = adjust_topitem_for_hidden_categories(topitem);
	int force_trigger = is_xmbctrl_trigger(item->text) && !xmbctrl_triggered;
	if(force_trigger) xmbctrl_triggered = 1;
	if(force_trigger || xlog_hook(2, item))
		AddVshItem(a0, topitem, item);

	return 0;
}

int AddVshItemPatchedVideo(void *a0, int topitem, SceVshItem *item)
{
	topitem = adjust_topitem_for_hidden_categories(topitem);
	int force_trigger = is_xmbctrl_trigger(item->text) && !xmbctrl_triggered;
	if(force_trigger) xmbctrl_triggered = 1;
	if(force_trigger || xlog_hook(3, item))
		AddVshItem(a0, topitem, item);

	return 0;
}

int AddVshItemPatchedGame(void *a0, int topitem, SceVshItem *item)
{
	topitem = adjust_topitem_for_hidden_categories(topitem);
	int force_trigger = is_xmbctrl_trigger(item->text) && !xmbctrl_triggered;
	if(force_trigger) xmbctrl_triggered = 1;
	if(force_trigger || xlog_hook(4, item))
		AddVshItem(a0, topitem, item);

	return 0;
}

int AddVshItemPatchedGameSavedataMs(void *a0, int topitem, SceVshItem *item)
{
	topitem = adjust_topitem_for_hidden_categories(topitem);
	int force_trigger = is_xmbctrl_trigger(item->text) && !xmbctrl_triggered;
	if(force_trigger) xmbctrl_triggered = 1;
	if(force_trigger || xlog_hook(5, item))
		AddVshItem(a0, topitem, item);

	return 0;
}

int AddVshItemPatchedGameSavedataEf(void *a0, int topitem, SceVshItem *item)
{
	topitem = adjust_topitem_for_hidden_categories(topitem);
	int force_trigger = is_xmbctrl_trigger(item->text) && !xmbctrl_triggered;
	if(force_trigger) xmbctrl_triggered = 1;
	if(force_trigger || xlog_hook(6, item))
		AddVshItem(a0, topitem, item);

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
	xlog("settings: USE_PLUGIN=%d HIDE_ALL=%d PSN=%d\n", set[55], set[54], set[6]);
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

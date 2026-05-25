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
#include <pspmodulemgr.h>
#include <systemctrl.h>
#include <main.h>
#include <kubridge.h>
#include <string.h>
#include <stdarg.h>
#include "minGlue.h"
#include "minIni.h"
#include "include/macros.h"

PSP_MODULE_INFO("XMBIH", 0x0007, 1, 3);

static struct {
	volatile unsigned char guard[0x200];
	volatile unsigned char flags[57];
} cfg_store;

#define set cfg_store.flags

static char log_path[16];
static int log_enabled;
static int saw_umd_video_path;
static int saw_umd_game_path;
static int (*UmdMediaStateFunc)(void *ctx);
static void (*TopcatStateSetupFunc)(void *ctx);
static int (*TopcatSelectHelperFunc)(void *ctx, int topitem);
static int (*TopcatSelectShiftedFunc)(void *ctx, int adjusted_topitem, int original_topitem);
static int (*TopcatPositionFunc)(void *obj, int topitem);
static u32 umd_media_state_patch;
static u32 topcat_state_setup_patch;
static u32 topcat_select_helper_patch;
static u32 topcat_position_patch;
static volatile u32 add_vsh_filter_ra;
static u32 top_category_runtime_obj;
static int umd_state_calls;
static int topcat_setup_calls;
static int topcat_select_calls;
static int topcat_position_calls;

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

int strncmp(const char *a, const char *b, unsigned int n)
{
	while (n && *a && *a == *b) {
		a++;
		b++;
		n--;
	}
	if (!n)
		return 0;
	return (int)(unsigned char)*a - (int)(unsigned char)*b;
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

static void append_log(const char *msg)
{
	SceUID fd;
	unsigned int len;

	if (!log_enabled || !msg)
		return;

	len = strlen(msg);
	if (!len)
		return;

	fd = sceIoOpen(log_path, PSP_O_WRONLY | PSP_O_CREAT | PSP_O_APPEND, 0777);
	if (fd < 0)
		return;

	sceIoWrite(fd, msg, len);
	sceIoClose(fd);
}

static void xlog_raw_both(const char *msg)
{
	append_log(msg);
}

static void xlog(const char *fmt, ...)
{
	(void)fmt;
}

static void append_int(char *buf, int *pos, int value)
{
	char tmp[16];
	int i = 0;
	unsigned int v;

	if (value < 0) {
		buf[(*pos)++] = '-';
		v = (unsigned int)(-value);
	}
	else {
		v = (unsigned int)value;
	}

	do {
		tmp[i++] = '0' + (v % 10);
		v /= 10;
	} while (v && i < (int)sizeof(tmp));

	while (i > 0)
		buf[(*pos)++] = tmp[--i];
}

static void append_hex8(char *buf, int *pos, u32 value)
{
	static const char hexdigits[] = "0123456789ABCDEF";
	int i;

	for (i = 7; i >= 0; i--)
		buf[(*pos)++] = hexdigits[(value >> (i * 4)) & 0xF];
}

static void append_text(char *buf, int *pos, const char *text)
{
	while (*text)
		buf[(*pos)++] = *text++;
}

static void xlog_boot_state(int hidden_count)
{
	char buf[96];
	int pos = 0;

	append_text(buf, &pos, "boot: use=");
	append_int(buf, &pos, set[55]);
	append_text(buf, &pos, " hide_all=");
	append_int(buf, &pos, set[54]);
	append_text(buf, &pos, " top_hidden=");
	append_int(buf, &pos, hidden_count);
	append_text(buf, &pos, "\n");
	buf[pos] = 0;
	append_log(buf);
}

static void xlog_vsh_text(u32 text_addr)
{
	char buf[48];
	int pos = 0;

	append_text(buf, &pos, "vsh text=");
	append_hex8(buf, &pos, text_addr);
	append_text(buf, &pos, "\n");
	buf[pos] = 0;
	append_log(buf);
}

static void xlog_media_item(const char *kind, int topitem, int adjusted, const char *text)
{
	char buf[128];
	int pos = 0;

	append_text(buf, &pos, kind);
	append_text(buf, &pos, " item top=");
	append_int(buf, &pos, topitem);
	append_text(buf, &pos, " adj=");
	append_int(buf, &pos, adjusted);
	append_text(buf, &pos, " text=");
	append_text(buf, &pos, text);
	append_text(buf, &pos, "\n");
	buf[pos] = 0;
	append_log(buf);
}

static void xlog_filter_caller(u32 ra, int topitem, const char *text)
{
	char buf[128];
	int pos = 0;

	append_text(buf, &pos, "filter caller ra=");
	append_hex8(buf, &pos, ra);
	append_text(buf, &pos, " top=");
	append_int(buf, &pos, topitem);
	append_text(buf, &pos, " text=");
	append_text(buf, &pos, text);
	append_text(buf, &pos, "\n");
	buf[pos] = 0;
	append_log(buf);
}

static void xlog_code_words(const char *label, u32 addr, int words)
{
	char buf[256];
	int pos = 0;
	int i;

	append_text(buf, &pos, label);
	append_text(buf, &pos, "=");
	append_hex8(buf, &pos, addr);
	append_text(buf, &pos, " [");
	for (i = 0; i < words; i++) {
		if (i)
			append_text(buf, &pos, ",");
		append_hex8(buf, &pos, *(u32 *)(addr + i * 4));
	}
	append_text(buf, &pos, "]\n");
	buf[pos] = 0;
	append_log(buf);
}

static u32 resolve_jump_target(u32 addr, u32 insn)
{
	return ((addr + 4) & 0xF0000000) | ((insn & 0x03FFFFFF) << 2);
}

static void xlog_cstring(const char *label, const char *text)
{
	char buf[192];
	int pos = 0;
	int i = 0;

	append_text(buf, &pos, label);
	append_text(buf, &pos, "=");
	while (text && text[i] && i < 96) {
		char c = text[i++];
		if ((unsigned char)c < 0x20 || (unsigned char)c > 0x7E)
			break;
		buf[pos++] = c;
	}
	append_text(buf, &pos, "\n");
	buf[pos] = 0;
	append_log(buf);
}

static void xlog_module_for_addr(u32 addr)
{
	SceUID mods[64];
	int count = 0;
	int i;

	if (sceKernelGetModuleIdList(mods, sizeof(mods), &count) < 0)
		return;

	for (i = 0; i < count && i < (int)(sizeof(mods) / sizeof(mods[0])); i++) {
		SceKernelModuleInfo info;
		char buf[192];
		int pos = 0;

		memset(&info, 0, sizeof(info));
		info.size = sizeof(info);
		if (sceKernelQueryModuleInfo(mods[i], &info) < 0)
			continue;

		if (addr < info.text_addr || addr >= info.text_addr + info.text_size)
			continue;

		append_text(buf, &pos, "caller module=");
		append_text(buf, &pos, info.name);
		append_text(buf, &pos, " text=");
		append_hex8(buf, &pos, info.text_addr);
		append_text(buf, &pos, " size=");
		append_hex8(buf, &pos, info.text_size);
		append_text(buf, &pos, " addr=");
		append_hex8(buf, &pos, addr);
		append_text(buf, &pos, "\n");
		buf[pos] = 0;
		append_log(buf);
		return;
	}
}

static void xlog_umd_path(const char *kind, const char *path)
{
	char buf[192];
	int pos = 0;

	append_text(buf, &pos, kind);
	append_text(buf, &pos, " open=");
	append_text(buf, &pos, path);
	append_text(buf, &pos, "\n");
	buf[pos] = 0;
	append_log(buf);
}

static void xlog_resolved_helper(const char *label, u32 stub_addr)
{
	u32 target = resolve_jump_target(stub_addr, _lw(stub_addr));

	xlog_code_words(label, stub_addr, 8);
	xlog_module_for_addr(target);
	xlog_code_words("helper real", target, 16);
}

static void xlog_media_hit(const char *kind, void *a0, int topitem, int adjusted, const char *text)
{
	char buf[160];
	int pos = 0;

	append_text(buf, &pos, kind);
	append_text(buf, &pos, " hit a0=");
	append_hex8(buf, &pos, (u32)a0);
	append_text(buf, &pos, " top=");
	append_int(buf, &pos, topitem);
	append_text(buf, &pos, " adj=");
	append_int(buf, &pos, adjusted);
	append_text(buf, &pos, " text=");
	append_text(buf, &pos, text);
	append_text(buf, &pos, "\n");
	buf[pos] = 0;
	append_log(buf);
}

static void xlog_umd_state(int before, int after)
{
	char buf[96];
	int pos = 0;

	append_text(buf, &pos, "umd state cat=");
	append_int(buf, &pos, before);
	append_text(buf, &pos, " adj=");
	append_int(buf, &pos, after);
	append_text(buf, &pos, "\n");
	buf[pos] = 0;
	append_log(buf);
}

static void xlog_umd_state_call(void *ctx, int ret, int category)
{
	char buf[160];
	int pos = 0;

	append_text(buf, &pos, "umd state call=");
	append_int(buf, &pos, ++umd_state_calls);
	append_text(buf, &pos, " ctx=");
	append_hex8(buf, &pos, (u32)ctx);
	append_text(buf, &pos, " ret=");
	append_int(buf, &pos, ret);
	append_text(buf, &pos, " cat=");
	append_int(buf, &pos, category);
	append_text(buf, &pos, " video=");
	append_int(buf, &pos, saw_umd_video_path);
	append_text(buf, &pos, " game=");
	append_int(buf, &pos, saw_umd_game_path);
	append_text(buf, &pos, "\n");
	buf[pos] = 0;
	append_log(buf);
}

static void xlog_topcat_count(u32 obj, int count, int new_count, void *ctx)
{
	char buf[160];
	int pos = 0;

	append_text(buf, &pos, "topcat count obj=");
	append_int(buf, &pos, (int)obj);
	append_text(buf, &pos, " count=");
	append_int(buf, &pos, count);
	append_text(buf, &pos, " new=");
	append_int(buf, &pos, new_count);
	append_text(buf, &pos, " e70=");
	append_int(buf, &pos, *(int *)((char *)ctx + 0xE70));
	append_text(buf, &pos, " e74=");
	append_int(buf, &pos, *(int *)((char *)ctx + 0xE74));
	append_text(buf, &pos, " e78=");
	append_int(buf, &pos, *(int *)((char *)ctx + 0xE78));
	append_text(buf, &pos, " e7c=");
	append_int(buf, &pos, *(int *)((char *)ctx + 0xE7C));
	append_text(buf, &pos, "\n");
	buf[pos] = 0;
	append_log(buf);
}

static void xlog_topcat_slot_detail(int idx, u32 obj);

static void xlog_topcat_runtime_state(void)
{
	u32 obj = top_category_runtime_obj;
	u32 arr330;
	u32 ptr360;
	u32 *slot_ptrs;
	char buf[256];
	int pos = 0;

	if (!obj)
		return;

	arr330 = *(u32 *)(obj + 0x330);
	ptr360 = *(u32 *)(obj + 0x360);
	slot_ptrs = ptr360 ? (u32 *)ptr360 : NULL;

	append_text(buf, &pos, "topcat runtime obj=");
	append_hex8(buf, &pos, obj);
	append_text(buf, &pos, " 334=");
	append_hex8(buf, &pos, *(u32 *)(obj + 0x334));
	append_text(buf, &pos, " 338=");
	append_hex8(buf, &pos, *(u32 *)(obj + 0x338));
	append_text(buf, &pos, " 33C=");
	append_hex8(buf, &pos, *(u32 *)(obj + 0x33C));
	append_text(buf, &pos, " arr330=");
	append_hex8(buf, &pos, arr330);
	append_text(buf, &pos, " ptr360=");
	append_hex8(buf, &pos, ptr360);
	append_text(buf, &pos, "\n");
	buf[pos] = 0;
	append_log(buf);

	if (slot_ptrs) {
		pos = 0;
		append_text(buf, &pos, "topcat ptr360=");
		append_hex8(buf, &pos, ptr360);
		append_text(buf, &pos, " [");
		append_hex8(buf, &pos, slot_ptrs[0]);
		append_text(buf, &pos, ",");
		append_hex8(buf, &pos, slot_ptrs[1]);
		append_text(buf, &pos, ",");
		append_hex8(buf, &pos, slot_ptrs[2]);
		append_text(buf, &pos, ",");
		append_hex8(buf, &pos, slot_ptrs[3]);
		append_text(buf, &pos, ",");
		append_hex8(buf, &pos, slot_ptrs[4]);
		append_text(buf, &pos, ",");
		append_hex8(buf, &pos, slot_ptrs[5]);
		append_text(buf, &pos, ",");
		append_hex8(buf, &pos, slot_ptrs[6]);
		append_text(buf, &pos, ",");
		append_hex8(buf, &pos, slot_ptrs[7]);
		append_text(buf, &pos, "]\n");
		buf[pos] = 0;
		append_log(buf);

		xlog_topcat_slot_detail(3, slot_ptrs[3]);
		xlog_topcat_slot_detail(4, slot_ptrs[4]);
		xlog_topcat_slot_detail(5, slot_ptrs[5]);
	}
}

static void xlog_topcat_slot_detail(int idx, u32 obj)
{
	char buf[160];
	int pos = 0;

	append_text(buf, &pos, "topcat slot");
	append_int(buf, &pos, idx);
	append_text(buf, &pos, " obj=");
	append_hex8(buf, &pos, obj);
	if (obj) {
		append_text(buf, &pos, " 330=");
		append_hex8(buf, &pos, *(u32 *)(obj + 0x330));
		append_text(buf, &pos, " 334=");
		append_hex8(buf, &pos, *(u32 *)(obj + 0x334));
		append_text(buf, &pos, " 33C=");
		append_hex8(buf, &pos, *(u32 *)(obj + 0x33C));
	}
	append_text(buf, &pos, "\n");
	buf[pos] = 0;
	append_log(buf);
}

static void xlog_topcat_slot_words(int idx, u32 obj)
{
	char buf[256];
	int pos = 0;
	u32 *p = (u32 *)obj;

	append_text(buf, &pos, "topcat slot");
	append_int(buf, &pos, idx);
	append_text(buf, &pos, " words=");
	append_hex8(buf, &pos, obj);
	append_text(buf, &pos, " [");
	if (obj) {
		append_hex8(buf, &pos, p[0]);
		append_text(buf, &pos, ",");
		append_hex8(buf, &pos, p[1]);
		append_text(buf, &pos, ",");
		append_hex8(buf, &pos, p[2]);
		append_text(buf, &pos, ",");
		append_hex8(buf, &pos, p[3]);
		append_text(buf, &pos, ",");
		append_hex8(buf, &pos, p[4]);
		append_text(buf, &pos, ",");
		append_hex8(buf, &pos, p[5]);
		append_text(buf, &pos, ",");
		append_hex8(buf, &pos, p[6]);
		append_text(buf, &pos, ",");
		append_hex8(buf, &pos, p[7]);
	}
	append_text(buf, &pos, "]\n");
	buf[pos] = 0;
	append_log(buf);
}

static void xlog_topcat_slot_tail_words(int idx, u32 obj)
{
	char buf[256];
	int pos = 0;
	u32 *p = obj ? (u32 *)(obj + 0x320) : NULL;

	append_text(buf, &pos, "topcat slot");
	append_int(buf, &pos, idx);
	append_text(buf, &pos, " tail=");
	append_hex8(buf, &pos, obj);
	append_text(buf, &pos, " [");
	if (p) {
		append_hex8(buf, &pos, p[0]);
		append_text(buf, &pos, ",");
		append_hex8(buf, &pos, p[1]);
		append_text(buf, &pos, ",");
		append_hex8(buf, &pos, p[2]);
		append_text(buf, &pos, ",");
		append_hex8(buf, &pos, p[3]);
		append_text(buf, &pos, ",");
		append_hex8(buf, &pos, p[4]);
		append_text(buf, &pos, ",");
		append_hex8(buf, &pos, p[5]);
		append_text(buf, &pos, ",");
		append_hex8(buf, &pos, p[6]);
		append_text(buf, &pos, ",");
		append_hex8(buf, &pos, p[7]);
	}
	append_text(buf, &pos, "]\n");
	buf[pos] = 0;
	append_log(buf);
}

static void xlog_topcat_slots_for_umd(const char *label)
{
	u32 obj = top_category_runtime_obj;
	u32 ptr360;
	u32 *slot_ptrs;
	char buf[96];
	int pos = 0;

	if (!obj)
		return;

	ptr360 = *(u32 *)(obj + 0x360);
	if (!ptr360)
		return;

	slot_ptrs = (u32 *)ptr360;

	append_text(buf, &pos, label);
	append_text(buf, &pos, " ptr360=");
	append_hex8(buf, &pos, ptr360);
	append_text(buf, &pos, "\n");
	buf[pos] = 0;
	append_log(buf);

	xlog_topcat_slot_detail(3, slot_ptrs[3]);
	xlog_topcat_slot_detail(4, slot_ptrs[4]);
	xlog_topcat_slot_detail(5, slot_ptrs[5]);
	xlog_topcat_slot_words(3, slot_ptrs[3]);
	xlog_topcat_slot_words(4, slot_ptrs[4]);
	xlog_topcat_slot_words(5, slot_ptrs[5]);
	xlog_topcat_slot_tail_words(3, slot_ptrs[3]);
	xlog_topcat_slot_tail_words(4, slot_ptrs[4]);
	xlog_topcat_slot_tail_words(5, slot_ptrs[5]);
}

static void xlog_topcat_ctx380(void *ctx)
{
	u32 *states = (u32 *)((char *)ctx + 0x17C);
	char buf[256];
	int pos = 0;

	append_text(buf, &pos, "topcat ctx380=");
	append_hex8(buf, &pos, (u32)states);
	append_text(buf, &pos, " [");
	append_hex8(buf, &pos, states[0]);
	append_text(buf, &pos, ",");
	append_hex8(buf, &pos, states[1]);
	append_text(buf, &pos, ",");
	append_hex8(buf, &pos, states[2]);
	append_text(buf, &pos, ",");
	append_hex8(buf, &pos, states[3]);
	append_text(buf, &pos, ",");
	append_hex8(buf, &pos, states[4]);
	append_text(buf, &pos, ",");
	append_hex8(buf, &pos, states[5]);
	append_text(buf, &pos, ",");
	append_hex8(buf, &pos, states[6]);
	append_text(buf, &pos, ",");
	append_hex8(buf, &pos, states[7]);
	append_text(buf, &pos, "]\n");
	buf[pos] = 0;
	append_log(buf);
}

static void xlog_topcat_setup(void *ctx)
{
	char buf[128];
	int pos = 0;

	append_text(buf, &pos, "topcat setup call=");
	append_int(buf, &pos, ++topcat_setup_calls);
	append_text(buf, &pos, " ctx=");
	append_hex8(buf, &pos, (u32)ctx);
	append_text(buf, &pos, " video=");
	append_int(buf, &pos, saw_umd_video_path);
	append_text(buf, &pos, " game=");
	append_int(buf, &pos, saw_umd_game_path);
	append_text(buf, &pos, "\n");
	buf[pos] = 0;
	append_log(buf);
	xlog_topcat_ctx380(ctx);
}

static void xlog_topcat_select(void *ctx, int topitem, int ret)
{
	char buf[192];
	int pos = 0;
	u32 obj = *(u32 *)((char *)ctx + 0xA6C);
	u32 obj338 = obj ? *(u32 *)(obj + 0x338) : 0;
	u32 state = (topitem >= 0 && topitem < 8) ? *(u32 *)((char *)ctx + 0x17C + topitem * 4) : 0;

	append_text(buf, &pos, "topcat select call=");
	append_int(buf, &pos, ++topcat_select_calls);
	append_text(buf, &pos, " ctx=");
	append_hex8(buf, &pos, (u32)ctx);
	append_text(buf, &pos, " top=");
	append_int(buf, &pos, topitem);
	append_text(buf, &pos, " ret=");
	append_int(buf, &pos, ret);
	append_text(buf, &pos, " state=");
	append_hex8(buf, &pos, state);
	append_text(buf, &pos, " obj338=");
	append_hex8(buf, &pos, obj338);
	append_text(buf, &pos, " video=");
	append_int(buf, &pos, saw_umd_video_path);
	append_text(buf, &pos, " game=");
	append_int(buf, &pos, saw_umd_game_path);
	append_text(buf, &pos, "\n");
	buf[pos] = 0;
	append_log(buf);
}

static void xlog_topcat_position(void *obj, int topitem, int ret)
{
	char buf[256];
	int pos = 0;
	u32 ptr360 = obj ? *(u32 *)((char *)obj + 0x360) : 0;
	u32 child = 0;
	u32 child330 = 0;
	u32 child338 = 0;
	u32 child33c = 0;

	if (ptr360 && topitem >= 0 && topitem < 8) {
		child = ((u32 *)ptr360)[topitem];
		if (child) {
			child330 = *(u32 *)(child + 0x330);
			child338 = *(u32 *)(child + 0x338);
			child33c = *(u32 *)(child + 0x33C);
		}
	}

	append_text(buf, &pos, "topcat pos call=");
	append_int(buf, &pos, ++topcat_position_calls);
	append_text(buf, &pos, " obj=");
	append_hex8(buf, &pos, (u32)obj);
	append_text(buf, &pos, " top=");
	append_int(buf, &pos, topitem);
	append_text(buf, &pos, " ret=");
	append_int(buf, &pos, ret);
	append_text(buf, &pos, " child=");
	append_hex8(buf, &pos, child);
	append_text(buf, &pos, " 330=");
	append_hex8(buf, &pos, child330);
	append_text(buf, &pos, " 338=");
	append_hex8(buf, &pos, child338);
	append_text(buf, &pos, " 33C=");
	append_hex8(buf, &pos, child33c);
	append_text(buf, &pos, " video=");
	append_int(buf, &pos, saw_umd_video_path);
	append_text(buf, &pos, " game=");
	append_int(buf, &pos, saw_umd_game_path);
	append_text(buf, &pos, "\n");
	buf[pos] = 0;
	append_log(buf);
}

static void xlog_addvsh_return(const char *kind, int topitem, int ret)
{
	char buf[128];
	int pos = 0;

	append_text(buf, &pos, kind);
	append_text(buf, &pos, " top=");
	append_int(buf, &pos, topitem);
	append_text(buf, &pos, " ret=");
	append_int(buf, &pos, ret);
	append_text(buf, &pos, "\n");
	buf[pos] = 0;
	append_log(buf);
}

static void xlog_wrapped_addvsh_return(const char *kind, int original_topitem, int adjusted, int ret, const char *text)
{
	char buf[192];
	int pos = 0;

	append_text(buf, &pos, kind);
	append_text(buf, &pos, " orig=");
	append_int(buf, &pos, original_topitem);
	append_text(buf, &pos, " adj=");
	append_int(buf, &pos, adjusted);
	append_text(buf, &pos, " ret=");
	append_int(buf, &pos, ret);
	append_text(buf, &pos, " text=");
	append_text(buf, &pos, text);
	append_text(buf, &pos, "\n");
	buf[pos] = 0;
	append_log(buf);
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
	if (!strncmp(file, "/UMD_VIDEO/", 11)) {
		saw_umd_video_path = 1;
		xlog_umd_path("umd video", file);
	}
	else if (!strncmp(file, "/PSP_GAME/", 10) &&
	         strcmp(file, "/PSP_GAME/SYSDIR/UPDATE/PARAM.SFO") != 0) {
		saw_umd_game_path = 1;
		xlog_umd_path("umd game", file);
	}

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
static int add_vsh_wrapped_call = 0;
static int logged_umd_filter_caller;

/* Prologue-patch trampoline buffer. We attempt to patch the real
   AddVshItem's first two instructions to `j AddVshItemFilter; nop`. If it
   takes effect, every call -- including the xmbctrl wrapper forwarding the
   trigger item to the real function -- routes through our filter so the
   first trigger can ALSO be hidden. If it doesn't take effect (we observed
   this on v1.3fix2-derived code), the only consequence is that one
   trigger item still shows; the rest of the code still works. */
static u32 add_vsh_trampoline[4] __attribute__((section(".data"), aligned(4))) = { 0, 0, 0, 0 };
void AddVshItemFilterEntry(void);

int skip(SceVshItem *item, int location);  /* forward decl, defined further down */
static int adjust_topitem_for_hidden_categories(int topitem);

__asm__(
	".set noreorder\n"
	".globl AddVshItemFilterEntry\n"
	"AddVshItemFilterEntry:\n"
	"lui $t9, %hi(add_vsh_filter_ra)\n"
	"sw $ra, %lo(add_vsh_filter_ra)($t9)\n"
	"j AddVshItemFilter\n"
	"nop\n"
	".set reorder\n"
);

int AddVshItemFilter(void *a0, int topitem, SceVshItem *item)
{
	u32 caller_ra = add_vsh_filter_ra;

	if (!add_vsh_wrapped_call) {
		xlog_media_item("filter", topitem, topitem, item->text);
		if (!strcmp(item->text, "msgshare_umd")) {
			xlog_filter_caller(caller_ra, topitem, item->text);
			xlog_topcat_runtime_state();
			if (!logged_umd_filter_caller) {
				logged_umd_filter_caller = 1;
				xlog_module_for_addr(caller_ra);
				xlog_code_words("filter caller code", caller_ra - 0x20, 24);
				xlog_cstring("filter caller str1", (const char *)0x0881E438);
				xlog_cstring("filter caller str2", (const char *)0x0881E440);
				xlog_code_words("addvsh wrapper code", (u32)AddVshItem, 12);
			}
		}
	}

	/* Items reaching us here either came from xmbctrl forwarding (the
	   trigger item, or its CFW item insertions) or any other caller of
	   the real AddVshItem. Apply the user's hide rules and forward via
	   the trampoline if allowed. Location 0 is the default; the
	   trigger items and CFW item names don't depend on location, so
	   that's fine. */
	if(skip(item, 0)) {
		int (*trampoline)(void *, int, SceVshItem *) =
			(int(*)(void *, int, SceVshItem *))add_vsh_trampoline;
		int tracing_umd = (!add_vsh_wrapped_call && !strcmp(item->text, "msgshare_umd"));
		if (tracing_umd)
			xlog_topcat_slots_for_umd("umd slots pre");
		int ret = trampoline(a0, topitem, item);
		if (tracing_umd)
			xlog_topcat_slots_for_umd("umd slots post");

		if (tracing_umd)
			xlog_addvsh_return("filter addvsh", topitem, ret);
		return ret;
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
		xlog_topcat_ctx380(ctx);
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

	xlog_topcat_count(obj, count, new_count, ctx);

	if (count > new_count) {
		if (hide_top_category(0)) {
			if (!top_category_runtime_return_override_logged) {
				top_category_runtime_return_override_logged = 1;
				xlog("topcat: settings-hidden skip runtime count patch %d -> %d\n",
					count, new_count);
			}
			return count;
		}

		if (!top_category_count_logged) {
			top_category_count_logged = 1;
			xlog("topcat: runtime count returned %d -> %d\n", count, new_count);
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
	if(force_trigger || xlog_hook(0, item)) {
		add_vsh_wrapped_call = 1;
		AddVshItem(a0, topitem, item);
		add_vsh_wrapped_call = 0;
	}

	return 0;
}

static int UmdMediaStatePatched(void *ctx)
{
	int ret = UmdMediaStateFunc(ctx);
	int category = *(int *)((char *)ctx + 5160);

	xlog_umd_state_call(ctx, ret, category);

	return ret;
}

static void TopcatStateSetupPatched(void *ctx)
{
	TopcatStateSetupFunc(ctx);
	xlog_topcat_setup(ctx);
}

static int TopcatSelectHelperPatched(void *ctx, int topitem)
{
	int ret = TopcatSelectHelperFunc(ctx, topitem);

	if (saw_umd_video_path || saw_umd_game_path)
		xlog_topcat_select(ctx, topitem, ret);

	return ret;
}

static int TopcatPositionPatched(void *obj, int topitem)
{
	int ret = TopcatPositionFunc(obj, topitem);
	u32 ptr360 = obj ? *(u32 *)((char *)obj + 0x360) : 0;
	u32 child = 0;

	if (ptr360 && topitem >= 0 && topitem < 8) {
		child = ((u32 *)ptr360)[topitem];
	}

	if ((saw_umd_video_path || saw_umd_game_path) &&
	    (topitem == 4 || topitem == 5))
		xlog_topcat_position(obj, topitem, ret);

	return ret;
}

static int UmdVideoAddPatchedRet(void *a0, int topitem, SceVshItem *item)
{
	int adjusted = adjust_topitem_for_hidden_categories(topitem);
	int ret;

	add_vsh_wrapped_call = 1;
	ret = AddVshItem(a0, adjusted, item);
	add_vsh_wrapped_call = 0;
	xlog_wrapped_addvsh_return("umd video add", topitem, adjusted, ret, item->text);
	return ret;
}

static int UmdGameAddPatchedRet(void *a0, int topitem, SceVshItem *item)
{
	int adjusted = adjust_topitem_for_hidden_categories(topitem);
	int ret;

	add_vsh_wrapped_call = 1;
	ret = AddVshItem(a0, adjusted, item);
	add_vsh_wrapped_call = 0;
	xlog_wrapped_addvsh_return("umd game add", topitem, adjusted, ret, item->text);
	return ret;
}

static int UmdVideoSelectShiftPatched(void *ctx, int topitem)
{
	int adjusted = adjust_topitem_for_hidden_categories(topitem);
	int ret = TopcatSelectShiftedFunc(ctx, adjusted, topitem);

	if (saw_umd_video_path) {
		char buf[128];
		int pos = 0;
		append_text(buf, &pos, "umd video select orig=");
		append_int(buf, &pos, topitem);
		append_text(buf, &pos, " adj=");
		append_int(buf, &pos, adjusted);
		append_text(buf, &pos, " ret=");
		append_int(buf, &pos, ret);
		append_text(buf, &pos, "\n");
		buf[pos] = 0;
		append_log(buf);
	}

	return ret;
}

static int UmdGameSelectShiftPatched(void *ctx, int topitem)
{
	int adjusted = adjust_topitem_for_hidden_categories(topitem);
	int ret = TopcatSelectShiftedFunc(ctx, adjusted, topitem);

	if (saw_umd_video_path || saw_umd_game_path) {
		char buf[128];
		int pos = 0;
		append_text(buf, &pos, "umd game select orig=");
		append_int(buf, &pos, topitem);
		append_text(buf, &pos, " adj=");
		append_int(buf, &pos, adjusted);
		append_text(buf, &pos, " ret=");
		append_int(buf, &pos, ret);
		append_text(buf, &pos, "\n");
		buf[pos] = 0;
		append_log(buf);
	}

	return ret;
}

int AddVshItemPatchedPhoto(void *a0, int topitem, SceVshItem *item)
{
	topitem = adjust_topitem_for_hidden_categories(topitem);
	int force_trigger = is_xmbctrl_trigger(item->text) && !xmbctrl_triggered;
	if(force_trigger) xmbctrl_triggered = 1;
	if(force_trigger || xlog_hook(1, item)) {
		add_vsh_wrapped_call = 1;
		AddVshItem(a0, topitem, item);
		add_vsh_wrapped_call = 0;
	}

	return 0;
}

int AddVshItemPatchedMusic(void *a0, int topitem, SceVshItem *item)
{
	topitem = adjust_topitem_for_hidden_categories(topitem);
	int force_trigger = is_xmbctrl_trigger(item->text) && !xmbctrl_triggered;
	if(force_trigger) xmbctrl_triggered = 1;
	if(force_trigger || xlog_hook(2, item)) {
		add_vsh_wrapped_call = 1;
		AddVshItem(a0, topitem, item);
		add_vsh_wrapped_call = 0;
	}

	return 0;
}

int AddVshItemPatchedVideo(void *a0, int topitem, SceVshItem *item)
{
	int original_topitem = topitem;
	topitem = adjust_topitem_for_hidden_categories(topitem);
	if (original_topitem != topitem)
		xlog_media_item("video", original_topitem, topitem, item->text);
	if (saw_umd_video_path)
		xlog_media_hit("video", a0, original_topitem, topitem, item->text);
	int force_trigger = is_xmbctrl_trigger(item->text) && !xmbctrl_triggered;
	if(force_trigger) xmbctrl_triggered = 1;
	if(force_trigger || xlog_hook(3, item)) {
		add_vsh_wrapped_call = 1;
		{
			int ret = AddVshItem(a0, topitem, item);
			xlog_wrapped_addvsh_return("video addvsh", original_topitem, topitem, ret, item->text);
		}
		add_vsh_wrapped_call = 0;
	}

	return 0;
}

int AddVshItemPatchedGame(void *a0, int topitem, SceVshItem *item)
{
	int original_topitem = topitem;
	topitem = adjust_topitem_for_hidden_categories(topitem);
	if (original_topitem != topitem)
		xlog_media_item("game", original_topitem, topitem, item->text);
	if (saw_umd_video_path || saw_umd_game_path)
		xlog_media_hit("game", a0, original_topitem, topitem, item->text);
	int force_trigger = is_xmbctrl_trigger(item->text) && !xmbctrl_triggered;
	if(force_trigger) xmbctrl_triggered = 1;
	if(force_trigger || xlog_hook(4, item)) {
		add_vsh_wrapped_call = 1;
		{
			int ret = AddVshItem(a0, topitem, item);
			xlog_wrapped_addvsh_return("game addvsh", original_topitem, topitem, ret, item->text);
		}
		add_vsh_wrapped_call = 0;
	}

	return 0;
}

int AddVshItemPatchedGameSavedataMs(void *a0, int topitem, SceVshItem *item)
{
	topitem = adjust_topitem_for_hidden_categories(topitem);
	int force_trigger = is_xmbctrl_trigger(item->text) && !xmbctrl_triggered;
	if(force_trigger) xmbctrl_triggered = 1;
	if(force_trigger || xlog_hook(5, item)) {
		add_vsh_wrapped_call = 1;
		AddVshItem(a0, topitem, item);
		add_vsh_wrapped_call = 0;
	}

	return 0;
}

int AddVshItemPatchedGameSavedataEf(void *a0, int topitem, SceVshItem *item)
{
	topitem = adjust_topitem_for_hidden_categories(topitem);
	int force_trigger = is_xmbctrl_trigger(item->text) && !xmbctrl_triggered;
	if(force_trigger) xmbctrl_triggered = 1;
	if(force_trigger || xlog_hook(6, item)) {
		add_vsh_wrapped_call = 1;
		AddVshItem(a0, topitem, item);
		add_vsh_wrapped_call = 0;
	}

	return 0;
}

void PatchVshMain(u32 text_addr)
{
	TopcatSelectShiftedFunc = (int (*)(void *, int, int))(text_addr + 0x22998);

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
	xlog_code_words("addvsh target", (u32)AddVshItem, 12);
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

		_sw(0x08000000 | (((u32)AddVshItemFilterEntry >> 2) & 0x03FFFFFF), real_addvsh);
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

	if (umd_media_state_patch) {
		HIJACK_FUNCTION(text_addr + umd_media_state_patch, UmdMediaStatePatched, UmdMediaStateFunc);
		xlog("  umd state patch=0x%X site=0x%08X\n",
			umd_media_state_patch, text_addr + umd_media_state_patch);
	}

	if (topcat_state_setup_patch) {
		HIJACK_FUNCTION(text_addr + topcat_state_setup_patch, TopcatStateSetupPatched, TopcatStateSetupFunc);
		xlog("  topcat setup patch=0x%X site=0x%08X\n",
			topcat_state_setup_patch, text_addr + topcat_state_setup_patch);
	}

	if (topcat_select_helper_patch) {
		HIJACK_FUNCTION(text_addr + topcat_select_helper_patch, TopcatSelectHelperPatched, TopcatSelectHelperFunc);
		xlog("  topcat select patch=0x%X site=0x%08X\n",
			topcat_select_helper_patch, text_addr + topcat_select_helper_patch);
		xlog_code_words("topcat select target", text_addr + topcat_select_helper_patch, 16);
		xlog_code_words("topcat select tramp", (u32)TopcatSelectHelperFunc, 8);
	}

	xlog_code_words("topcat select shifted", (u32)TopcatSelectShiftedFunc, 16);

	if (topcat_position_patch) {
		u32 tramp_target;

		HIJACK_FUNCTION(text_addr + topcat_position_patch, TopcatPositionPatched, TopcatPositionFunc);
		xlog("  topcat pos patch=0x%X site=0x%08X\n",
			topcat_position_patch, text_addr + topcat_position_patch);
		xlog_code_words("topcat pos target", text_addr + topcat_position_patch, 16);
		xlog_code_words("topcat pos tramp", (u32)TopcatPositionFunc, 8);
		tramp_target = resolve_jump_target((u32)TopcatPositionFunc, _lw((u32)TopcatPositionFunc));
		xlog_module_for_addr(tramp_target);
		xlog_code_words("topcat pos real", tramp_target, 16);
	}

	xlog_resolved_helper("topcat down stub", text_addr + 0x3F230);
	xlog_resolved_helper("topcat settle stub", text_addr + 0x3F378);
	xlog_resolved_helper("topcat up stub", text_addr + 0x3F798);

	MAKE_CALL(text_addr + 0x22CDC, UmdVideoAddPatchedRet);
	MAKE_CALL(text_addr + 0x22CEC, UmdVideoSelectShiftPatched);
	MAKE_CALL(text_addr + 0x22D5C, UmdGameAddPatchedRet);
	MAKE_CALL(text_addr + 0x22DA4, UmdGameSelectShiftPatched);

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
		xlog_vsh_text(text_addr);
		xlog("OnModuleStart: vsh_module text=0x%08X patching s6=%d s51=%d s52=%d s53=%d s38=%d\n",
			text_addr, set[6], set[51], set[52], set[53], set[38]);
		PatchVshMain(text_addr);
	}

	return ret;
}

int module_start(SceSize args, void *argp)
{
	log_enabled = 0;
	log_path[0] = 0;

	if (argp) {
		if (!strncmp((const char *)argp, "ms0:/", 5)) {
			strcpy(log_path, "ms0:/xmbih.log");
			log_enabled = 1;
		}
		else if (!strncmp((const char *)argp, "ef0:/", 5)) {
			strcpy(log_path, "ef0:/xmbih.log");
			log_enabled = 1;
		}
	}

	if (log_enabled) {
		SceUID fd = sceIoOpen(log_path, PSP_O_WRONLY | PSP_O_CREAT | PSP_O_TRUNC, 0777);
		if (fd >= 0)
			sceIoClose(fd);
	}

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
			topcat_state_setup_patch = 0x22AF4;
			topcat_select_helper_patch = 0x22928;
			topcat_position_patch = 0x3F4E0;
			umd_media_state_patch = 0x2AEAC;
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
	xlog_boot_state(top_category_hidden_count);
	if (!set[55]) {
		return 0;
	}

	xlog_raw_both("ck7: pre-sctrlHENSetStartModuleHandler\n");
	previous = sctrlHENSetStartModuleHandler(OnModuleStart);
	xlog_raw_both("ck8: post-sctrlHENSetStartModuleHandler\n");

	return 0;
}

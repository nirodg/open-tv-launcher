/* launcher/act.c — ACT dispatch. Port of the prototype's ACT/GUARD/
 * CHANGE_TOAST/afterChange/options plus the settings flows (scan/connect/
 * pair, app management, power, settime). Deferred screens (Store, Live TV,
 * search, text entry, app UIs) answer politely instead of navigating; each
 * notes the screen that replaces it in the real build. */
#define _GNU_SOURCE
#include "drawui.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static bool eq(const char *a, const char *b) { return a && b && !strcmp(a, b); }
/* bounded copy that does not trip -Wformat-truncation */
static void scopy(char *dst, size_t cap, const char *src)
{
	size_t n = strlen(src);
	if (n > cap - 1) n = cap - 1;
	memcpy(dst, src, n);
	dst[n] = 0;
}

/* forward decls (dialog callbacks reference each other) */
static void opt_open(void), opt_pin(void), opt_move_l(void), opt_move_r(void);
static void opt_appinfo(void), opt_uninstall(void);
static const char *abuf_pin(const char *id);
static void anti_choose(int i);
static void picreset_close(int i);
static const DlgBtn *settime_btns(void);

/* rebuild the current screen (rows re-read state); focus is kept by key */
static void refresh(void) { scene_refresh(); }

/* ── deferred-screen toasts ───────────────────────────────────────────── */
static const char *later_name(const char *id, const App *a)
{
	if (a) return a->name;
	if (eq(id, "store")) return "The Store";
	if (eq(id, "tv")) return "Live TV";
	if (eq(id, "search")) return "Search";
	if (eq(id, "player")) return "The player";
	return "That screen";
}
static void later(const char *id, const App *a)
{
	char m[96];
	snprintf(m, sizeof m, "%s arrives in a later build", later_name(id, a));
	toast(m, "info");
}

/* ── open an app ─────────────────────────────────────────────────────────
 * Real build: the app's own package launches via the launcher intent. */
static void open_app(const char *id)
{
	const App *a = app_info(id);
	if (!a) return;
	if (a->input)
		sv_seti("lastIn", 1);
	else
		appst_set(id, "stopped", 0);
	if (a->go && a->go[0]) {
		if (!strncmp(a->go, "settings:", 9)) {
			SetCtx *c = set_ctx();
			c->nsub = 0;
			c->last_main[0] = 0;
			scene_go("settings", a->go + 9);
			return;
		}
		later(id, a); /* tv:guide / store:discover */
		return;
	}
	later(id, a); /* the app's own UI is a later build */
}

/* ── navigation ────────────────────────────────────────────────────────── */
static void act_gos(const char *page)
{
	SetCtx *c = set_ctx();
	c->nsub = 0;
	c->last_main[0] = 0;
	scene_go("settings", page);
}
static void act_spage(const char *p)
{
	if (!eq(scene_cur_id(), "settings")) {
		act_gos(p);
		return;
	}
	char k[40];
	snprintf(k, sizeof k, "sp-%s", p);
	SetCtx *c = set_ctx();
	scene_focus_key(k); /* the sidebar hook switches the page if needed */
	if (c->last_main[0]) scene_focus_key(c->last_main);
	else {
		char rk[96];
		snprintf(rk, sizeof rk, "r-%s-0-0", c->page);
		scene_focus_key(rk); /* first row of the fresh page */
	}
}

/* ── afterChange: side effects + toast for a state change ──────────────── */
void act_after_change(const char *key)
{
	if (eq(key, "input") && !eq(sv("input"), "Home screen")) {
		/* real build: the input app opens (hdmi1/hdmi2) */
		lpop_all();
		sv_set("input", "Home screen");
		toast("HDMI inputs arrive in a later build", "input");
		g_dirty = true;
		return;
	}
	if (eq(key, "sleep"))
		sv_seti("sleepAt", eq(sv("sleep"), "Off") ? 0
				  : (int)scene_now() + atoi(sv("sleep")) * 60);
	if (eq(key, "autoTime") && sv_true("autoTime"))
		sv_seti("timeAdj", 0);
	if (eq(key, "bt") && sv_true("bt"))
		sv_seti("btSeen", 0);
	if (eq(key, "photoSrc") && !eq(sv("photoSrc"), "Online"))
		sv_set("wall", "Default");
	if (eq(key, "photoSrc") || eq(key, "wall") || eq(key, "wifi"))
		wall_invalidate(); /* the cached wallpaper re-renders */
	if (eq(key, "wifi") && sv_true("wifi"))
		sv_seti("wifiSeen", 0);

	/* sound falls back to the TV speakers when its output goes away */
	const char *fb = eq(key, "bt") || eq(key, "cec") ? audio_fallback() : "";
	if (eq(key, "bt")) {
		char m[160];
		snprintf(m, sizeof m, "%s%s",
			 sv_true("bt") ? "Bluetooth on" : "Bluetooth off", fb);
		toast(m, "bluetooth");
	} else {
		toast_change(key);
		if (fb[0])
			toast("Sound is on the TV speakers now", "speaker");
	}
	g_dirty = true;
	refresh();
}

/* ── picker values (setOpt) ────────────────────────────────────────────── */
static void photo_consent_yes(void)
{
	sv_set("photoSrc", "Online");
	act_after_change("photoSrc");
}
void set_opt(const char *key, const char *v)
{
	if (eq(v, sv(key)))
		return;
	if (eq(key, "res")) {
		/* real build: DRM mode-set + a "keep this display setting?"
		 * countdown that reverts on its own. */
		sv_set("res", v);
		char m[96];
		snprintf(m, sizeof m, "Resolution: %s", v);
		toast(m, "display");
		g_dirty = true;
		return;
	}
	if (eq(key, "photoSrc") && eq(v, "Online")) {
		static const DlgBtn b[] = {
			{ "Use online photos", "check", photo_consent_yes, false },
			{ "Cancel", "x", NULL, false },
		};
		dialog_open("Use online photos?",
			    "The TV will download photos from picsum.photos, a free service that serves photos from Unsplash. The service sees this TV's IP address. Nothing else is sent, and you can switch back at any time.",
			    b, 2, 1, NULL);
		return;
	}
	/* free-text setOpt paths need the on-screen keyboard (a later build) */
	if (eq(key, "tvName") && eq(v, "Custom name…")) {
		toast("Custom names arrive in a later build", "check");
		return;
	}
	if (eq(key, "proxy") && eq(v, "Manual")) {
		toast("Proxy setup arrives in a later build", "link");
		return;
	}
	if (eq(key, "ipMode") && eq(v, "Static")) {
		toast("Static addresses arrive in a later build", "link");
		return;
	}
	bool photo_wall = eq(key, "wall") && eq(v, "Photo of the day");
	sv_set(key, v);
	act_after_change(key);
	/* photo walls download from the network; a later build serves them */
	if (photo_wall)
		toast("Online photos arrive in a later build", "image");
}

/* ── toggles that ask before they change something with side effects ───── */
static char pend_key[24];
static int pend_nv;
static void pend_apply(void)
{
	sv_seti(pend_key, pend_nv);
	act_after_change(pend_key);
}
static bool guard_for(const char *key, bool nv, const char **title,
		      const char **text, const char **ok)
{
	if (eq(key, "bt") && !nv) {
		*title = "Turn off Bluetooth?";
		*text = "Headphones and other accessories will disconnect. The remote keeps working with its basic buttons, but voice search stops until Bluetooth is back on.";
		*ok = "Turn off";
		return true;
	}
	if (eq(key, "diag") && nv) {
		*title = "Share usage & diagnostics?";
		*text = "Once a day the TV sends crash reports and anonymous counts of which screens are used to the Open TV project. Nothing is linked to you, and you can turn this off at any time.";
		*ok = "Turn on";
		return true;
	}
	if (eq(key, "st_usb") && nv) {
		*title = "Allow installs from USB & files?";
		*text = "Apps from files aren't checked by a source you trust. The TV still asks before each install.";
		*ok = "Allow";
		return true;
	}
	if (eq(key, "cec") && !nv && eq(sv("soundOut"), "Soundbar (HDMI ARC)")) {
		*title = "Turn off HDMI-CEC?";
		*text = "The soundbar is controlled through HDMI-CEC, so sound will move to the TV speakers.";
		*ok = "Turn off";
		return true;
	}
	return false;
}

/* ── pending ids for dialog callbacks (DlgBtn has no argument slot) ────── */
static char pend_id[40];

/* ── app management ────────────────────────────────────────────────────── */
static void do_uninstall(void)
{
	const char *id = pend_id;
	const App *a = app_info(id);
	store_uninstall(id);
	if (pin_has(id))
		pin_toggle(id);
	char m[96];
	snprintf(m, sizeof m, "%s uninstalled", a ? a->name : id);
	toast(m, "trash");
	refresh();
}
static void ask_uninstall(const char *id)
{
	const App *a = app_info(id);
	if (!a) return;
	snprintf(pend_id, sizeof pend_id, "%s", id);
	char text[192];
	snprintf(text, sizeof text,
		 "The app%s will be removed from this TV. You can install it again from the Store.",
		 eq(sv("onUninstall"), "Remove data") ? " and its data" : "");
	static const DlgBtn b[] = {
		{ "Uninstall", "trash", do_uninstall, true },
		{ "Cancel", "x", NULL, false },
	};
	char title[96];
	snprintf(title, sizeof title, "Uninstall %s?", a->name);
	dialog_open(title, text, b, 2, 1, NULL);
}

static void do_appstop(void)
{
	const char *id = pend_id;
	const App *a = app_info(id);
	appst_set(id, "stopped", 1);
	char m[96];
	snprintf(m, sizeof m, "%s stopped", a ? a->name : id);
	toast(m, "x");
	refresh();
}
static void do_appdata(void)
{
	const char *id = pend_id;
	const App *a = app_info(id);
	appst_set(id, "data", 0);
	appst_set(id, "cache", 0);
	char m[104];
	snprintf(m, sizeof m, "%s's data cleared", a ? a->name : id);
	toast(m, "trash");
	refresh();
}

/* install / update from the settings app page (store UI is a later build) */
static void appact(const char *id)
{
	const StoreApp *a = store_app(id);
	if (!a) return;
	if (a->installed && !a->next) {
		later(id, NULL); /* the app's own screen */
		return;
	}
	if (!a->installed && a->anti && sv_true("st_warn")) {
		snprintf(pend_id, sizeof pend_id, "%s", id);
		static const DlgBtn b[] = {
			{ "Install anyway", "download", NULL, false },
			{ "Cancel", "x", NULL, false },
		};
		/* handled with onclose so Install anyway can run install_start */
		char title[96], text[256];
		snprintf(title, sizeof title, "%s has anti-features", a->name);
		snprintf(text, sizeof text,
			 "%s. The app works, but it does things some people would rather avoid.",
			 a->anti);
		dialog_open(title, text, b, 2, 1, anti_choose);
		return;
	}
	install_start(id);
}

/* the anti-feature dialog's choice */
static void anti_choose(int i)
{
	if (i == 0)
		install_start(pend_id);
}

static void app_menu(const char *id)
{
	const App *a = app_info(id);
	if (!a) return;
	snprintf(pend_id, sizeof pend_id, "%s", id);
	bool pinned = pin_has(id);
	const StoreApp *sa = a->store ? store_app(id) : NULL;
	static DlgBtn b[8];
	int n = 0;
	b[n++] = (DlgBtn){ "Open", "play", opt_open, false };
	b[n++] = (DlgBtn){ pinned ? "Remove from Home" : "Add to Home",
			   pinned ? "x" : "plus", opt_pin, false };
	if (pinned) {
		b[n++] = (DlgBtn){ "Move left", "arrowl", opt_move_l, false };
		b[n++] = (DlgBtn){ "Move right", "arrowr", opt_move_r, false };
	}
	if (sa && sa->installed) {
		b[n++] = (DlgBtn){ "App info", "info", opt_appinfo, false };
		b[n++] = (DlgBtn){ "Uninstall", "trash", opt_uninstall, true };
	}
	b[n++] = (DlgBtn){ "Cancel", "x", NULL, false };
	dialog_open(a->name, "", b, n, 0, NULL);
}
static void opt_open(void) { open_app(pend_id); }
static void opt_pin(void)
{
	bool added = pin_toggle(pend_id);
	const App *a = app_info(pend_id);
	char m[96];
	snprintf(m, sizeof m, "%s %s Home", a ? a->name : pend_id,
		 added ? "added to" : "removed from");
	toast(m, added ? "plus" : "x");
	refresh();
}
static void opt_move_l(void)
{
	pin_move(pend_id, -1);
	refresh();
	scene_focus_key(abuf_pin(pend_id));
}
static void opt_move_r(void)
{
	pin_move(pend_id, 1);
	refresh();
	scene_focus_key(abuf_pin(pend_id));
}
static const char *abuf_pin(const char *id)
{
	static char b[48];
	snprintf(b, sizeof b, "pin-%s", id);
	return b;
}
static void opt_appinfo(void)
{
	char spec[64];
	snprintf(spec, sizeof spec, "app:%s", pend_id);
	scene_go("settings", "apps");
	sub_open(spec);
}
static void opt_uninstall(void) { ask_uninstall(pend_id); }

/* ── picreset (dialog onclose applies, Reset touches several keys) ─────── */
static void picreset_close(int i)
{
	if (i != 0)
		return;
	sv_set("picMode", "Natural");
	sv_set("colorTemp", "Standard");
	sv_seti("backlight", 80);
	sv_seti("contrast", 50);
	sv_seti("saturation", 50);
	sv_seti("sharpness", 50);
	toast("Picture settings reset", "display");
	g_dirty = true;
	refresh();
}

/* ── pinadd (settings pins page + app menu share the toast) ────────────── */
static void act_pinadd(const char *id)
{
	bool added = pin_toggle(id);
	const App *a = app_info(id);
	char m[96];
	snprintf(m, sizeof m, "%s %s Home", a ? a->name : id,
		 added ? "added to" : "removed from");
	toast(m, added ? "plus" : "x");
	refresh();
}

/* ── network flows ─────────────────────────────────────────────────────── */
static void connect_wifi(const char *ssid)
{
	int n = 0;
	Net *ns = nets_all(&n);
	Net *nn = NULL;
	for (int i = 0; i < n; i++)
		if (!strcmp(ns[i].ssid, ssid)) nn = &ns[i];
	if (!nn) return;
	if (!sv_true("wifi")) {
		toast("Turn on Wi-Fi first", "wifioff");
		return;
	}
	if (strcmp(nn->sec, "Open") && !nn->saved) {
		/* real build: textEntry layer (the on-screen keyboard) asks
		 * for the password, at least 8 characters */
		toast("Password entry arrives in a later build", "wifioff");
		return;
	}
	wifi_connect_start(ssid); /* sys.c settles + toasts "Connected to …" */
}

static void do_wififorget(void)
{
	char ssid[48];
	snprintf(ssid, sizeof ssid, "%s", pend_id);
	wifi_forget(ssid);
	char m[96];
	snprintf(m, sizeof m, "%s forgotten", ssid);
	toast(m, "wifioff");
	refresh();
}

static void do_freespace(void)
{
	int nb;
	const App *const *inst = app_all(&nb);
	for (int i = 0; i < nb; i++)
		appst_set(inst[i]->id, "cache", 0);
	sv_seti("cacheMB", 0);
	char m[64];
	snprintf(m, sizeof m, "%s freed", gb(stor_stats().cache));
	toast(m, "check");
	refresh();
}

/* ── bluetooth flows ───────────────────────────────────────────────────── */
static void do_btforget(void)
{
	const char *id = pend_id;
	BtDev *d = bt_find(id);
	if (!d) return;
	char name[48];
	snprintf(name, sizeof name, "%s", d->name);
	bt_forget(id);
	const char *fb = audio_fallback();
	char m[160];
	snprintf(m, sizeof m, "%s forgotten%s", name, fb);
	toast(m, "bluetooth");
	refresh();
}

/* ── power ─────────────────────────────────────────────────────────────── */
static void do_restart(void) { sys_reboot(); }
static void do_poweroff(void)
{
	/* real build: the display sleeps and the box powers down */
	lpop_all();
	sv_seti("sleepAt", 0);
	sv_set("sleep", "Off");
	scene_go("standby", "");
}
static void do_factory(void)
{
	sys_factory_reset();
	sys_reboot();
	toast("The TV is back to how it came", "refresh");
}
static void factory_step2(void)
{
	static const DlgBtn b[] = {
		{ "Erase everything", "trash", do_factory, true },
		{ "Cancel", "x", NULL, false },
	};
	dialog_open("Erase everything?", "This can't be undone.", b, 2, 1, NULL);
}
static void pm_sleep(void)
{
	cycle_key("sleep", 1);
	act_after_change("sleep");
}

/* ── profile ───────────────────────────────────────────────────────────── */
static void profile_set(const char *p, const char *msg)
{
	sv_set("profile", p);
	toast(msg, "user");
	g_dirty = true;
	refresh();
}
static void prof_a(void) { profile_set("A", "Switched to Alex"); }
static void prof_s(void) { profile_set("S", "Switched to Sam"); }
static void prof_g(void) { profile_set("G", "Guest mode"); }

/* ── set the time (manual adjust dialog) ───────────────────────────────── */
static void settime_close(int i)
{
	static const int sh[4] = { -60, 60, -5, 5 };
	if (i >= 0 && i <= 3) {
		time_shift(sh[i]);
		dialog_big(clock_str());
		dialog_open("Set the time",
			    "The TV keeps this time until you turn automatic time back on.",
			    settime_btns(), 5, i, settime_close);
		return;
	}
	char m[64];
	snprintf(m, sizeof m, "Time set to %s", clock_str());
	toast(m, "clock");
	g_dirty = true;
	refresh();
}
static const DlgBtn *settime_btns(void)
{
	static DlgBtn b[5];
	const char *lbl[5] = { "-1 hour", "+1 hour", "-5 min", "+5 min", "Done" };
	const char *ic[5] = { NULL, NULL, NULL, NULL, "check" };
	for (int i = 0; i < 5; i++)
		b[i] = (DlgBtn){ lbl[i], ic[i], NULL, false };
	return b;
}

/* ── input names (picker over the @inname list) ────────────────────────── */
static char inname_key[16], inname_id[16];
static void inname_picked(const char *v)
{
	sv_set(inname_key, v);
	const App *a = app_info(inname_id);
	char m[128];
	if (eq(v, "None"))
		snprintf(m, sizeof m, "%s has no name", a ? a->name : "Input");
	else
		snprintf(m, sizeof m, "%s is now \"%s\"", a ? a->name : "Input", v);
	toast(m, "monitor");
	g_dirty = true;
	refresh();
}

/* ── hold-OK options (prototype options()) ─────────────────────────────── */
static void act_options(void)
{
	FNode *f = scene_focused();
	if (!f || !f->menu[0]) {
		toast("No options here", "info");
		return;
	}
	char kind[16] = "", id[40] = "";
	const char *colon = strchr(f->menu, ':');
	if (colon) {
		size_t n = (size_t)(colon - f->menu);
		if (n >= sizeof kind) n = sizeof kind - 1;
		memcpy(kind, f->menu, n);
		kind[n] = 0;
		scopy(id, sizeof id, colon + 1);
	} else {
		scopy(kind, sizeof kind, f->menu);
		const char *dash = strchr(f->k, '-'); /* "app-<id>" / "pin-<id>" */
		if (dash)
			scopy(id, sizeof id, dash + 1);
	}
	if (eq(kind, "profile")) {
		act_fire("profile");
		return;
	}
	if (eq(kind, "app") || eq(kind, "pin")) {
		app_menu(id);
		return;
	}
	toast("No options here", "info");
}

/* ── the dispatch table (prototype ACT) ────────────────────────────────── */
void act_init(void) {}

void act_fire(const char *act)
{
	if (!act || !act[0])
		return;
	char cmd[24] = "", arg[160] = "";
	const char *colon = strchr(act, ':');
	if (colon) {
		snprintf(cmd, (size_t)(colon - act) + 1, "%s", act);
		snprintf(arg, sizeof arg, "%s", colon + 1);
	} else {
		snprintf(cmd, sizeof cmd, "%s", act);
	}

	if (eq(cmd, "open")) { open_app(arg); return; }
	if (eq(cmd, "go")) {
		if (eq(arg, "apps") || eq(arg, "settings")) {
			scene_go(arg, "");
			return;
		}
		later(arg, app_info(arg));
		return;
	}
	if (eq(cmd, "gos")) { act_gos(arg); return; }
	if (eq(cmd, "home")) { scene_home(); return; }
	if (eq(cmd, "qp")) { qp_open(); return; }
	if (eq(cmd, "profile")) {
		static const DlgBtn b[] = {
			{ "Alex", "user", prof_a, false },
			{ "Sam", "user", prof_s, false },
			{ "Guest", "user", prof_g, false },
			{ "Cancel", "x", NULL, false },
		};
		dialog_open("Who's watching?", "", b, 4, 0, NULL);
		return;
	}
	if (eq(cmd, "filter")) { sv_set("appsFilter", arg); refresh(); return; }
	if (eq(cmd, "spage")) { act_spage(arg); return; }
	if (eq(cmd, "sub")) { sub_open(arg); return; }
	if (eq(cmd, "tog")) {
		bool nv = !sv_true(arg);
		const char *t, *x, *ok;
		if (guard_for(arg, nv, &t, &x, &ok)) {
			snprintf(pend_key, sizeof pend_key, "%s", arg);
			pend_nv = nv;
			static DlgBtn b[2];
			b[0] = (DlgBtn){ ok, nv ? "check" : "x", pend_apply, false };
			b[1] = (DlgBtn){ "Cancel", "x", NULL, false };
			dialog_open(t, x, b, 2, 1, NULL);
			return;
		}
		sv_seti(arg, nv);
		act_after_change(arg);
		return;
	}
	if (eq(cmd, "cyc")) {
		cycle_key(arg, 1);
		act_after_change(arg);
		return;
	}
	if (eq(cmd, "opt")) { picker_open_key(arg); return; }
	if (eq(cmd, "pick")) { picker_choose(atoi(arg)); return; }
	if (eq(cmd, "dlg")) { dialog_choose(atoi(arg)); return; }
	if (eq(cmd, "why") || eq(cmd, "toast")) { toast(arg, "info"); return; }

	/* network */
	if (eq(cmd, "wifiscan")) { wifi_scan_start(); return; }
	if (eq(cmd, "wificonn")) { connect_wifi(arg); return; }
	if (eq(cmd, "wifiadd")) {
		/* real build: textEntry layer for the hidden network's name */
		toast("Hidden networks arrive in a later build", "wifi");
		return;
	}
	if (eq(cmd, "wififorget")) {
		snprintf(pend_id, sizeof pend_id, "%s", arg);
		static const DlgBtn b[] = {
			{ "Forget", "trash", do_wififorget, true },
			{ "Cancel", "x", NULL, false },
		};
		char title[96], text[192];
		snprintf(title, sizeof title, "Forget %s?", arg);
		snprintf(text, sizeof text,
			 "The TV won't join this network by itself any more. You'll need the password to connect again.");
		dialog_open(title, text, b, 2, 1, NULL);
		return;
	}
	if (eq(cmd, "speedrun")) { speed_start(); return; }

	/* picture */
	if (eq(cmd, "picreset")) {
		static const DlgBtn b[] = {
			{ "Reset", "refresh", NULL, false },
			{ "Cancel", "x", NULL, false },
		};
		/* onclose applies, so Reset can touch several keys at once */
		dialog_open("Reset picture settings?",
			    "Picture mode, backlight, contrast, colour, sharpness and colour temperature go back to how they came.",
			    b, 2, 1, picreset_close);
		return;
	}
	if (eq(cmd, "saverstart")) {
		/* real build: the screen saver layer (photos or the clock) */
		toast("The screen saver arrives in a later build", "moon");
		return;
	}

	/* apps */
	if (eq(cmd, "appstop")) {
		snprintf(pend_id, sizeof pend_id, "%s", arg);
		static const DlgBtn b[] = {
			{ "Force stop", "x", do_appstop, true },
			{ "Cancel", "x", NULL, false },
		};
		char title[96], text[160];
		const App *a = app_info(arg);
		snprintf(title, sizeof title, "Force stop %s?", a ? a->name : arg);
		snprintf(text, sizeof text,
			 "The app closes straight away and may lose anything that wasn't saved.");
		dialog_open(title, text, b, 2, 1, NULL);
		return;
	}
	if (eq(cmd, "appcache")) {
		appst_set(arg, "cache", 0);
		toast("Cache cleared", "check");
		refresh();
		return;
	}
	if (eq(cmd, "appdata")) {
		snprintf(pend_id, sizeof pend_id, "%s", arg);
		static const DlgBtn b[] = {
			{ "Clear data", "trash", do_appdata, true },
			{ "Cancel", "x", NULL, false },
		};
		char title[96], text[208];
		const App *a = app_info(arg);
		snprintf(title, sizeof title, "Clear %s's data?", a ? a->name : arg);
		snprintf(text, sizeof text,
			 "All of the app's files, settings and sign-ins will be deleted from this TV. This can't be undone.");
		dialog_open(title, text, b, 2, 1, NULL);
		return;
	}
	if (eq(cmd, "appperm")) {
		char id[40], perm[16];
		const char *c2 = strchr(arg, ':');
		if (!c2) return;
		snprintf(id, (size_t)(c2 - arg) + 1, "%s", arg);
		snprintf(perm, sizeof perm, "%s", c2 + 1);
		appst_toggle_perm(id, perm);
		int has = 0;
		appst_perm(id, perm, &has);
		const char *pname = perm;
		perm_info(perm, &pname, NULL);
		char low[24];
		snprintf(low, sizeof low, "%s", pname);
		for (char *p = low; *p; p++)
			if (*p >= 'A' && *p <= 'Z')
				*p = (char)(*p - 'A' + 'a');
		const App *a = app_info(id);
		char m[128];
		snprintf(m, sizeof m, "%s %s use %s", a ? a->name : id,
			 has ? "can" : "can't", low);
		toast(m, has ? "check" : "x");
		refresh();
		return;
	}
	if (eq(cmd, "appnotif")) {
		int cur = 0;
		appst_get(arg, "notif", &cur);
		appst_set(arg, "notif", !cur);
		const App *a = app_info(arg);
		char m[128];
		snprintf(m, sizeof m, "Notifications from %s %s", a ? a->name : arg,
			 !cur ? "on" : "off");
		toast(m, "info");
		refresh();
		return;
	}
	if (eq(cmd, "uninstall")) { ask_uninstall(arg); return; }
	if (eq(cmd, "appact")) { appact(arg); return; }
	if (eq(cmd, "pinadd")) { act_pinadd(arg); return; }

	/* inputs */
	if (eq(cmd, "inname")) {
		snprintf(inname_id, sizeof inname_id, "%s", arg);
		snprintf(inname_key, sizeof inname_key, "inputs%c",
			 eq(arg, "hdmi2") ? '2' : '1');
		int n = 0;
		const char *const *o = cyc_opts("@inname", &n);
		const App *a = app_info(arg);
		char title[48];
		snprintf(title, sizeof title, "Name %s", a ? a->name : "input");
		picker_open(title, "Shown when you switch to this input.", o, n,
			    sv(inname_key), inname_picked);
		return;
	}

	/* bluetooth */
	if (eq(cmd, "btscan")) { bt_scan_start(); return; }
	if (eq(cmd, "btpairdo")) { bt_pair_start(arg); return; }
	if (eq(cmd, "btconn")) {
		BtDev *d = bt_find(arg);
		if (d)
			bt_conn_start(arg, !d->conn);
		return;
	}
	if (eq(cmd, "btfind")) {
		toast("Your remote is beeping", "remote");
		return;
	}
	if (eq(cmd, "btforget")) {
		BtDev *d = bt_find(arg);
		if (!d) return;
		snprintf(pend_id, sizeof pend_id, "%s", arg);
		static const DlgBtn b[] = {
			{ "Forget", "trash", do_btforget, true },
			{ "Cancel", "x", NULL, false },
		};
		char title[96], text[256];
		snprintf(title, sizeof title, "Forget %s?", d->name);
		snprintf(text, sizeof text, "%s",
			 eq(d->kind, "remote")
				 ? "The remote keeps working with its basic buttons. To use it over Bluetooth again, hold Home and Back on the remote for 5 seconds to pair it."
				 : "You'll need to pair it again to use it with this TV.");
		dialog_open(title, text, b, 2, 1, NULL);
		return;
	}

	/* storage */
	if (eq(cmd, "freespace")) {
		static char lbl[48];
		char text[160];
		snprintf(lbl, sizeof lbl, "Clear %s", gb(stor_stats().cache));
		snprintf(text, sizeof text,
			 "Clears %s of cached files. Apps keep their data and sign-ins.",
			 gb(stor_stats().cache));
		static DlgBtn b[2];
		b[0] = (DlgBtn){ lbl, "trash", do_freespace, false };
		b[1] = (DlgBtn){ "Cancel", "x", NULL, false };
		dialog_open("Free up space?", text, b, 2, 0, NULL);
		return;
	}
	if (eq(cmd, "gotv")) { later("tv", NULL); return; }

	/* system */
	if (eq(cmd, "settime")) {
		dialog_big(clock_str());
		dialog_open("Set the time",
			    "The TV keeps this time until you turn automatic time back on.",
			    settime_btns(), 5, 1, settime_close);
		return;
	}
	if (eq(cmd, "sysupdate")) { sys_update_start(); return; }
	if (eq(cmd, "licences")) {
		static const DlgBtn b[] = { { "Close", "x", NULL, false } };
		dialog_open("Open-source licences",
			    "Open TV is built from open-source parts. Each keeps its own licence: the Inter typeface - SIL Open Font License 1.1. AndroidX and Compose for TV - Apache 2.0. Media3 (ExoPlayer) - Apache 2.0. Open TV itself - licence not chosen yet.",
			    b, 1, 0, NULL);
		return;
	}
	if (eq(cmd, "factory")) {
		static const DlgBtn b[] = {
			{ "Continue", "refresh", factory_step2, true },
			{ "Cancel", "x", NULL, false },
		};
		dialog_open("Reset to factory settings?",
			    "Everything on this TV will be erased: apps you installed and their data, saved Wi-Fi networks, paired devices, recordings and all settings.",
			    b, 2, 1, NULL);
		return;
	}
	if (eq(cmd, "restart")) {
		static const DlgBtn b[] = {
			{ "Restart", "refresh", do_restart, false },
			{ "Cancel", "x", NULL, false },
		};
		dialog_open("Restart the TV?", "", b, 2, 0, NULL);
		return;
	}
	if (eq(cmd, "poweroff")) {
		static const DlgBtn b[] = {
			{ "Turn off", "power", do_poweroff, false },
			{ "Cancel", "x", NULL, false },
		};
		dialog_open("Turn off the TV?", "", b, 2, 0, NULL);
		return;
	}
	if (eq(cmd, "powermenu")) {
		static char lbl[64];
		snprintf(lbl, sizeof lbl, "Sleep timer · %s", sv("sleep"));
		static DlgBtn b[4];
		b[0] = (DlgBtn){ "Turn off", "power", do_poweroff, false };
		b[1] = (DlgBtn){ "Restart", "refresh", do_restart, false };
		b[2] = (DlgBtn){ lbl, "moon", pm_sleep, false };
		b[3] = (DlgBtn){ "Cancel", "x", NULL, false };
		dialog_open("Power", "", b, 4, 0, NULL);
		return;
	}
	if (eq(cmd, "options")) { act_options(); return; }

	/* store sources / TV / keyboard / search: later builds */
	if (eq(cmd, "srctog") || eq(cmd, "stab") || eq(cmd, "tvtab") ||
	    eq(cmd, "details") || eq(cmd, "cat") || eq(cmd, "updateall") ||
	    eq(cmd, "libtab") || eq(cmd, "hero") || eq(cmd, "addsource") ||
	    eq(cmd, "scan") || eq(cmd, "addplaylist") || eq(cmd, "pltog") ||
	    eq(cmd, "key") || eq(cmd, "shift") || eq(cmd, "kbmode") ||
	    eq(cmd, "bksp") || eq(cmd, "clear") || eq(cmd, "sug") ||
	    eq(cmd, "searchgo") || eq(cmd, "voice") || eq(cmd, "prog") ||
	    eq(cmd, "guide") || eq(cmd, "watch") || eq(cmd, "rec") ||
	    eq(cmd, "remind") || eq(cmd, "fav") || eq(cmd, "playpause") ||
	    eq(cmd, "chup") || eq(cmd, "chdown") || eq(cmd, "cc") ||
	    eq(cmd, "audiotrk") || eq(cmd, "chlist") || eq(cmd, "switch") ||
	    eq(cmd, "help")) {
		static const struct { const char *c, *m; } LATER[] = {
			{ "stab", "The Store arrives in a later build" },
			{ "details", "The Store arrives in a later build" },
			{ "cat", "The Store arrives in a later build" },
			{ "updateall", "The Store arrives in a later build" },
			{ "libtab", "The Store arrives in a later build" },
			{ "srctog", "Store settings arrive in a later build" },
			{ "addsource", "Store settings arrive in a later build" },
			{ "guide", "Live TV arrives in a later build" },
			{ "watch", "Live TV arrives in a later build" },
			{ "chlist", "Live TV arrives in a later build" },
			{ "searchgo", "Search arrives in a later build" },
			{ "voice", "Voice search arrives in a later build" },
		};
		for (unsigned i = 0; i < sizeof(LATER) / sizeof(*LATER); i++)
			if (eq(LATER[i].c, cmd)) {
				toast(LATER[i].m, "info");
				return;
			}
		toast("That arrives in a later build", "info");
		return;
	}
}

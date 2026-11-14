/* launcher/sys.h — shared state helpers for the screens.
 * sys.c owns the ported prototype state (S, CYC, LAUNCH, CATALOG, app state,
 * device mocks). ui.h already declares the state API; this header adds the
 * things screens need beyond it. */
#ifndef OTV_SYS_H
#define OTV_SYS_H

#include "ui.h"

/* ── wifi network mock (prototype DEV.nets) ───────────────────────────── */
typedef struct { const char *ssid, *sec; int sig, ghz; bool saved; } Net;
Net *nets_all(int *n);
const char *net_cur(void);        /* connected ssid, or NULL */
const char *online(void);         /* "Home-5G" style, or NULL */
const char *ipaddr(void);
const char *sig_word(int sig);    /* Weak/Fair/Good/Strong */
const char *net_desc(int i);      /* "Connected · Good signal" etc */
const char *net_sub(int i);       /* "Open network · WPA2" style */

/* ── bluetooth mock ────────────────────────────────────────────────────── */
typedef struct { const char *id, *name, *kind; int batt; bool conn; } BtDev;
BtDev *bt_devs(int *n);
BtDev *bt_find(const char *id);
const BtDev *bt_near(int *n);          /* nearby devices not paired yet */
void bt_forget(const char *id);        /* remove a paired device */
bool bt_add_near(const char *id);      /* pair a nearby device into the list */
const char *bt_kind_word(const char *kind);
bool headphones_ok(void);
const char *audio_fallback(void);      /* " · sound is on the TV speakers now" or "" */

/* ── app catalogue helpers ─────────────────────────────────────────────── */
const char *app_go(const char *id);          /* "settings:picture" or NULL */
const char *app_src_name(const char *src);   /* source display name */
const char *const *app_perms(const char *id, int *n);
int app_hash(const char *s);                 /* stable mock-hash */

/* ── storage ───────────────────────────────────────────────────────────── */
typedef struct { float system, apps, data, cache, rec, used, total; } Stor;
Stor stor_stats(void);
const char *gb(float v);          /* "8.4 GB" / "620 MB" into a static buf */

/* ── time helpers ──────────────────────────────────────────────────────── */
const char *clock_str(void);      /* "14:05" / "2:05 PM" into a static buf */
const char *date_str(bool short_);
const char *greeting(void);
const char *date_short(void);
const char *sleep_target_str(void);   /* "22:30" or "Off" for the sleep timer */
void time_shift(int add_min);     /* manual time adjust (settime dialog) */

/* ── settings page model (screen_settings.c) ───────────────────────────── */
typedef struct {
	const char *t;        /* row type: tog opt sld act nav info bar */
	const char *k;        /* node key (stable) */
	const char *icon;     /* icon id, "sig:N", "app:ID", "dot:hex" or NULL */
	const char *title;
	const char *d;        /* description */
	const char *v;        /* right value text */
	const char *vh;       /* right value lead icon (e.g. "lock") */
	const char *act;      /* data-act, or NULL */
	const char *key;      /* state key for tog/opt/sld */
	bool on;              /* tog override (perms); -1 = use state */
	int  on_set;          /* 0 = use key, 1 = use on */
	bool busy;
	bool danger;
	bool dis; const char *why;
} SRow;

/* context used while building/drawing the main pane */
/* settings page model (screen_settings.c) */
#define SUB_MAX 4
typedef struct {
	const char *page;     /* SPAGES id */
	struct {
		char id[16];      /* sub-page id */
		char arg[96];     /* argument (ssid, app id, perm...) */
		char from[48];    /* node to restore when leaving the sub-page */
	} sub[SUB_MAX];
	int  nsub;            /* sub-page stack depth (0 = top-level page) */
	char last_main[48];   /* last focused main-pane row (sidebar handoff) */
} SetCtx;
SetCtx *set_ctx(void);

void rows_build(Layer *L, const SRow *rows, int n, float x, float y, float w);
float rows_height(const SRow *rows, int n, float w);

/* sub-page entry (screen_settings.c) */
void sub_open(const char *spec);           /* "id" or "id:arg" */
bool sub_active(void);
void sub_close(void);                      /* back from a sub-page */
void sub_enter_hook(const char *id);       /* start wifi/bt scans etc */

/* overlays (overlays.c) — driven by act.c */
void picker_choose(int i);
const char *pick_opt(int i);
void dialog_choose(int i);
const DlgBtn *dlg_btn(int i);
bool overlay_open(const char *name);       /* "picker" | "dialog" | "qp" */

/* picker value application (act.c) */
void set_opt(const char *key, const char *v);

/* ── mock device flows (busy machines driven by sys_tick) ──────────────── */
void wifi_scan_start(void);
void wifi_connect_start(const char *ssid);
void wifi_forget(const char *ssid);
void bt_scan_start(void);
void bt_pair_start(const char *id);
void bt_conn_start(const char *id, bool on);
void sys_update_start(void);
void speed_start(void);
void sys_reboot(void);
void sys_factory_reset(void);          /* restore the initial mock state */
bool install_progress(const char *id, int *pct);
void install_start(const char *id);
void store_uninstall(const char *id);  /* flip the catalogue + drop state */
bool perm_info(const char *perm, const char **name, const char **icon);

/* ── home pins ─────────────────────────────────────────────────────────── */
bool pin_has(const char *id);
bool pin_toggle(const char *id);       /* true = added */
void pin_move(const char *id, int dir);
const char *const *pin_list(int *n);

#endif

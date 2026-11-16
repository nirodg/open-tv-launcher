/* launcher/sys.c — ported prototype state (S/CYC/LAUNCH/CATALOG/appSt) and
 * the mock device layer. In the real OS each mock becomes a gateway or
 * system call; everything device-shaped stays behind this file. */
#define _GNU_SOURCE
#include "sys.h"
#include "media.h"
#include <stdio.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>
#include <dirent.h>
#include <sys/stat.h>
#include <time.h>

/* ── state tables ──────────────────────────────────────────────────────── */
typedef struct { const char *k; const char *v; } SStr;
typedef struct { const char *k; int v; } SInt;

static SStr sstr[] = {
	{ "sleep", "Off" }, { "picMode", "Natural" }, { "soundOut", "TV speakers" },
	{ "input", "Home screen" }, { "res", "3840 × 2160 · 60 Hz" },
	{ "colorTemp", "Standard" }, { "saverType", "Photos" }, { "saverDelay", "10 min" },
	{ "soundMode", "Standard" }, { "lipsync", "0 ms" },
	{ "wifiNet", "Home-5G" }, { "wifiBusy", "" }, { "proxy", "None" }, { "proxyAddr", "" },
	{ "ipMode", "DHCP" }, { "staticIp", "" },
	{ "btPairing", "" }, { "btBusy", "" },
	{ "inputs1", "Game console" }, { "inputs2", "None" },
	{ "lang", "English" }, { "tvName", "Living room TV" }, { "tz", "Automatic" },
	{ "autoOff", "4 hours" }, { "powerOn", "Home screen" },
	{ "dlLoc", "Internal" }, { "onUninstall", "Remove data" },
	{ "photoSrc", "Built in" }, { "wall", "Default" },
	{ "appsFilter", "All" }, { "profile", "A" },
	/* video app state: prefs (prototype appPrefs.video) + sort */
	{ "vidResume", "Ask" }, { "vidSubs", "Off" }, { "vidSkip", "30 seconds" },
	{ "vidSort", "Newest" }, { "vidHw", "1" },
	{ "speedDown", "" }, { "speedUp", "" }, { "speedPing", "" },
};
static SInt sint[] = {
	{ "vol", 24 }, { "muted", 0 }, { "backlight", 80 },
	{ "wifi", 1 }, { "bt", 1 }, { "sleepAt", 0 },
	{ "frameMatch", 1 }, { "hdrMatch", 1 }, { "nightLight", 0 }, { "nightStr", 50 },
	{ "contrast", 50 }, { "saturation", 50 }, { "sharpness", 50 },
	{ "nightAudio", 0 }, { "sysSounds", 1 },
	{ "wifiScan", 0 }, { "wifiSeen", 0 },
	{ "btScan", 0 }, { "btSeen", 0 },
	{ "cec", 1 }, { "cecOn", 1 }, { "cecOff", 1 }, { "autoInput", 1 },
	{ "diag", 0 }, { "location", 0 }, { "micAccess", 1 },
	{ "autoTime", 1 }, { "timeAdj", 0 }, { "clock24", 1 },
	{ "updBusy", 0 }, { "updAt", 0 }, { "sysAutoUpd", 1 },
	{ "st_auto", 1 }, { "st_notify", 1 }, { "st_warn", 1 }, { "st_hide", 0 }, { "st_usb", 0 },
	{ "cacheMB", 1200 },
	{ "antenna", 1 }, { "lastIn", 0 },
};

static const char *sstr_get(const char *k)
{
	for (unsigned i = 0; i < sizeof(sstr) / sizeof(*sstr); i++)
		if (!strcmp(sstr[i].k, k)) return sstr[i].v;
	return "";
}
static SInt *sint_get(const char *k)
{
	for (unsigned i = 0; i < sizeof(sint) / sizeof(*sint); i++)
		if (!strcmp(sint[i].k, k)) return &sint[i];
	return NULL;
}

const char *sv(const char *key) { return sstr_get(key); }
int svi(const char *key)
{
	SInt *s = sint_get(key);
	if (s) return s->v;
	/* numeric strings live in the string table (speed results, timeAdj) */
	return atoi(sstr_get(key));
}
void sv_set(const char *key, const char *val)
{
	for (unsigned i = 0; i < sizeof(sstr) / sizeof(*sstr); i++)
		if (!strcmp(sstr[i].k, key)) { sstr[i].v = val; return; }
}
void sv_seti(const char *key, int val)
{
	SInt *s = sint_get(key);
	if (s) { s->v = val; return; }
	static char bufs[8][24]; static int bi;
	char *b = bufs[bi++ & 7];
	snprintf(b, 24, "%d", val);
	sv_set(key, b);
}
bool sv_true(const char *key) { return svi(key) != 0; }

/* ── option lists (CYC) ───────────────────────────────────────────────── */
static const char *const o_picMode[] = { "Natural", "Vivid", "Cinema", "Game", NULL };
static const char *const o_res[] = {
	"3840 × 2160 · 60 Hz", "3840 × 2160 · 50 Hz", "1920 × 1080 · 60 Hz",
	"1920 × 1080 · 50 Hz", "1280 × 720 · 60 Hz", NULL,
};
static const char *const o_colorTemp[] = { "Cool", "Standard", "Warm", NULL };
static const char *const o_saverType[] = { "Photos", "Clock", "Off", NULL };
static const char *const o_saverDelay[] = { "1 min", "5 min", "10 min", "30 min", NULL };
static const char *const o_soundOut[] = { "TV speakers", "Headphones", "Soundbar (HDMI ARC)", NULL };
static const char *const o_soundMode[] = { "Standard", "Cinema", "Voice boost", "Music", NULL };
static const char *const o_lipsync[] = { "0 ms", "20 ms", "40 ms", "60 ms", "80 ms", "120 ms", "200 ms", NULL };
static const char *const o_proxy[] = { "None", "Manual", NULL };
static const char *const o_ipMode[] = { "DHCP", "Static", NULL };
static const char *const o_lang[] = {
	"English", "Deutsch", "Español", "Français", "Italiano",
	"Nederlands", "Polski", "Português", "Română", NULL,
};
static const char *const o_tz[] = {
	"Automatic", "London (GMT+0)", "Lisbon (GMT+0)", "Paris (GMT+1)", "Berlin (GMT+1)",
	"Bucharest (GMT+2)", "Athens (GMT+2)", "Istanbul (GMT+3)", "Dubai (GMT+4)",
	"New Delhi (GMT+5:30)", "Singapore (GMT+8)", "Tokyo (GMT+9)", "Sydney (GMT+10)",
	"New York (GMT−5)", "Chicago (GMT−6)", "Los Angeles (GMT−8)", NULL,
};
static const char *const o_sleep[] = { "Off", "15 min", "30 min", "60 min", "90 min", "120 min", NULL };
static const char *const o_autoOff[] = { "Never", "1 hour", "4 hours", "8 hours", NULL };
static const char *const o_powerOn[] = { "Home screen", "Last used input", NULL };
static const char *const o_input[] = { "Home screen", "HDMI 1", "HDMI 2", NULL };
static const char *const o_photoSrc[] = { "Built in", "Online", NULL };
static const char *const o_wall[] = { "Default", "Photo of the day", NULL };
static const char *const o_dlLoc[] = { "Internal", "USB drive", NULL };
static const char *const o_onUninstall[] = { "Remove data", "Keep data", NULL };
static const char *const o_tvName[] = { "Living room TV", "Bedroom TV", "Kitchen TV", "Custom name…", NULL };
static const char *const o_inname[] = { "None", "Game console", "Blu-ray player", "Set-top box",
					"PC", "Soundbar", "Streaming stick", NULL };

typedef struct { const char *key; const char *const *opts; } Cyc;
static const Cyc cycs[] = {
	{ "picMode", o_picMode }, { "res", o_res }, { "colorTemp", o_colorTemp },
	{ "saverType", o_saverType }, { "saverDelay", o_saverDelay },
	{ "soundOut", o_soundOut }, { "soundMode", o_soundMode }, { "lipsync", o_lipsync },
	{ "proxy", o_proxy }, { "ipMode", o_ipMode },
	{ "lang", o_lang }, { "tz", o_tz },
	{ "sleep", o_sleep }, { "autoOff", o_autoOff }, { "powerOn", o_powerOn }, { "input", o_input },
	{ "photoSrc", o_photoSrc }, { "wall", o_wall },
	{ "dlLoc", o_dlLoc }, { "onUninstall", o_onUninstall }, { "tvName", o_tvName },
};
static const Cyc cyc_inname = { "@inname", o_inname };

const char *const *cyc_opts(const char *key, int *n)
{
	const Cyc *c = NULL;
	if (!strcmp(key, "@inname")) c = &cyc_inname;
	else for (unsigned i = 0; i < sizeof(cycs) / sizeof(*cycs); i++)
		if (!strcmp(cycs[i].key, key)) { c = &cycs[i]; break; }
	if (!c) { *n = 0; return NULL; }
	int cnt = 0;
	while (c->opts[cnt]) cnt++;
	*n = cnt;
	return c->opts;
}

const char *cyc_title(const char *key)
{
	static const struct { const char *k, *t; } T[] = {
		{ "res", "Resolution" }, { "picMode", "Picture mode" },
		{ "colorTemp", "Colour temperature" }, { "saverType", "Screen saver" },
		{ "saverDelay", "Start the screen saver after" }, { "soundOut", "Sound output" },
		{ "soundMode", "Sound mode" }, { "lipsync", "Lip-sync delay" },
		{ "proxy", "Proxy" }, { "ipMode", "IP settings" }, { "tvName", "Device name" },
		{ "lang", "Language" }, { "tz", "Time zone" }, { "sleep", "Sleep timer" },
		{ "autoOff", "Turn off when idle" }, { "powerOn", "When the TV turns on" },
		{ "dlLoc", "Download location" }, { "onUninstall", "When you uninstall an app" },
		{ "photoSrc", "Screen saver photos" }, { "wall", "Home background" },
	};
	for (unsigned i = 0; i < sizeof(T) / sizeof(*T); i++)
		if (!strcmp(T[i].k, key)) return T[i].t;
	return key;
}

const char *cyc_note(const char *key)
{
	static const struct { const char *k, *n; } N[] = {
		{ "res", "The screen may go dark for a moment while it switches." },
		{ "lipsync", "Raise the delay if voices come before lips move." },
		{ "saverDelay", "Counted from the last button press." },
		{ "tz", "The clock and the TV guide follow this." },
		{ "powerOn", "What you see after turning the TV on." },
		{ "wall", "Shows the photo behind Home and the other screens." },
		{ "onUninstall", "Keeping data means the app picks up where you left off if you install it again." },
	};
	for (unsigned i = 0; i < sizeof(N) / sizeof(*N); i++)
		if (!strcmp(N[i].k, key)) return N[i].n;
	return NULL;
}

/* options that can't be picked right now, with the reason shown in the picker */
const char *opt_dis(const char *key, const char *val)
{
	if (!strcmp(key, "soundOut")) {
		if (!strcmp(val, "Headphones") && !headphones_ok())
			return "Connect Bluetooth headphones first";
		if (!strcmp(val, "Soundbar (HDMI ARC)") && !sv_true("cec"))
			return "Turn on HDMI-CEC in Settings › Inputs first";
	}
	if (!strcmp(key, "dlLoc") && !strcmp(val, "USB drive"))
		return "No USB drive connected";
	if (!strcmp(key, "wall") && !strcmp(val, "Photo of the day") && strcmp(sv("photoSrc"), "Online"))
		return "Needs online photos (Screen saver photos › Online)";
	return NULL;
}

void cycle_key(const char *key, int dir)
{
	(void)dir;
	int n; const char *const *o = cyc_opts(key, &n);
	if (!n) return;
	int i = 0;
	for (int k = 0; k < n; k++)
		if (!strcmp(o[k], sv(key))) { i = k; break; }
	for (int c = 0; c < n; c++) {
		i = (i + 1) % n;
		if (!opt_dis(key, o[i])) break;
	}
	sv_set(key, o[i]);
}

void slide_key(const char *key, int delta, int abs)
{
	int v = abs >= 0 ? abs : svi(key) + delta;
	if (v < 0) v = 0;
	if (v > 100) v = 100;
	sv_seti(key, v);
	g_dirty = true;
}

void toggle_key(const char *key) { sv_seti(key, !svi(key)); }

/* ── home pins (order editable via the options menu) ─────────────────── */
static const char *pins[24] = { "browser", "files", "gallery", "games", "store", "hdmi1", "hdmi2" };
static int npins = 7;

bool pin_has(const char *id)
{
	for (int i = 0; i < npins; i++)
		if (!strcmp(pins[i], id)) return true;
	return false;
}

bool pin_toggle(const char *id)
{
	for (int i = 0; i < npins; i++)
		if (!strcmp(pins[i], id)) {
			memmove(&pins[i], &pins[i + 1], (npins - i - 1) * sizeof(pins[0]));
			npins--;
			return false;
		}
	if (npins < 24)
		pins[npins++] = id;
	return true;
}

void pin_move(const char *id, int dir)
{
	int i = -1;
	for (int k = 0; k < npins; k++)
		if (!strcmp(pins[k], id)) { i = k; break; }
	if (i < 0) return;
	int j = i + dir;
	if (j < 0 || j >= npins) return;
	const char *t = pins[i];
	pins[i] = pins[j];
	pins[j] = t;
}

const char *const *pin_list(int *n) { *n = npins; return pins; }

/* ── wifi / bluetooth mocks ────────────────────────────────────────────── */
static Net nets[] = {
	{ "Home-5G", "WPA3", 4, 5, true }, { "Home-2.4G", "WPA2", 3, 2, true },
	{ "Guest", "Open", 3, 2, false }, { "Neighbour", "WPA2", 2, 5, false },
	{ "Cafe Corner", "WPA2", 1, 2, false },
};
Net *nets_all(int *n) { *n = (int)(sizeof(nets) / sizeof(*nets)); return nets; }

static const char *cur_net = "Home-5G";
const char *net_cur(void) { return sv_true("wifi") && cur_net[0] ? cur_net : NULL; }
const char *online(void) { return net_cur(); }
const char *ipaddr(void)
{
	return !strcmp(sv("ipMode"), "Static") && sv("staticIp")[0] ? sv("staticIp") : "192.168.1.42";
}
const char *sig_word(int sig)
{
	static const char *w[] = { "", "Weak", "Fair", "Good", "Strong" };
	return w[sig < 0 ? 0 : sig > 4 ? 4 : sig];
}
const char *net_desc(int i)
{
	static char b[96];
	Net *n = &nets[i];
	const char *con = online() && !strcmp(n->ssid, cur_net) ? "Connected" :
			  n->saved ? "Saved" : "";
	snprintf(b, sizeof b, "%s%s%s", con, con[0] ? " · " : "",
		 !strcmp(n->sec, "Open") ? "Open network" : n->sec);
	return b;
}
const char *net_sub(int i) { return ""; }

static BtDev btdevs[] = {
	{ "remote", "Open TV Remote", "remote", 80, true },
	{ "hp", "Headphones", "headphones", 62, true },
	{ "", "", "", 0, false }, { "", "", "", 0, false },
	{ "", "", "", 0, false }, { "", "", "", 0, false },
	{ "", "", "", 0, false }, { "", "", "", 0, false },
};
static BtDev btout[8];              /* bt_devs() output (forgotten filtered) */
static char bt_forgotten[8][24];
static int nbt_forg;

static bool bt_forgotten_has(const char *id)
{
	for (int i = 0; i < nbt_forg; i++)
		if (!strcmp(bt_forgotten[i], id)) return true;
	return false;
}

BtDev *bt_devs(int *n)
{
	int k = 0;
	for (unsigned i = 0; i < sizeof(btdevs) / sizeof(*btdevs); i++)
		if (btdevs[i].id[0] && !bt_forgotten_has(btdevs[i].id))
			btout[k++] = btdevs[i];
	*n = k;
	return btout;
}

BtDev *bt_find(const char *id)
{
	for (unsigned i = 0; i < sizeof(btdevs) / sizeof(*btdevs); i++)
		if (btdevs[i].id[0] && !strcmp(btdevs[i].id, id) &&
		    !bt_forgotten_has(id))
			return &btdevs[i];
	return NULL;
}

void bt_forget(const char *id)
{
	if (bt_find(id) && nbt_forg < 8)
		snprintf(bt_forgotten[nbt_forg++], sizeof(*bt_forgotten), "%s", id);
}

static const BtDev btnear[] = {
	{ "pad", "Game controller", "gamepad", 0, false },
	{ "bar", "Soundbar", "speaker", 0, false },
	{ "kbd", "Wireless keyboard", "input", 0, false },
};
bool bt_add_near(const char *id)
{
	for (unsigned i = 0; i < sizeof(btnear) / sizeof(*btnear); i++) {
		if (strcmp(btnear[i].id, id)) continue;
		if (bt_find(id)) return true; /* already paired */
		for (unsigned j = 0; j < sizeof(btdevs) / sizeof(*btdevs); j++) {
			if (btdevs[j].id[0] && !bt_forgotten_has(btdevs[j].id)) continue;
			btdevs[j] = btnear[i];
			btdevs[j].conn = true;
			btdevs[j].batt = !strcmp(btnear[i].kind, "speaker") ? -1 : 60 + app_hash(id) % 40;
			return true;
		}
	}
	return false;
}
const BtDev *bt_near(int *n)
{
	static BtDev out[8];
	int k = 0;
	for (unsigned i = 0; i < sizeof(btnear) / sizeof(*btnear); i++)
		if (!bt_find(btnear[i].id))
			out[k++] = btnear[i];
	*n = k;
	return out;
}

const char *bt_kind_word(const char *kind)
{
	if (!strcmp(kind, "remote")) return "Remote";
	if (!strcmp(kind, "headphones")) return "Headphones";
	if (!strcmp(kind, "gamepad")) return "Game controller";
	if (!strcmp(kind, "speaker")) return "Speaker";
	return "Keyboard";
}
bool headphones_ok(void)
{
	int n; BtDev *d = bt_devs(&n);
	for (int i = 0; i < n; i++)
		if (!strcmp(d[i].kind, "headphones") && d[i].conn) return true;
	return false;
}
const char *audio_fallback(void)
{
	if ((!strcmp(sv("soundOut"), "Headphones") && !headphones_ok()) ||
	    (!strcmp(sv("soundOut"), "Soundbar (HDMI ARC)") && !sv_true("cec"))) {
		sv_set("soundOut", "TV speakers");
		return " · sound is on the TV speakers now";
	}
	return "";
}

/* ── app catalogue ─────────────────────────────────────────────────────── */
int app_hash(const char *s)
{
	unsigned h = 7;
	while (*s) h = h * 31 + (unsigned char)*s++;
	return (int)h;
}

static const App launch[] = {
	{ "video", "Video", "play", "red", "Watch", NULL, NULL, 0, 0, 0, 1 },
	{ "movies", "Movies & Series", "film", "slate", "Watch", NULL, NULL, 0, 0, 0, 0 },
	{ "music", "Music", "music", "violet", "Listen", NULL, NULL, 0, 0, 0, 0 },
	{ "livetv", "Live TV", "tv", "mint", "Watch", "Tuner & IPTV", "tv:guide", 0, 0, 0, 0 },
	{ "browser", "Browser", "globe", "sky", "Tools", NULL, NULL, 0, 0, 0, 0 },
	{ "files", "Files", "folder", "sun", "Tools", NULL, NULL, 0, 0, 0, 0 },
	{ "gallery", "Gallery", "image", "rose", "Watch", NULL, NULL, 0, 0, 0, 0 },
	{ "games", "Games", "gamepad", "blue", "Play", NULL, NULL, 0, 0, 0, 0 },
	{ "podcasts", "Podcasts", "headphones", "violet", "Listen", NULL, NULL, 0, 0, 0, 0 },
	{ "weather", "Weather", "cloud", "sky", "Tools", NULL, NULL, 0, 0, 0, 0 },
	{ "notes", "Notes", "notes", "mint", "Tools", NULL, NULL, 0, 0, 0, 0 },
	{ "store", "Store", "bag", "store", "System", NULL, "store:discover", 0, 0, 0, 0 },
	{ "remoteapp", "Remote", "remote", "slate", "System", NULL, "settings:remotes", 0, 0, 0, 0 },
	{ "speed", "Speed Test", "pulse", "blue", "Tools", NULL, NULL, 0, 0, 0, 0 },
	{ "cast", "Screen Cast", "cast", "rose", "Tools", NULL, NULL, 0, 0, 0, 0 },
	{ "settings", "Settings", "settings", "slate", "System", NULL, "settings:picture", 0, 0, 0, 1 },
	{ "hdmi1", "HDMI 1", "monitor", "slate", "System", NULL, NULL, 1, 1, 0, 0 },
	{ "hdmi2", "HDMI 2", "monitor", "slate", "System", NULL, NULL, 1, 1, 0, 0 },
};

/* store catalogue; `installed` flips at runtime via the appst copy below */
typedef struct { const char *id, *name, *kind, *cat, *glyph, *g, *ver, *next, *lic, *src;
		 const char *anti; int size; bool installed; } Store;
static Store catalog[] = {
	{ "reel", "Reel", "Video player", "Video & Movies", "play", "red", "3.2.1", NULL, "GPL-3.0", "main", NULL, 28, 0 },
	{ "orbit", "Orbit", "Music player", "Music & Audio", "music", "violet", "2.8.0", "2.9.1", "GPL-3.0", "main", NULL, 9, 1 },
	{ "streamline", "Streamline", "IPTV player", "Live TV & Radio", "tv", "mint", "1.14", "1.15", "Apache-2.0", "main", NULL, 25, 1 },
	{ "tuner", "Tuner", "Internet radio", "Live TV & Radio", "music", "mint", "5.0", "5.1", "GPL-3.0", "main", NULL, 12, 1 },
	{ "frame", "Frame", "Photo viewer", "Photos", "image", "rose", "1.6.0", NULL, "MIT", "main", NULL, 14, 0 },
	{ "arcade", "Pixel Arcade", "Retro games", "Games", "gamepad", "blue", "1.9.0", NULL, "GPL-2.0", "main", NULL, 140, 1 },
	{ "shelf", "Shelf", "E-book reader", "News & Reading", "book", "sun", "4.0.2", NULL, "GPL-3.0", "main", NULL, 31, 1 },
	{ "daily", "Daily", "News reader", "News & Reading", "news", "sky", "0.21", NULL, "GPL-3.0", "main", "Non-free network services", 8, 0 },
	{ "podshell", "Podshell", "Podcasts", "Music & Audio", "headphones", "violet", "3.3", NULL, "GPL-3.0", "main", NULL, 19, 0 },
	{ "toolbox", "Toolbox", "File manager", "Tools", "folder", "slate", "2.4", NULL, "Apache-2.0", "main", NULL, 6, 0 },
	{ "fitflow", "Fit Flow", "Home workouts", "Health & Fitness", "heart", "rose", "1.2", NULL, "MIT", "main", NULL, 44, 0 },
	{ "doodle", "Doodle Kids", "Drawing for kids", "Kids", "smile", "sun", "1.0.4", NULL, "GPL-3.0", "comm", NULL, 22, 0 },
	{ "weatherly", "Weatherly", "Forecasts", "Tools", "cloud", "sky", "2.0", NULL, "MIT", "main", "Tracking", 5, 0 },
	{ "castrx", "Cast Receiver", "Screen sharing", "System", "cast", "blue", "0.9", NULL, "LGPL-2.1", "comm", NULL, 11, 0 },
	{ "clipcut", "ClipCut", "Video editor", "Video & Movies", "film", "slate", "1.1", NULL, "GPL-3.0", "main", NULL, 63, 0 },
	{ "vidcast", "Vidcast", "Video podcasts", "Video & Movies", "headphones", "violet", "2.2", NULL, "GPL-3.0", "comm", NULL, 17, 0 },
	{ "lumen", "Lumen", "Smart lights", "Smart Home", "home", "mint", "1.3", NULL, "Apache-2.0", "main", NULL, 9, 0 },
	{ "lexi", "Lexi", "Language lessons", "Learning", "book", "sky", "0.8", NULL, "AGPL-3.0", "comm", NULL, 38, 0 },
	{ "knight", "Knight", "Chess", "Games", "gamepad", "slate", "3.0", NULL, "GPL-3.0", "arch", NULL, 15, 0 },
};
#define NCAT ((int)(sizeof(catalog) / sizeof(*catalog)))

static StoreApp sapps[NCAT]; /* mutable view over catalog for ui.h consumers */
static bool sapps_init;

static void sapps_fill(void)
{
	if (sapps_init) return;
	for (int i = 0; i < NCAT; i++) {
		sapps[i].id = catalog[i].id;
		sapps[i].name = catalog[i].name;
		sapps[i].kind = catalog[i].kind;
		sapps[i].glyph = catalog[i].glyph;
		sapps[i].g = catalog[i].g;
		sapps[i].ver = catalog[i].ver;
		sapps[i].lic = catalog[i].lic;
		sapps[i].next = catalog[i].next;
		sapps[i].anti = catalog[i].anti;
		sapps[i].size = catalog[i].size;
		sapps[i].installed = catalog[i].installed;
	}
	sapps_init = true;
}

const StoreApp *store_app(const char *id)
{
	sapps_fill();
	for (int i = 0; i < NCAT; i++)
		if (!strcmp(catalog[i].id, id)) return &sapps[i];
	return NULL;
}

static const App *all_apps[NCAT + 16]; /* lazily built union of launch + installed store */
static App store_as_app[NCAT];
static int n_all;
static bool all_init;

static void all_fill(void)
{
	if (all_init) return;
	sapps_fill();
	int k = 0;
	for (unsigned i = 0; i < sizeof(launch) / sizeof(*launch); i++)
		all_apps[k++] = &launch[i];
	for (int i = 0; i < NCAT; i++) {
		if (!catalog[i].installed) continue;
		store_as_app[i].id = catalog[i].id;
		store_as_app[i].name = catalog[i].name;
		store_as_app[i].glyph = catalog[i].glyph;
		store_as_app[i].g = catalog[i].g;
		/* STORE2LAUNCH mapping */
		if (!strcmp(catalog[i].cat, "Video & Movies") || !strcmp(catalog[i].cat, "Live TV & Radio") ||
		    !strcmp(catalog[i].cat, "Photos")) store_as_app[i].cat = "Watch";
		else if (!strcmp(catalog[i].cat, "Music & Audio")) store_as_app[i].cat = "Listen";
		else if (!strcmp(catalog[i].cat, "Games") || !strcmp(catalog[i].cat, "Kids")) store_as_app[i].cat = "Play";
		else store_as_app[i].cat = "Tools";
		store_as_app[i].sub = catalog[i].kind;
		store_as_app[i].store = true;
		all_apps[k++] = &store_as_app[i];
	}
	n_all = k;
	all_init = true;
}

static const App *app_find(const char *id)
{
	for (unsigned i = 0; i < sizeof(launch) / sizeof(*launch); i++)
		if (!strcmp(launch[i].id, id)) return &launch[i];
	for (int i = 0; i < NCAT; i++)
		if (!strcmp(catalog[i].id, id)) {
			all_fill();
			for (int j = 0; j < n_all; j++)
				if (all_apps[j]->store && !strcmp(all_apps[j]->id, id))
					return all_apps[j];
		}
	return NULL;
}

const App *app_info(const char *id) { return app_find(id); }

const App *const *app_all(int *n) { all_fill(); *n = n_all; return all_apps; }

const App *const *app_installed_store(int *n)
{
	static const App *list[NCAT];
	all_fill();
	int k = 0;
	for (int i = 0; i < n_all; i++)
		if (all_apps[i]->store) list[k++] = all_apps[i];
	*n = k;
	return list;
}

const App *const *app_builtin(int *n)
{
	static const App *list[16];
	int k = 0;
	for (unsigned i = 0; i < sizeof(launch) / sizeof(*launch); i++)
		if (!launch[i].input) list[k++] = &launch[i];
	*n = k;
	return list;
}

const char *app_go(const char *id)
{
	const App *a = app_find(id);
	return a ? a->go : NULL;
}

const char *app_src_name(const char *src)
{
	if (!src || !strcmp(src, "main")) return "Open TV Main";
	if (!strcmp(src, "comm")) return "Community";
	if (!strcmp(src, "arch")) return "Archive";
	return "Added by you";
}

/* ── permissions ──────────────────────────────────────────────────────── */
static const char *const perms_video[] = { "storage", NULL };
static const char *const perms_browser[] = { "location", "mic", "camera", NULL };
static const char *const perms_storage1[] = { "storage", NULL };
static const char *const perms_loc[] = { "location", NULL };
static const char *const perms_mic[] = { "mic", NULL };

const char *const *app_perms(const char *id, int *n)
{
	const char *const *p = NULL;
	if (!strcmp(id, "browser")) p = perms_browser;
	else if (!strcmp(id, "weather") || !strcmp(id, "weatherly") || !strcmp(id, "lumen") ||
		 !strcmp(id, "speed")) p = perms_loc;
	else if (!strcmp(id, "notes") || !strcmp(id, "lexi")) p = perms_mic;
	else if (!strcmp(id, "fitflow")) p = perms_storage1;
	else p = perms_video;
	int c = 0;
	while (p[c]) c++;
	*n = c;
	return p;
}

static const struct { const char *p, *name, *icon; } PERMS[] = {
	{ "mic", "Microphone", "mic" }, { "location", "Location", "globe" },
	{ "storage", "Files and media", "folder" }, { "camera", "Camera", "image" },
};

bool perm_info(const char *perm, const char **name, const char **icon)
{
	for (unsigned i = 0; i < sizeof(PERMS) / sizeof(*PERMS); i++)
		if (!strcmp(PERMS[i].p, perm)) {
			if (name) *name = PERMS[i].name;
			if (icon) *icon = PERMS[i].icon;
			return true;
		}
	return false;
}

/* ── per-app state (appSt) ─────────────────────────────────────────────── */
typedef struct {
	char id[24];
	int notif, stopped, data, cache;
	int pmic, plocation, pstorage, pcamera;
	bool seen;
} AppStRec;
static AppStRec appst[64];
static int nappst;

static AppStRec *appst_rec(const char *id, bool create)
{
	for (int i = 0; i < nappst; i++)
		if (!strcmp(appst[i].id, id)) return &appst[i];
	if (!create) return NULL;
	if (nappst >= 64) return NULL;
	AppStRec *r = &appst[nappst++];
	memset(r, 0, sizeof(*r));
	snprintf(r->id, sizeof(r->id), "%s", id);
	r->notif = 1;
	r->data = 5 + app_hash(id) % 180;
	r->cache = 2 + app_hash(id) % 90;
	/* default permissions on */
	int np; const char *const *pl = app_perms(id, &np);
	for (int i = 0; i < np; i++) {
		if (!strcmp(pl[i], "mic")) r->pmic = 1;
		else if (!strcmp(pl[i], "location")) r->plocation = 1;
		else if (!strcmp(pl[i], "storage")) r->pstorage = 1;
		else if (!strcmp(pl[i], "camera")) r->pcamera = 1;
	}
	return r;
}

void appst_ensure(const char *id) { appst_rec(id, true); }

bool appst_get(const char *id, const char *field, int *val)
{
	AppStRec *r = appst_rec(id, true);
	if (!r) return false;
	if (!strcmp(field, "notif")) *val = r->notif;
	else if (!strcmp(field, "stopped")) *val = r->stopped;
	else if (!strcmp(field, "data")) *val = r->data;
	else if (!strcmp(field, "cache")) *val = r->cache;
	else return false;
	return true;
}

void appst_set(const char *id, const char *field, int val)
{
	AppStRec *r = appst_rec(id, true);
	if (!r) return;
	if (!strcmp(field, "notif")) r->notif = val;
	else if (!strcmp(field, "stopped")) r->stopped = val;
	else if (!strcmp(field, "data")) r->data = val;
	else if (!strcmp(field, "cache")) r->cache = val;
}

static int *perm_ptr(AppStRec *r, const char *perm)
{
	if (!strcmp(perm, "mic")) return &r->pmic;
	if (!strcmp(perm, "location")) return &r->plocation;
	if (!strcmp(perm, "storage")) return &r->pstorage;
	if (!strcmp(perm, "camera")) return &r->pcamera;
	return NULL;
}

const char *appst_perm(const char *id, const char *perm, int *has)
{
	AppStRec *r = appst_rec(id, true);
	int *p = r ? perm_ptr(r, perm) : NULL;
	if (has) *has = p ? *p : 0;
	const char *name = perm, *icon;
	if (perm_info(perm, &name, &icon)) return name;
	return perm;
}

void appst_toggle_perm(const char *id, const char *perm)
{
	AppStRec *r = appst_rec(id, true);
	int *p = r ? perm_ptr(r, perm) : NULL;
	if (p) *p = !*p;
}

/* ── storage ──────────────────────────────────────────────────────────── */
Stor stor_stats(void)
{
	Stor s = { 0 };
	s.system = 8.4f;
	s.total = 32;
	sapps_fill();
	int nst; const App *const *st = app_installed_store(&nst);
	for (int i = 0; i < nst; i++) s.apps += st[i]->store ? 0 : 0;
	float store_sum = 0;
	for (int i = 0; i < NCAT; i++)
		if (catalog[i].installed) store_sum += catalog[i].size;
	s.apps = store_sum / 1000 + 3.6f;
	int na; const App *const *sa = app_builtin(&na);
	int nst2; const App *const *st2 = app_installed_store(&nst2);
	(void)st; (void)nst;
	float data = 0, cache = 0;
	for (int i = 0; i < na; i++) {
		int v; appst_get(sa[i]->id, "data", &v); data += v;
		appst_get(sa[i]->id, "cache", &v); cache += v;
	}
	for (int i = 0; i < nst2; i++) {
		int v; appst_get(st2[i]->id, "data", &v); data += v;
		appst_get(st2[i]->id, "cache", &v); cache += v;
	}
	s.data = data / 1000;
	s.cache = svi("cacheMB") / 1000.0f + cache / 1000;
	s.rec = 0;
	s.used = s.system + s.apps + s.data + s.cache + s.rec;
	return s;
}

const char *gb(float v)
{
	static char b[24];
	if (v < 1) snprintf(b, sizeof b, "%d MB", (int)(v * 1000 + 0.5f));
	else snprintf(b, sizeof b, "%.1f GB", v);
	return b;
}

/* ── time helpers ─────────────────────────────────────────────────────── */
static int tz_off(void)
{
	const char *cur = sv("tz");
	if (strcmp(cur, "Automatic")) {
		/* offsets from the prototype TZ table */
		static const struct { const char *n; int off; } Z[] = {
			{ "London", 0 }, { "Lisbon", 0 }, { "Paris", 60 }, { "Berlin", 60 },
			{ "Bucharest", 120 }, { "Athens", 120 }, { "Istanbul", 180 }, { "Dubai", 240 },
			{ "New Delhi", 330 }, { "Singapore", 480 }, { "Tokyo", 540 }, { "Sydney", 600 },
			{ "New York", -300 }, { "Chicago", -360 }, { "Los Angeles", -480 },
		};
		for (unsigned i = 0; i < sizeof(Z) / sizeof(*Z); i++)
			if (!strncmp(cur, Z[i].n, strlen(Z[i].n))) return Z[i].off;
	}
	return 0;
}

const char *clock_str(void)
{
	static char b[16];
	int h, m, s, wd, day, mon, year;
	gw_time_parts(&h, &m, &s, &wd, &day, &mon, &year);
	h += (tz_off() + (sv_true("autoTime") ? 0 : svi("timeAdj"))) / 60;
	m += (tz_off() + (sv_true("autoTime") ? 0 : svi("timeAdj"))) % 60;
	if (m < 0) { m += 60; h--; }
	h = (h % 24 + 24) % 24;
	if (sv_true("clock24")) snprintf(b, sizeof b, "%02d:%02d", h, m);
	else snprintf(b, sizeof b, "%d:%02d %s", h % 12 ? h % 12 : 12, m, h < 12 ? "AM" : "PM");
	return b;
}

const char *date_str(bool short_)
{
	static char b[64];
	int h, m, s, wd, day, mon, year;
	gw_time_parts(&h, &m, &s, &wd, &day, &mon, &year);
	(void)(tz_off() + 0); /* date stays local; fine for the mock */
	if (short_) snprintf(b, sizeof b, "%s %d %s", gw_wd_name(wd) + 0, day, gw_mon_name(mon));
	else snprintf(b, sizeof b, "%s, %d %s", gw_wd_name(wd), day, gw_mon_name(mon));
	/* shorten weekday for short_ */
	if (short_) {
		static const char *const wds[] = { "Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat" };
		snprintf(b, sizeof b, "%s %d %s", wds[wd % 7], day, gw_mon_name(mon));
	}
	return b;
}
const char *date_short(void) { return date_str(true); }

const char *greeting(void)
{
	int h, m, s, wd, day, mon, year;
	gw_time_parts(&h, &m, &s, &wd, &day, &mon, &year);
	h += (tz_off() + (sv_true("autoTime") ? 0 : svi("timeAdj"))) / 60;
	h = (h % 24 + 24) % 24;
	return h < 5 ? "Good night" : h < 12 ? "Good morning" : h < 18 ? "Good afternoon" : "Good evening";
}

void time_shift(int add_min) { sv_seti("timeAdj", svi("timeAdj") + add_min); }

/* ── mock busy machines (sys_tick) ────────────────────────────────────── */
static float t_wifi_scan = -1, t_wifi_busy = -1, t_bt_scan = -1, t_bt_pair = -1,
	     t_bt_busy = -1, t_upd = -1, t_boot = -1;
static char busy_ssid[48], busy_bt[24], pair_id[24];
static char wifi_prev[48];
static int speed_phase; /* 0 none 1 ping 2 down 3 up */
static float speed_t0, speed_last;
static int speed_i;
static struct { char id[24]; float t0; int prog; } installs[8];
static int ninst;
static bool offwarn_open;
static float idle_anchor;   /* last button press; reset by sys_note_input */

void wifi_scan_start(void)
{
	if (sv_true("wifiScan") || !sv_true("wifi")) return;
	sv_seti("wifiScan", 1);
	scene_request_redraw();
	t_wifi_scan = scene_now();
}
void wifi_connect_start(const char *ssid)
{
	Net *nn = NULL; int n = 0; nets_all(&n);
	for (int i = 0; i < n; i++)
		if (!strcmp(nets[i].ssid, ssid)) nn = &nets[i];
	if (!nn) return;
	snprintf(wifi_prev, sizeof wifi_prev, "%s", cur_net);
	snprintf(busy_ssid, sizeof busy_ssid, "%s", ssid);
	sv_set("wifiBusy", ssid);
	t_wifi_busy = scene_now();
	scene_request_redraw();
}

void wifi_forget(const char *ssid)
{
	int n = 0; nets_all(&n);
	for (int i = 0; i < n; i++)
		if (!strcmp(nets[i].ssid, ssid)) nets[i].saved = false;
	if (cur_net[0] && !strcmp(cur_net, ssid)) {
		cur_net = "";
		sv_set("wifiNet", "");
	}
	scene_request_redraw();
}
void bt_scan_start(void)
{
	if (sv_true("btScan") || !sv_true("bt")) return;
	sv_seti("btScan", 1);
	scene_request_redraw();
	t_bt_scan = scene_now();
}
void bt_pair_start(const char *id)
{
	if (sv("btPairing")[0]) return;
	sv_set("btPairing", id);
	snprintf(pair_id, sizeof pair_id, "%s", id);
	scene_request_redraw();
	t_bt_pair = scene_now();
}
void bt_conn_start(const char *id, bool on)
{
	BtDev *d = bt_find(id);
	if (!d) return;
	sv_set("btBusy", id);
	snprintf(busy_bt, sizeof busy_bt, "%s", id);
	d->conn = on; /* applied after the fake delay by flipping back if needed */
	d->conn = !on;
	t_bt_busy = scene_now();
	scene_request_redraw();
	(void)on;
}
void sys_update_start(void)
{
	if (sv_true("updBusy")) return;
	sv_seti("updBusy", 1);
	scene_request_redraw();
	t_upd = scene_now();
}

bool sys_busy(void) /* something the screen saver should not interrupt */
{
	return t_wifi_busy > 0 || t_bt_busy > 0 || t_bt_pair > 0 || t_wifi_scan > 0 ||
	       t_bt_scan > 0 || t_upd > 0 || speed_phase || ninst > 0;
}

static void sys_settle_wifi(void)
{
	int n = 0; nets_all(&n);
	for (int i = 0; i < n; i++)
		if (!strcmp(nets[i].ssid, busy_ssid)) { nets[i].saved = true; break; }
	snprintf(wifi_prev, sizeof wifi_prev, "%s", busy_ssid);
	cur_net = wifi_prev; /* connect success: now the current network */
	(void)wifi_prev;
	cur_net = busy_ssid[0] ? busy_ssid : cur_net;
	sv_set("wifiNet", cur_net);
	sv_set("wifiBusy", "");
	char conn_msg[80];
	snprintf(conn_msg, sizeof conn_msg, "Connected to %s", cur_net);
	busy_ssid[0] = 0;
	t_wifi_busy = -1;
	toast(conn_msg, "wifi");
	scene_request_redraw();
	if (!sub_active()) scene_refresh();
}

static void sys_settle_bt(void)
{
	BtDev *d = bt_find(busy_bt);
	if (d) {
		d->conn = !d->conn;
		const char *fb = audio_fallback();
		char m[160];
		snprintf(m, sizeof m, "%s %s%s", d->name, d->conn ? "connected" : "disconnected", fb);
		toast(m, "bluetooth");
	}
	sv_set("btBusy", "");
	busy_bt[0] = 0;
	t_bt_busy = -1;
	scene_request_redraw();
	if (!sub_active()) scene_refresh();
}

static void sys_tick_speed(float now)
{
	if (!speed_phase) return;
	if (now - speed_last < 0.14f) return;
	speed_last = now;
	if (speed_phase == 1 && now - speed_t0 > 0.7f) {
		sv_seti("speedPing", 8 + app_hash("p") % 13);
		speed_phase = 2; speed_i = 0;
		sv_set("speedDown", "…");
	} else if (speed_phase == 2) {
		speed_i++;
		int down = 280 + app_hash("d") % 61;
		int cur = down * speed_i / 12;
		static char b[16];
		snprintf(b, sizeof b, "%d", speed_i >= 12 ? down : cur);
		sv_set("speedDown", b);
		if (speed_i >= 12) { speed_phase = 3; speed_i = 0; sv_set("speedUp", "…"); }
	} else if (speed_phase == 3) {
		speed_i++;
		int up = 36 + app_hash("u") % 11;
		static char b[16];
		snprintf(b, sizeof b, "%d", speed_i >= 9 ? up : up * speed_i / 9);
		sv_set("speedUp", b);
		if (speed_i >= 9) {
			speed_phase = 0;
			char m[96];
			snprintf(m, sizeof m, "Download %s Mbit/s · Upload %s Mbit/s", sv("speedDown"), sv("speedUp"));
			toast(m, "pulse");
			scene_refresh();
		}
	}
	g_dirty = true;
	if (sub_active() || scene_focused()) scene_refresh();
}

void speed_start(void)
{
	if (speed_phase || !online()) return;
	sv_set("speedPing", ""); sv_set("speedDown", ""); sv_set("speedUp", "");
	speed_phase = 1; speed_t0 = speed_last = scene_now(); speed_i = 0;
	sv_seti("speedPing", 0);
	scene_request_redraw();
}

static void sys_tick_installs(float now)
{
	static float last;
	if (!ninst) { last = 0; return; }
	if (!last) last = now;
	if (now - last < 0.09f) return;
	last = now;
	for (int i = 0; i < ninst; i++) {
		installs[i].prog += 2 + app_hash(sv("lang")) % 5;
		if (installs[i].prog >= 100) {
			const StoreApp *a = store_app(installs[i].id);
			bool was_upd = false;
			/* upgrade */
			for (int c = 0; c < NCAT; c++) {
				if (strcmp(catalog[c].id, installs[i].id)) continue;
				was_upd = catalog[c].installed && catalog[c].next;
				if (was_upd) { catalog[c].ver = catalog[c].next; catalog[c].next = NULL; }
				catalog[c].installed = true;
				sapps[c].installed = true;
				sapps[c].ver = catalog[c].ver;
				sapps[c].next = NULL;
				break;
			}
			char m[96];
			snprintf(m, sizeof m, "%s %s", a ? a->name : installs[i].id, was_upd ? "updated" : "installed");
			toast(m, "check");
			installs[i] = installs[--ninst];
			i--;
			scene_refresh();
		}
	}
	g_dirty = true;
}

bool install_progress(const char *id, int *pct)
{
	for (int i = 0; i < ninst; i++)
		if (!strcmp(installs[i].id, id)) { *pct = installs[i].prog; return true; }
	return false;
}
void install_start(const char *id)
{
	if (!online()) { toast("You're offline. Connect to a network to install apps", "wifioff"); return; }
	if (install_progress(id, &(int){ 0 })) return;
	if (ninst < 8) {
		snprintf(installs[ninst].id, sizeof installs[ninst].id, "%s", id);
		installs[ninst].t0 = scene_now();
		installs[ninst].prog = 0;
		ninst++;
	}
	scene_refresh();
}

/* uninstall a store app: flip the catalogue, drop its state when asked */
void store_uninstall(const char *id)
{
	for (int i = 0; i < NCAT; i++) {
		if (strcmp(catalog[i].id, id)) continue;
		catalog[i].installed = false;
		catalog[i].next = NULL;
		sapps[i].installed = false;
		sapps[i].next = NULL;
		break;
	}
	all_init = false;
	if (!strcmp(sv("onUninstall"), "Remove data")) {
		for (int i = 0; i < nappst; i++)
			if (!strcmp(appst[i].id, id)) {
				appst[i] = appst[--nappst];
				break;
			}
	}
	g_dirty = true;
}

/* sleep timer + turn off when idle, with a one-minute warning dialog */
static void idle_checks(float now)
{
	if (speed_phase || ninst) return; /* don't fight other machines */
	float at = 0; bool have = false;
	if (svi("sleepAt")) { at = svi("sleepAt"); have = true; }
	float nows = now;
	float lim = 0;
	const char *ao = sv("autoOff");
	if (!strcmp(ao, "1 hour")) lim = 3600;
	else if (!strcmp(ao, "4 hours")) lim = 4 * 3600;
	else if (!strcmp(ao, "8 hours")) lim = 8 * 3600;
	if (!idle_anchor) idle_anchor = nows;
	/* idle deadline measured from the last button press */
	float idle_at = idle_anchor + lim;
	if (lim && (!have || idle_at < at)) { at = idle_at; have = true; }
	if (!have) { offwarn_open = false; return; }
	float left = at - nows;
	static float warned;
	if (left <= 60 && (!warned || !offwarn_open)) {
		warned = nows;
		offwarn_open = true;
		static const DlgBtn b[] = {
			{ "Keep watching", "play", NULL, false },
			{ "Turn off now", "power", NULL, false },
		};
		dialog_open("Still watching?", "The TV will turn off in a minute.",
			    b, 2, 0, NULL);
	}
	if (left <= 0) {
		/* milestone build: stand down instead of a hard power-off */
		sv_seti("sleepAt", 0);
		sv_set("sleep", "Off");
		offwarn_open = false;
		toast("Sleep timer reached · standby is next", "moon");
	}
}

static void sys_settle_pair(void)
{
	bt_add_near(pair_id);
	const BtDev *d = bt_find(pair_id);
	char m[96];
	snprintf(m, sizeof m, "%s paired", d ? d->name : "Device");
	sv_set("btPairing", "");
	pair_id[0] = 0;
	t_bt_pair = -1;
	toast(m, "bluetooth");
	scene_request_redraw();
	if (!sub_active()) scene_refresh();
}

void sys_tick(float now)
{
	static float prev = -1;
	float dt = prev < 0 ? 0 : now - prev;
	prev = now;
	if (t_wifi_scan > 0 && now - t_wifi_scan > 1.5f) {
		t_wifi_scan = -1;
		sv_seti("wifiScan", 0);
		sv_seti("wifiSeen", 1);
		/* wander the mock signal strengths like the prototype scan */
		int n = 0; nets_all(&n);
		for (int i = 0; i < n; i++) {
			nets[i].sig += (app_hash(sv("lang") + i) + i) % 3 - 1;
			if (nets[i].sig < 1) nets[i].sig = 1;
			if (nets[i].sig > 4) nets[i].sig = 4;
		}
		scene_refresh();
	}
	if (t_wifi_busy > 0 && now - t_wifi_busy > 1.8f) sys_settle_wifi();
	if (t_bt_scan > 0 && now - t_bt_scan > 2.2f) {
		t_bt_scan = -1;
		sv_seti("btScan", 0);
		sv_seti("btSeen", 1);
		scene_refresh();
	}
	if (t_bt_pair > 0 && now - t_bt_pair > 1.6f) sys_settle_pair();
	if (t_bt_busy > 0 && now - t_bt_busy > 0.9f) sys_settle_bt();
	if (t_upd > 0 && now - t_upd > 1.8f) {
		t_upd = -1;
		sv_seti("updBusy", 0);
		sv_seti("updAt", 1);
		toast("Open TV is up to date", "check");
		scene_refresh();
	}
	if (t_boot > 0 && now - t_boot > 1.8f) {
		t_boot = -1;
		scene_home();
	}
	sys_tick_speed(now);
	sys_tick_installs(now);
	idle_checks(now);
	(void)dt;
}

void sys_reboot(void)
{
	sv_seti("sleepAt", 0);
	lpop_all();
	t_boot = scene_now();
	scene_go("boot", "");
}

/* ── factory reset: restore the pristine mock state ───────────────────── */
static SStr sstr0[sizeof(sstr) / sizeof(*sstr)];
static SInt sint0[sizeof(sint) / sizeof(*sint)];
static BtDev btdevs0[8];
static Net nets0[8];
static struct { const char *ver, *next; bool installed; } cat0[32];
static bool state_saved;

static void state_save(void)
{
	if (state_saved) return;
	memcpy(sstr0, sstr, sizeof sstr);
	memcpy(sint0, sint, sizeof sint);
	memcpy(btdevs0, btdevs, sizeof btdevs);
	memcpy(nets0, nets, sizeof nets);
	for (int i = 0; i < NCAT; i++) {
		cat0[i].ver = catalog[i].ver;
		cat0[i].next = catalog[i].next;
		cat0[i].installed = catalog[i].installed;
	}
	state_saved = true;
}

void sys_factory_reset(void)
{
	state_save();
	memcpy(sstr, sstr0, sizeof sstr);
	memcpy(sint, sint0, sizeof sint);
	memcpy(btdevs, btdevs0, sizeof btdevs);
	memcpy(nets, nets0, sizeof nets);
	for (int i = 0; i < NCAT; i++) {
		catalog[i].ver = cat0[i].ver;
		catalog[i].next = cat0[i].next;
		catalog[i].installed = cat0[i].installed;
	}
	sapps_init = false;
	all_init = false;
	nappst = 0;
	nbt_forg = 0;
	cur_net = "Home-5G";
	static const char *const pins0[] = { "browser", "files", "gallery", "games", "store", "hdmi1", "hdmi2" };
	npins = 0;
	for (unsigned i = 0; i < sizeof(pins0) / sizeof(*pins0); i++)
		pins[npins++] = pins0[i];
	speed_phase = 0;
	sv_set("speedPing", ""); sv_set("speedDown", ""); sv_set("speedUp", "");
	ninst = 0;
	t_wifi_scan = t_wifi_busy = t_bt_scan = t_bt_pair = t_bt_busy = t_upd = t_boot = -1;
	busy_ssid[0] = 0; busy_bt[0] = 0; pair_id[0] = 0;
	offwarn_open = false;
	idle_anchor = 0;
	g_dirty = true;
}

void sys_init(void)
{
	/* snapshot the pristine state so factory reset can restore it */
	state_save();
}

/* ── idle clock (screen saver / auto-off anchor) ──────────────────────── */
void sys_note_input(void) { idle_anchor = scene_now(); }

/* ── sleep timer target as a wall-clock string ("Off at 22:30") ───────── */
const char *sleep_target_str(void)
{
	static char b[16];
	int at = svi("sleepAt");
	if (!at) return "Off";
	int delta = (at - (int)scene_now()) / 60;
	if (delta < 0) delta = 0;
	int h = 0, mnt = 0;
	if (sscanf(clock_str(), "%d:%d", &h, &mnt) == 2) {
		mnt += delta;
		h = (h + mnt / 60) % 24;
		if (h < 0) h += 24;
		mnt = (mnt % 60 + 60) % 60;
		if (sv_true("clock24")) snprintf(b, sizeof b, "%02d:%02d", h, mnt);
		else snprintf(b, sizeof b, "%d:%02d %s", h % 12 ? h % 12 : 12, mnt,
			      h < 12 ? "AM" : "PM");
		return b;
	}
	return clock_str();
}

/* ── CHANGE_TOAST subset: toast for a state change ────────────────────── */
void toast_change(const char *key)
{
	const char *m = NULL, *ic = "check";
	static char b[128];
	if (!strcmp(key, "sleep")) {
		if (!strcmp(sv("sleep"), "Off")) { m = "Sleep timer off"; ic = "moon"; }
		else { snprintf(b, sizeof b, "The TV will turn off at %s", sleep_target_str()); m = b; ic = "moon"; }
	} else if (!strcmp(key, "wifi")) {
		if (sv_true("wifi")) {
			snprintf(b, sizeof b, "Wi-Fi on%s", sv("wifiNet")[0] ? "" : "");
			m = b; ic = "wifi";
		} else { m = "Wi-Fi off"; ic = "wifioff"; }
	} else if (!strcmp(key, "bt")) {
		m = sv_true("bt") ? "Bluetooth on" : "Bluetooth off"; ic = "bluetooth";
	} else if (!strcmp(key, "picMode")) {
		snprintf(b, sizeof b, "Picture: %s", sv("picMode")); m = b; ic = "display";
	} else if (!strcmp(key, "soundOut")) {
		snprintf(b, sizeof b, "Sound: %s", sv("soundOut")); m = b; ic = "speaker";
	} else if (!strcmp(key, "soundMode")) {
		snprintf(b, sizeof b, "Sound mode: %s", sv("soundMode")); m = b; ic = "speaker";
	} else if (!strcmp(key, "saverType")) {
		if (!strcmp(sv("saverType"), "Off")) { m = "Screen saver off"; ic = "moon"; }
		else { snprintf(b, sizeof b, "Screen saver: %s", sv("saverType")); m = b; ic = "moon"; }
	} else if (!strcmp(key, "tz")) {
		snprintf(b, sizeof b, "Time zone: %s", sv("tz")); m = b; ic = "clock";
	} else if (!strcmp(key, "lang")) {
		snprintf(b, sizeof b, "Language set to %s. Translations aren't in this build yet", sv("lang"));
		m = b; ic = "globe";
	} else if (!strcmp(key, "diag")) {
		m = sv_true("diag") ? "Usage & diagnostics on" : "Usage & diagnostics off"; ic = "shield";
	} else if (!strcmp(key, "autoOff")) {
		if (!strcmp(sv("autoOff"), "Never")) m = "The TV stays on when idle";
		else { snprintf(b, sizeof b, "The TV turns off after %s without a button press", sv("autoOff")); m = b; }
		ic = "power";
	}
	if (m) toast(m, ic);
}

/* ── video library: real clips from the device's storage ──────────────── */
/* The device description names the storage mounts (device.conf). The Video
 * app lists every .mp4 found there; a "<name>.dur" sidecar carries the
 * duration (our sample generator writes it), and a "<name>.jpg" filmstrip
 * carries pre-rendered preview frames for the player (QEMU has no video
 * decode yet — the real media backend replaces this later). Network shares
 * are the prototype's mock while network=mock. */
static VidClip vids[24];
static int nvids;
static bool vids_init;

static void vid_scan_dir(const char *dir, const char *folder)
{
	DIR *d = opendir(dir);
	if (!d)
		return;
	struct dirent *de;
	while ((de = readdir(d)) && nvids < (int)(sizeof vids / sizeof *vids)) {
		const char *n = de->d_name;
		size_t ln = strlen(n);
		if (ln < 5 || strcasecmp(n + ln - 4, ".mp4") != 0)
			continue;
		VidClip *v = &vids[nvids];
		memset(v, 0, sizeof *v);
		snprintf(v->id, sizeof v->id, "c%d", nvids);
		snprintf(v->path, sizeof v->path, "%s/%s", dir, n);
		snprintf(v->folder, sizeof v->folder, "%s", folder);

		/* title: filename without the extension, separators as spaces */
		char base[96];
		snprintf(base, sizeof base, "%.*s", (int)(ln - 4), n);
		for (char *p = base; *p; p++)
			if (*p == '-' || *p == '_' || *p == '.')
				*p = ' ';
		snprintf(v->title, sizeof v->title, "%s", base);

		struct stat st;
		if (stat(v->path, &st) != 0)
			continue;
		v->size = st.st_size;
		v->order = st.st_mtime;
		struct tm tmv;
		localtime_r(&st.st_mtime, &tmv);
		strftime(v->date, sizeof v->date, "%-d %b %Y", &tmv);

		/* duration: probe the container (media.c), .dur sidecar
		 * as a fallback for probe failures */
		double d = media_probe_duration(v->path);
		if (d > 0) {
			v->dur = (int)d;
		} else {
			char durp[288];
			snprintf(durp, sizeof durp, "%.*s.dur",
				 (int)(strlen(v->path) - 4), v->path);
			FILE *f = fopen(durp, "r");
			if (f) {
				int dd = 0;
				if (fscanf(f, "%d", &dd) == 1 && dd > 0)
					v->dur = dd;
				fclose(f);
			}
		}
		if (!v->dur)
			v->dur = 30;
		nvids++;
	}
	closedir(d);
}

VidClip *vid_clips(int *n)
{
	if (!vids_init) {
		vids_init = true;
		nvids = 0;
		vid_scan_dir(otv_device_str("storage", "internal", "/media/internal"),
			     "Internal storage");
		vid_scan_dir(otv_device_str("storage", "external", "/media/external"),
			     "USB drive");
	}
	*n = nvids;
	return vids;
}

void vid_rescan(void)
{
	/* keep playback state (position, watched) for files that still exist */
	struct { char path[256]; int pos, dur; bool watched; } keep[24];
	int nk = nvids < 24 ? nvids : 24;
	for (int i = 0; i < nk; i++) {
		snprintf(keep[i].path, sizeof keep[i].path, "%s", vids[i].path);
		keep[i].pos = vids[i].pos;
		keep[i].dur = vids[i].dur;
		keep[i].watched = vids[i].watched;
	}
	vids_init = false;
	nvids = 0;
	int n;
	(void)vid_clips(&n); /* rescan */
	for (int i = 0; i < nvids; i++)
		for (int k = 0; k < nk; k++)
			if (!strcmp(vids[i].path, keep[k].path)) {
				vids[i].pos = keep[k].pos;
				vids[i].watched = keep[k].watched;
				break;
			}
}

/* ── network shares (prototype mock; real SMB/DLNA with the media backend) */
typedef struct { const char *file, *scene; int dur; } VFile;
typedef struct { const char *name; const VFile *files; int nf; } VDirDef;
typedef struct {
	VShare share;
	const VDirDef *dirs;
	int ndirs;
	VidClip *items;      /* materialized lazily, pos/watched stateful */
	int nitems;
} VShareDef;

static const VFile f_films[] = {
	{ "Sunday Matinee.mkv", "cinema", 6100 },
	{ "Coral Garden.mkv", "nature", 3000 },
	{ "Street Band Live.mp4", "music", 3900 },
};
static const VFile f_home[] = {
	{ "Holiday 2025.mp4", "nature", 1260 },
	{ "Wedding speech.mp4", "kids", 540 },
};
static const VFile f_vids[] = {
	{ "Drawing tutorial.mp4", "kids", 840 },
	{ "Garden drone flight.mp4", "nature", 300 },
};
static const VDirDef d_nas[] = {
	{ "Films", f_films, 3 }, { "Home videos", f_home, 2 },
};
static const VDirDef d_pc[] = {
	{ "Videos", f_vids, 2 },
};
static VShareDef shares[] = {
	{ { "nas", "Home NAS", "SMB", "smb://192.168.1.20/media" }, d_nas, 2, NULL, 0 },
	{ { "pc", "Living room PC", "DLNA", "Media server on 192.168.1.31" }, d_pc, 1, NULL, 0 },
};

int vid_shares_count(void)
{
	return (int)(sizeof shares / sizeof *shares);
}
const VShare *vid_share(int i)
{
	return i >= 0 && i < vid_shares_count() ? &shares[i].share : NULL;
}
int vid_share_dirs(int i)
{
	return i >= 0 && i < vid_shares_count() ? shares[i].ndirs : 0;
}
const char *vid_share_dir_name(int i, int d)
{
	return (i >= 0 && i < vid_shares_count() && d >= 0 &&
		d < shares[i].ndirs) ? shares[i].dirs[d].name : "";
}

VidClip *vid_share_clips(int i, int d, int *n)
{
	if (i < 0 || i >= vid_shares_count() || d < 0 || d >= shares[i].ndirs) {
		*n = 0;
		return NULL;
	}
	VShareDef *sh = &shares[i];
	const VDirDef *dir = &sh->dirs[d];
	if (!sh->items) {
		/* materialize every directory once, keep positions across views */
		int total = 0;
		for (int k = 0; k < sh->ndirs; k++)
			total += sh->dirs[k].nf;
		sh->items = calloc(total ? total : 1, sizeof(VidClip));
		sh->nitems = 0;
		for (int k = 0; k < sh->ndirs; k++)
			for (int f = 0; f < sh->dirs[k].nf; f++) {
				const VFile *vf = &sh->dirs[k].files[f];
				VidClip *v = &sh->items[sh->nitems++];
				bool mkv = vf->file && strstr(vf->file, ".mkv");
				snprintf(v->id, sizeof v->id, "n-%s-%d-%d",
					 sh->share.id, k, f);
				snprintf(v->path, sizeof v->path, "%s/%s/%s",
					 sh->share.addr, sh->dirs[k].name, vf->file);
				snprintf(v->title, sizeof v->title, "%.*s",
					 (int)(strlen(vf->file) - 4), vf->file);
				snprintf(v->folder, sizeof v->folder, "%s",
					 sh->dirs[k].name);
				snprintf(v->net, sizeof v->net, "%s", sh->share.name);
				snprintf(v->res, sizeof v->res, "%s",
					 mkv ? "3840 × 2160" : "1920 × 1080");
				snprintf(v->codec, sizeof v->codec, "%s",
					 mkv ? "H.265" : "H.264");
				v->fps = 24;
				v->dur = vf->dur;
				v->size = (long)vf->dur * (mkv ? 9 : 5) * 1000 * 1000;
				v->is_share = true;
			}
	}
	*n = dir->nf;
	return sh->items;
}

VidClip *vid_find(const char *id)
{
	int n;
	(void)vid_clips(&n);
	for (int i = 0; i < n; i++)
		if (!strcmp(vids[i].id, id))
			return &vids[i];
	for (int s = 0; s < vid_shares_count(); s++)
		for (int d = 0; d < vid_share_dirs(s); d++) {
			int m;
			VidClip *l = vid_share_clips(s, d, &m);
			for (int i = 0; i < m; i++)
				if (!strcmp(l[i].id, id))
					return &l[i];
		}
	return NULL;
}

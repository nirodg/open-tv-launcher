/* launcher/screen_settings.c — SETTINGS (port of SCREENS.settings + spage/SUB).
 * Sidebar with nine sections; the main pane is a scrolling stack of glass
 * group cards built from SRow tables. Sub-pages reuse the same row builder.
 * All geometry mirrors the prototype CSS (side 560×888 at 96,96; smain
 * left 696 right 96 with 40px top padding). */
#define _GNU_SOURCE
#include "drawui.h"
#include <stdio.h>
#include <stdarg.h>
#include <ctype.h>

/* ── sidebar pages ────────────────────────────────────────────────────── */
typedef struct { const char *id, *name, *icon; Col badge; } SPage;
static const SPage SPAGES[] = {
	{ "network", "Network & Internet", "wifi",      {  52, 120, 246, 255 } },
	{ "picture", "Picture",            "display",   { 124,  92, 255, 255 } },
	{ "sound",   "Sound",              "speaker",   { 255,  92, 138, 255 } },
	{ "apps",    "Apps",               "grid",      {  43, 182, 115, 255 } },
	{ "inputs",  "Inputs",             "input",     { 240, 122,  43, 255 } },
	{ "remotes", "Remotes & Bluetooth","bluetooth", {  47, 168, 224, 255 } },
	{ "privacy", "Privacy",            "lock",      {  74,  86, 116, 255 } },
	{ "storage", "Storage",            "storage",   { 142, 142, 147, 255 } },
	{ "system",  "System & About",     "info",      {  74,  86, 116, 255 } },
};
#define NSP ((int)(sizeof(SPAGES) / sizeof(*SPAGES)))

/* ── row/group buffers (rebuilt by main_fill, read by draw) ───────────── */
#define MAXR 96
#define MAXG_ 10
static SRow R_[MAXR];
static int nR;
static struct { const char *title; int i0, n; R card; float gt_y; } G_[MAXG_];
static int nG, cur_g;

static char P_title[96], P_intro[320];
static float groups_y0;
static bool has_intro;

/* string arena: dynamic row strings live here between build and draw */
static char arena[16384];
static int arena_n;
static const char *abuf(const char *fmt, ...)
{
	if (arena_n >= (int)sizeof(arena) - 4)
		return "";
	char *p = arena + arena_n;
	va_list ap;
	va_start(ap, fmt);
	int n = vsnprintf(p, sizeof(arena) - arena_n, fmt, ap);
	va_end(ap);
	if (n < 0 || arena_n + n + 1 > (int)sizeof(arena))
		return "";
	arena_n += n + 1;
	return p;
}

/* row keys: r-<pre>-<gi>-<i>, same scheme as the prototype */
static char key_arena[MAXR][64];
static int nkey;

static void gnew(const char *title)
{
	if (nG >= MAXG_)
		return;
	cur_g = nG;
	G_[nG].title = title;
	G_[nG].i0 = nR;
	G_[nG].n = 0;
	nG++;
}

static SRow *rw(const char *t, const char *k, const char *title)
{
	if (nR >= MAXR - 1 || !nG)
		gnew("");
	SRow *r = &R_[nR++];
	memset(r, 0, sizeof(*r));
	r->t = t;
	r->k = k;
	r->title = title;
	if (nG)
		G_[nG - 1].n++;
	return r;
}

/* ── context ──────────────────────────────────────────────────────────── */
static SetCtx ctx = { .page = "picture" };
SetCtx *set_ctx(void) { return &ctx; }

static const char *page_id(const char *sp_key)
{
	for (int i = 0; i < NSP; i++)
		if (!strcmp(SPAGES[i].id, sp_key))
			return SPAGES[i].id;
	return SPAGES[0].id;
}

static const char *page_name(const char *id)
{
	for (int i = 0; i < NSP; i++)
		if (!strcmp(SPAGES[i].id, id))
			return SPAGES[i].name;
	return "Settings";
}

/* ── shared row builders ──────────────────────────────────────────────── */
static const char *photo_note(void)
{
	if (strcmp(sv("photoSrc"), "Online"))
		return "Built-in pictures. Nothing is downloaded";
	if (!online())
		return "Offline · using built-in pictures for now";
	return "Online · pictures load in the background";
}

static SRow *app_row(const App *a)
{
	SRow *r = rw("nav", abuf("ap-%s", a->id), a->name);
	r->icon = abuf("app:%s", a->id);
	int stopped;
	appst_get(a->id, "stopped", &stopped);
	r->d = stopped ? "Stopped" : "";
	const StoreApp *sa = a->store ? store_app(a->id) : NULL;
	r->v = sa ? abuf("%d MB", sa->size) : "";
	r->act = abuf("sub:app:%s", a->id);
	return r;
}

static void gOff_add(SRow *r, const char *perm)
{
	/* whole-TV switches that disable an app permission */
	if (!strcmp(perm, "mic") && !sv_true("micAccess")) {
		r->dis = true;
		r->why = "The microphone is off for the whole TV (Settings > Privacy)";
		r->on = false;
	} else if (!strcmp(perm, "location") && !sv_true("location")) {
		r->dis = true;
		r->why = "Location is off for the whole TV (Settings > Privacy)";
		r->on = false;
	}
}

static void perm_row(const char *app_id, const char *perm)
{
	const char *name = perm, *icon;
	perm_info(perm, &name, &icon);
	int has;
	appst_perm(app_id, perm, &has);
	SRow *r = rw("tog", abuf("ai-p-%s", perm), name);
	r->icon = icon;
	r->on = has != 0;
	r->on_set = true;
	r->act = abuf("appperm:%s:%s", app_id, perm);
	r->key = NULL;
	gOff_add(r, perm);
}

/* ── top-level pages (port of spage()) ────────────────────────────────── */
static void fill_network(void)
{
	bool on = online() != NULL;
	int sig = 0, nn;
	Net *ns = nets_all(&nn);
	if (on)
		for (int i = 0; i < nn; i++)
			if (!strcmp(ns[i].ssid, net_cur()))
				sig = ns[i].sig;

	gnew("Wi-Fi");
	SRow *r = rw("tog", NULL, "Wi-Fi");
	r->key = "wifi";

	r = rw("nav", "nw-cur", on ? sv("wifiNet") : "Not connected");
	r->icon = on ? abuf("sig:%d", sig) : "wifioff";
	r->d = on ? abuf("Connected · %s signal", sig_word(sig))
		  : (sv_true("wifi") ? "Pick a network to connect" : "Wi-Fi is off");
	r->act = on ? abuf("sub:wifinet:%s", net_cur()) : "sub:wifi";
	if (!sv_true("wifi")) {
		r->dis = true;
		r->why = "Turn on Wi-Fi first";
	}

	r = rw("nav", "nw-list", "Wi-Fi networks");
	r->d = "Networks near the TV";
	r->act = "sub:wifi";
	if (!sv_true("wifi")) {
		r->dis = true;
		r->why = "Turn on Wi-Fi first";
	}

	int saved = 0;
	for (int i = 0; i < nn; i++)
		if (ns[i].saved) saved++;
	r = rw("nav", "nw-saved", "Saved networks");
	r->v = abuf("%d", saved);
	r->act = "sub:saved";

	gnew("Other connections");
	r = rw("nav", "nw-eth", "Ethernet");
	r->icon = "link";
	r->v = "Not connected";
	r->act = "sub:eth";

	gnew("Tools");
	r = rw("info", "nw-ip", "IP address");
	r->v = on ? ipaddr() : "—";
	r = rw("nav", "nw-speed", "Network speed test");
	r->v = sv("speedPing")[0] && sv("speedDown")[0] && strcmp(sv("speedDown"), "…")
		       ? abuf("%s Mbit/s", sv("speedDown")) : "";
	r->act = "sub:speed";
}

static void fill_picture(void)
{
	gnew("Display");
	SRow *r = rw("opt", NULL, "Resolution");
	r->key = "res";
	r = rw("opt", NULL, "Picture mode");
	r->key = "picMode";
	r->d = "Try Vivid or Cinema and watch the screen change";
	r = rw("sld", NULL, "Backlight");
	r->key = "backlight";
	r = rw("nav", "pc-adv", "Advanced picture");
	r->d = "Contrast, colour, sharpness and colour temperature";
	r->act = "sub:advpic";
	r = rw("tog", NULL, "Match frame rate");
	r->key = "frameMatch";
	r->d = "Switch refresh rate to fit what's playing";
	r = rw("tog", NULL, "Match dynamic range");
	r->key = "hdrMatch";
	r->d = "Use HDR only when the content has it";

	gnew("Comfort");
	r = rw("tog", NULL, "Night light");
	r->key = "nightLight";
	r->d = "Warmer colours, easier on the eyes at night";
	r = rw("sld", NULL, "Night light strength");
	r->key = "nightStr";
	if (!sv_true("nightLight")) {
		r->dis = true;
		r->why = "Turn on Night light first";
	}

	gnew("Screen saver & background");
	r = rw("opt", NULL, "Screen saver");
	r->key = "saverType";
	r = rw("opt", NULL, "Photos from");
	r->key = "photoSrc";
	r->d = photo_note();
	r = rw("opt", NULL, "Home background");
	r->key = "wall";
	r->d = "A new photo every day, with the photographer's name";
	r = rw("opt", NULL, "Start after");
	r->key = "saverDelay";
	r = rw("act", "pc-saver", "Start now");
	r->act = "saverstart";
	if (!strcmp(sv("saverType"), "Off")) {
		r->dis = true;
		r->why = "Pick a screen saver first";
	}
}

static void fill_sound(void)
{
	gnew("Output");
	SRow *r = rw("sld", NULL, "Volume");
	r->key = "vol";
	r = rw("opt", NULL, "Sound output");
	r->key = "soundOut";
	const char *mode = sv("soundMode");
	r = rw("opt", NULL, "Sound mode");
	r->key = "soundMode";
	r->d = !strcmp(mode, "Cinema") ? "Wider sound for films"
		     : !strcmp(mode, "Voice boost") ? "Clearer speech"
		     : !strcmp(mode, "Music") ? "Fuller bass and treble"
		     : "Balanced for most things";

	gnew("Advanced");
	r = rw("tog", NULL, "Night mode");
	r->key = "nightAudio";
	r->d = "Softens loud scenes";
	r = rw("opt", NULL, "Lip-sync delay");
	r->key = "lipsync";
	r->d = "Delay the sound if it runs ahead of the picture";
	r = rw("tog", NULL, "System sounds");
	r->key = "sysSounds";
	r->d = "A soft click when you move and select";
}

static void fill_apps(void)
{
	int nst;
	const App *const *st = app_installed_store(&nst);
	gnew("From the Store");
	if (nst)
		for (int i = 0; i < nst; i++)
			app_row(st[i]);
	else
		rw("info", NULL, "No Store apps installed");

	gnew("Built in");
	int nb;
	const App *const *bi = app_builtin(&nb);
	for (int i = 0; i < nb; i++)
		app_row(bi[i]);

	gnew("More");
	SRow *r = rw("act", "ap-all", "See all apps");
	r->act = "go:apps";
	r = rw("act", "ap-store", "Open the Store");
	r->act = "go:store";
}

static void fill_inputs(void)
{
	gnew("Connected devices");
	SRow *r = rw("nav", "in-hdmi1", "HDMI 1");
	r->icon = "monitor";
	r->d = strcmp(sv("inputs1"), "None") ? sv("inputs1") : "";
	r->v = "Connected";
	r->act = "sub:input:hdmi1";
	r = rw("nav", "in-hdmi2", "HDMI 2");
	r->icon = "monitor";
	r->d = strcmp(sv("inputs2"), "None") ? sv("inputs2") : "";
	r->v = "No signal";
	r->act = "sub:input:hdmi2";

	gnew("HDMI-CEC");
	r = rw("tog", NULL, "HDMI-CEC");
	r->key = "cec";
	r->d = "Control connected devices with this remote";
	r = rw("tog", NULL, "Turn on the TV with a device");
	r->key = "cecOn";
	if (!sv_true("cec")) { r->dis = true; r->why = "Turn on HDMI-CEC first"; }
	r = rw("tog", NULL, "Turn off devices with the TV");
	r->key = "cecOff";
	if (!sv_true("cec")) { r->dis = true; r->why = "Turn on HDMI-CEC first"; }
	r = rw("tog", NULL, "Switch input automatically");
	r->key = "autoInput";
	r->d = "When a device turns on";
	if (!sv_true("cec")) { r->dis = true; r->why = "Turn on HDMI-CEC first"; }
	r = rw("info", NULL, "Devices found");
	r->v = sv_true("cec") ? "Game console (HDMI 1)" : "—";
}

static void fill_remotes(void)
{
	gnew("Paired devices");
	int n;
	BtDev *d = bt_devs(&n);
	if (n) {
		for (int i = 0; i < n; i++) {
			SRow *r = rw("nav", abuf("bt-%s", d[i].id), d[i].name);
			r->icon = d[i].kind;
			r->d = !strcmp(d[i].kind, "remote") ? "This TV's remote" : "";
			r->v = !sv_true("bt") ? "Off"
			             : d[i].conn ? (d[i].batt >= 0 ? abuf("Battery %d%%", d[i].batt)
			                                           : "Connected")
			                         : "Not connected";
			r->act = abuf("sub:btdev:%s", d[i].id);
		}
	} else {
		rw("info", NULL, "No paired devices");
	}

	gnew("Bluetooth");
	SRow *r = rw("tog", NULL, "Bluetooth");
	r->key = "bt";
	r = rw("nav", "bt-pair", "Pair a remote or accessory");
	r->d = "Headphones, game controllers, keyboards, speakers";
	r->act = "sub:btpair";
	if (!sv_true("bt")) {
		r->dis = true;
		r->why = "Turn on Bluetooth first";
	}
}

static void fill_privacy(void)
{
	gnew("Data");
	SRow *r = rw("tog", NULL, "Usage & diagnostics");
	r->key = "diag";
	r->d = sv_true("diag")
		       ? "On · sends crash reports and usage counts once a day"
		       : "Off by default. Nothing leaves the TV unless you turn this on";
	r = rw("tog", NULL, "Location");
	r->key = "location";
	r->d = "Lets apps you allow see roughly where the TV is";
	r = rw("tog", NULL, "Microphone");
	r->key = "micAccess";
	r->d = "For voice search and apps you allow";
	r = rw("opt", NULL, "Online photos");
	r->key = "photoSrc";
	r->d = "For the screen saver and Home background";
	r->v = !strcmp(sv("photoSrc"), "Online") ? "On" : "Off";

	gnew("Permissions");
	r = rw("nav", "pv-perms", "App permissions");
	r->d = "See and change what each app can use";
	r->act = "sub:perms";

	gnew("Ads");
	r = rw("info", NULL, "Advertising ID");
	r->v = "None · this OS shows no ads";
}

static void fill_storage(void)
{
	Stor st = stor_stats();
	gnew("Internal storage");
	SRow *r = rw("bar", "sg-bar", "");
	r = rw("info", "sg-sys", "System");
	r->icon = "dot:8E8E93";
	r->v = gb(st.system);
	r = rw("nav", "sg-apps", "Apps");
	r->icon = "dot:5B8CFF";
	r->v = gb(st.apps);
	r->act = "spage:apps";
	r = rw("info", "sg-data", "App data");
	r->icon = "dot:9B7BFF";
	r->v = gb(st.data);
	r = rw("nav", "sg-rec", "Recordings");
	r->icon = "dot:FF4D5E";
	r->v = gb(st.rec);
	r->act = "gotv:recordings";
	r = rw("info", "sg-cache", "Cache");
	r->icon = "dot:3DD6B0";
	r->v = gb(st.cache);
	r = rw("act", "sg-free", "Free up space");
	r->d = "Clears caches. Apps keep their data";
	r->v = st.cache > 0 ? abuf("Clear %s", gb(st.cache)) : "";
	r->act = "freespace";
	if (st.cache <= 0) {
		r->dis = true;
		r->why = "Caches are already empty";
	}

	gnew("Removable storage");
	r = rw("info", NULL, "USB drive");
	r->v = "Not connected";
}

static void fill_system(void)
{
	gnew("Device");
	SRow *r = rw("opt", NULL, "Device name");
	r->key = "tvName";
	r = rw("opt", NULL, "Language");
	r->key = "lang";

	gnew("Date & time");
	r = rw("tog", NULL, "Set time automatically");
	r->key = "autoTime";
	r->d = "Uses the time from the network";
	r = rw("opt", NULL, "Time zone");
	r->key = "tz";
	r = rw("act", "sy-time", "Time");
	r->v = clock_str();
	r->act = "settime";
	if (sv_true("autoTime")) {
		r->dis = true;
		r->why = "Turn off automatic time first";
	}
	r = rw("tog", NULL, "Use 24-hour format");
	r->key = "clock24";

	gnew("Power & energy");
	r = rw("opt", NULL, "Sleep timer");
	r->key = "sleep";
	r->v = !strcmp(sv("sleep"), "Off") ? "Off" : abuf("Off at %s", sleep_target_str());
	r = rw("opt", NULL, "Turn off when idle");
	r->key = "autoOff";
	r->d = "When nobody presses a button";
	r = rw("opt", NULL, "When the TV turns on");
	r->key = "powerOn";

	gnew("Software");
	r = rw("nav", "sy-upd", "System update");
	r->v = sv_true("updBusy") ? "Checking…" : sv_true("updAt") ? "Up to date" : "";
	r->act = "sub:update";
	r = rw("nav", "sy-about", "About");
	r->v = "Open TV 1.0";
	r->act = "sub:about";

	gnew("Restart & reset");
	r = rw("act", "sy-restart", "Restart");
	r->act = "restart";
	r = rw("act", "sy-off", "Power off");
	r->act = "poweroff";
	r = rw("act", "sy-reset", "Reset to factory settings");
	r->d = "Erases apps, saved networks, paired devices and settings";
	r->danger = true;
	r->act = "factory";
}

/* ── sub pages (port of SUB) ──────────────────────────────────────────── */
static bool fill_wifi(void)
{
	snprintf(P_title, sizeof P_title, "Wi-Fi networks");
	snprintf(P_intro, sizeof P_intro, "%s",
		 "Pick a network to connect. Networks with a lock need a password.");
	has_intro = true;

	gnew("Nearby");
	if (!sv_true("wifiSeen")) {
		SRow *r = rw("info", "wf-scan", "Looking for networks…");
		r->busy = true;
	} else {
		int n;
		Net *ns = nets_all(&n);
		bool busyany = sv("wifiBusy")[0] != 0;
		/* connected first, then by signal strength */
		for (int pass = 0; pass < 5; pass++) {
			for (int i = 0; i < n; i++) {
				Net *nn = &ns[i];
				bool con = online() && !strcmp(nn->ssid, net_cur());
				if ((pass == 0) != con) continue;
				if (pass > 0 && nn->sig != 5 - pass) continue;
				bool busy = !strcmp(sv("wifiBusy"), nn->ssid);
				SRow *r = rw("nav", abuf("wf-%s", nn->ssid), nn->ssid);
				r->icon = abuf("sig:%d", nn->sig);
				r->d = abuf("%s%s%s",
					    con ? "Connected" : busy ? "Connecting…" : nn->saved ? "Saved" : "",
					    con || busy || nn->saved ? " · " : "",
					    !strcmp(nn->sec, "Open") ? "Open network" : nn->sec);
				r->busy = busy;
				r->vh = !busy && strcmp(nn->sec, "Open") ? "lock" : "";
				r->act = con ? abuf("sub:wifinet:%s", nn->ssid)
					     : abuf("wificonn:%s", nn->ssid);
				if (busyany && !busy) {
					r->dis = true;
					r->why = "Wait for the current connection to finish";
				}
			}
		}
	}
	gnew("More");
	SRow *r = rw("act", "wf-again", sv_true("wifiScan") ? "Searching…" : "Search again");
	r->busy = sv_true("wifiScan");
	r->act = "wifiscan";
	if (sv_true("wifiScan")) {
		r->dis = true;
		r->why = "Already searching";
	}
	r = rw("act", "wf-add", "Add a network manually");
	r->d = "For networks that hide their name";
	r->act = "wifiadd";
	return true;
}

static bool fill_saved(void)
{
	snprintf(P_title, sizeof P_title, "Saved networks");
	snprintf(P_intro, sizeof P_intro, "%s",
		 "The TV joins these by itself when they're in range.");
	has_intro = true;

	int n;
	Net *ns = nets_all(&n);
	gnew("");
	int cnt = 0;
	for (int i = 0; i < n; i++) {
		if (!ns[i].saved) continue;
		SRow *r = rw("nav", abuf("sv-%s", ns[i].ssid), ns[i].ssid);
		r->icon = "wifi";
		r->d = online() && !strcmp(ns[i].ssid, net_cur()) ? "Connected" : ns[i].sec;
		r->act = abuf("sub:wifinet:%s", ns[i].ssid);
		cnt++;
	}
	if (!cnt)
		rw("info", NULL, "No saved networks");
	return true;
}

static Net *wifi_net(const char *ssid)
{
	int n;
	Net *ns = nets_all(&n);
	for (int i = 0; i < n; i++)
		if (!strcmp(ns[i].ssid, ssid))
			return &ns[i];
	return NULL;
}

static bool fill_wifinet(const char *ssid)
{
	Net *nn = wifi_net(ssid);
	if (!nn)
		return false;
	snprintf(P_title, sizeof P_title, "%s", ssid);
	has_intro = false;
	bool con = online() && !strcmp(ssid, net_cur());

	gnew("Status");
	SRow *r = rw("info", NULL, "Status");
	r->v = con ? "Connected" : nn->saved ? "Saved" : "Not saved";
	r = rw("info", NULL, "Signal");
	r->v = sig_word(nn->sig);
	r = rw("info", NULL, "Security");
	r->v = nn->sec;
	r = rw("info", NULL, "Frequency");
	r->v = abuf("%d GHz", nn->ghz);
	r = rw("info", NULL, "IP address");
	r->v = con ? ipaddr() : "—";
	r = rw("info", NULL, "MAC address");
	r->v = "Private · 6A:2F:91:C4:0B:7E";

	gnew("Settings");
	r = rw("opt", NULL, "Proxy");
	r->key = "proxy";
	r->v = !strcmp(sv("proxy"), "Manual") ? sv("proxyAddr") : "None";
	r = rw("opt", NULL, "IP settings");
	r->key = "ipMode";
	r->v = !strcmp(sv("ipMode"), "Static") ? abuf("Static · %s", sv("staticIp"))
					       : "DHCP (automatic)";

	gnew("");
	if (!con) {
		r = rw("act", "wn-conn", "Connect");
		r->act = abuf("wificonn:%s", ssid);
		r->busy = !strcmp(sv("wifiBusy"), ssid);
		if (!sv_true("wifi") || sv("wifiBusy")[0]) {
			r->dis = true;
			r->why = sv_true("wifi") ? "Already connecting" : "Turn on Wi-Fi first";
		}
	}
	if (nn->saved) {
		r = rw("act", "wn-forget", "Forget this network");
		r->danger = true;
		r->act = abuf("wififorget:%s", ssid);
	}
	return true;
}

static bool fill_eth(void)
{
	snprintf(P_title, sizeof P_title, "Ethernet");
	snprintf(P_intro, sizeof P_intro, "%s",
		 "Plug a network cable into the LAN port on the back of the TV. It connects by itself.");
	has_intro = true;
	gnew("Status");
	SRow *r = rw("info", NULL, "Cable");
	r->v = "Not connected";
	r = rw("info", NULL, "IP address");
	r->v = "—";
	r = rw("info", NULL, "MAC address");
	r->v = "3C:91:2B:07:5E:A4";
	return true;
}

static bool fill_speed(void)
{
	snprintf(P_title, sizeof P_title, "Network speed test");
	snprintf(P_intro, sizeof P_intro, "%s",
		 "Measures how fast this TV can download and upload. It only runs when you ask.");
	has_intro = true;
	bool running = !strcmp(sv("speedDown"), "…") || !strcmp(sv("speedUp"), "…");

	gnew("Result");
	SRow *r = rw("info", "sp-ping", "Ping");
	r->v = sv("speedPing")[0] ? abuf("%s ms", sv("speedPing")) : running ? "…" : "—";
	r->busy = running && !sv("speedPing")[0];
	r = rw("info", "sp-down", "Download");
	r->v = sv("speedDown")[0] ? abuf("%s Mbit/s", sv("speedDown")) : "—";
	r->busy = !strcmp(sv("speedDown"), "…");
	r = rw("info", "sp-up", "Upload");
	r->v = sv("speedUp")[0] ? abuf("%s Mbit/s", sv("speedUp")) : "—";
	r->busy = !strcmp(sv("speedUp"), "…");

	gnew("");
	r = rw("act", "sp-run",
	       running ? "Testing…" : sv("speedPing")[0] ? "Test again" : "Start the test");
	r->act = "speedrun";
	if (running || !online()) {
		r->dis = true;
		r->why = running ? "A test is already running" : "Connect to a network first";
	}
	r = rw("info", NULL, "Test server");
	r->v = "speed.example.org";
	return true;
}

static bool fill_advpic(void)
{
	snprintf(P_title, sizeof P_title, "Advanced picture");
	snprintf(P_intro, sizeof P_intro, "%s",
		 "Fine-tune the picture. The screen changes as you adjust.");
	has_intro = true;
	gnew("Adjust");
	SRow *r = rw("sld", NULL, "Contrast");
	r->key = "contrast";
	r = rw("sld", NULL, "Colour");
	r->key = "saturation";
	r = rw("sld", NULL, "Sharpness");
	r->key = "sharpness";
	r = rw("opt", NULL, "Colour temperature");
	r->key = "colorTemp";
	gnew("");
	r = rw("act", "apx-reset", "Reset picture settings");
	r->act = "picreset";
	return true;
}

static bool fill_app(const char *id)
{
	const App *a = app_info(id);
	const StoreApp *sa = store_app(id);
	if (!a || a->input || (sa && !sa->installed))
		return false;
	snprintf(P_title, sizeof P_title, "%s", a->name);
	snprintf(P_intro, sizeof P_intro, "%s",
		 sa ? abuf("Version %s · %s · from %s", sa->ver, sa->lic, app_src_name("main"))
		    : "Built into Open TV");
	has_intro = true;
	int notif, stopped, data, cache;
	appst_get(id, "notif", &notif);
	appst_get(id, "stopped", &stopped);
	appst_get(id, "data", &data);
	appst_get(id, "cache", &cache);

	gnew("");
	SRow *r = rw("act", "ai-open", "Open");
	r->act = abuf("open:%s", id);
	if (sa && sa->next) {
		r = rw("act", "ai-upd", abuf("Update to %s", sa->next));
		r->act = abuf("appact:%s", id);
	}
	r = rw("act", "ai-stop", "Force stop");
	r->d = "Use this if the app stops responding";
	r->v = stopped ? "Stopped" : "";
	r->act = abuf("appstop:%s", id);
	if (stopped) {
		r->dis = true;
		r->why = abuf("%s isn't running", a->name);
	}
	if (sa) {
		r = rw("act", "ai-un", "Uninstall");
		r->danger = true;
		r->act = abuf("uninstall:%s", id);
	}

	gnew("Storage");
	r = rw("info", NULL, "App");
	r->v = sa ? abuf("%d MB", sa->size) : "Part of the system";
	r = rw("info", "ai-data", "Data");
	r->v = abuf("%d MB", data);
	r = rw("info", "ai-cache", "Cache");
	r->v = abuf("%d MB", cache);
	r = rw("act", "ai-cc", "Clear cache");
	r->act = abuf("appcache:%s", id);
	if (!cache) {
		r->dis = true;
		r->why = "The cache is already empty";
	}
	r = rw("act", "ai-cd", "Clear data");
	r->d = "Deletes the app's files, settings and sign-ins";
	r->danger = true;
	r->act = abuf("appdata:%s", id);
	if (!data && !cache) {
		r->dis = true;
		r->why = "There's no data to clear";
	}

	gnew("Permissions");
	int np;
	const char *const *perms = app_perms(id, &np);
	if (np)
		for (int i = 0; i < np; i++)
			perm_row(id, perms[i]);
	else
		rw("info", NULL, "Needs no permissions");

	gnew("Notifications");
	r = rw("tog", "ai-notif", "Show notifications");
	r->on = notif != 0;
	r->on_set = true;
	r->key = NULL;
	r->act = abuf("appnotif:%s", id);
	return true;
}

static bool fill_input(const char *id)
{
	bool hd1 = !strcmp(id, "hdmi1");
	const App *a = app_info(id);
	if (!a)
		return false;
	snprintf(P_title, sizeof P_title, "%s", a->name);
	snprintf(P_intro, sizeof P_intro, "%s", hd1 ? "A device is connected and sending a picture."
						   : "Nothing is sending a picture to this input.");
	has_intro = true;

	gnew("");
	SRow *r = rw("act", "ip-go", abuf("Switch to %s", a->name));
	r->act = abuf("open:%s", id);
	r = rw("opt", "ip-name", "Name");
	r->v = hd1 ? sv("inputs1") : sv("inputs2");
	r->act = abuf("inname:%s", id);
	r->key = NULL;
	r = rw("tog", "ip-home", "Show on Home");
	r->on = pin_has(id);
	r->on_set = true;
	r->key = NULL;
	r->act = abuf("pinadd:%s", id);

	gnew("Signal");
	r = rw("info", NULL, "Status");
	r->v = hd1 ? "Connected" : "No signal";
	r = rw("info", NULL, "Format");
	r->v = hd1 ? "3840 × 2160 · 60 Hz · HDR" : "—";
	return true;
}

static bool fill_btdev(const char *id)
{
	BtDev *d = bt_find(id);
	if (!d)
		return false;
	snprintf(P_title, sizeof P_title, "%s", d->name);
	has_intro = false;
	bool busy = !strcmp(sv("btBusy"), id);

	gnew("Status");
	SRow *r = rw("info", "bd-st", "Status");
	r->v = !sv_true("bt") ? "Bluetooth is off" : busy ? "" :
	                  d->conn ? "Connected" : "Not connected";
	r->busy = busy;
	r = rw("info", NULL, "Battery");
	r->v = d->batt >= 0 && d->conn && sv_true("bt") ? abuf("%d%%", d->batt) : "—";
	r = rw("info", NULL, "Type");
	r->v = bt_kind_word(d->kind);

	gnew("");
	r = rw("act", "bd-conn", d->conn ? "Disconnect" : "Connect");
	r->act = abuf("btconn:%s", id);
	if (!sv_true("bt") || busy) {
		r->dis = true;
		r->why = sv_true("bt") ? "Please wait" : "Turn on Bluetooth first";
	}
	if (!strcmp(d->kind, "remote")) {
		r = rw("act", "bd-find", "Find my remote");
		r->d = "Makes the remote beep for 10 seconds";
		r->act = "btfind";
		if (!sv_true("bt") || !d->conn) {
			r->dis = true;
			r->why = "The remote isn't connected";
		}
	}
	r = rw("act", "bd-forget", "Forget this device");
	r->danger = true;
	r->act = abuf("btforget:%s", id);
	return true;
}

static bool fill_btpair(void)
{
	snprintf(P_title, sizeof P_title, "Pair a device");
	snprintf(P_intro, sizeof P_intro, "%s",
		 "Put the device in pairing mode. Usually you hold its pairing button until a light flashes.");
	has_intro = true;

	gnew("Available devices");
	if (!sv_true("btSeen")) {
		SRow *r = rw("info", "bp-scan", "Looking for devices…");
		r->busy = true;
	} else {
		int n;
		const BtDev *nd = bt_near(&n);
		if (n) {
			for (int i = 0; i < n; i++) {
				bool busy = !strcmp(sv("btPairing"), nd[i].id);
				SRow *r = rw("nav", abuf("bp-%s", nd[i].id), nd[i].name);
				r->icon = nd[i].kind;
				r->d = busy ? "Pairing…" : bt_kind_word(nd[i].kind);
				r->busy = busy;
				r->act = abuf("btpairdo:%s", nd[i].id);
				if (sv("btPairing")[0] && !busy) {
					r->dis = true;
					r->why = "Wait for the current pairing to finish";
				}
			}
		} else {
			rw("info", "bp-none", "No new devices found");
		}
	}
	gnew("");
	SRow *r = rw("act", "bp-again", sv_true("btScan") ? "Searching…" : "Search again");
	r->busy = sv_true("btScan");
	r->act = "btscan";
	if (sv_true("btScan")) {
		r->dis = true;
		r->why = "Already searching";
	}
	return true;
}

static void settings_apps_list(const char *perm, const App *const **out, int *n)
{
	/* every app that could ask for this permission: built in + installed store apps */
	static const App *list[64];
	int k = 0, nb, nst;
	const App *const *bi = app_builtin(&nb);
	const App *const *st = app_installed_store(&nst);
	for (int i = 0; i < nb; i++) {
		int np;
		const char *const *ps = app_perms(bi[i]->id, &np);
		for (int j = 0; j < np; j++)
			if (!strcmp(ps[j], perm)) list[k++] = bi[i];
	}
	for (int i = 0; i < nst; i++) {
		int np;
		const char *const *ps = app_perms(st[i]->id, &np);
		for (int j = 0; j < np; j++)
			if (!strcmp(ps[j], perm)) list[k++] = st[i];
	}
	*out = list;
	*n = k;
}

static bool fill_perms(void)
{
	snprintf(P_title, sizeof P_title, "App permissions");
	snprintf(P_intro, sizeof P_intro, "%s",
		 "Pick a permission to see which apps can use it.");
	has_intro = true;
	gnew("");
	static const char *const perms[] = { "mic", "location", "storage", "camera" };
	for (unsigned i = 0; i < sizeof(perms) / sizeof(*perms); i++) {
		const char *name, *icon;
		perm_info(perms[i], &name, &icon);
		const App *const *apps;
		int na;
		settings_apps_list(perms[i], &apps, &na);
		int used = 0;
		for (int j = 0; j < na; j++) {
			int has;
			appst_perm(apps[j]->id, perms[i], &has);
			if (has) used++;
		}
		SRow *r = rw("nav", abuf("pm-%s", perms[i]), name);
		r->icon = icon;
		r->v = abuf("%d of %d apps", used, na);
		r->act = abuf("sub:perm:%s", perms[i]);
	}
	return true;
}

static bool fill_perm(const char *perm)
{
	const char *name, *icon;
	perm_info(perm, &name, &icon);
	bool off = (!strcmp(perm, "mic") && !sv_true("micAccess")) ||
		   (!strcmp(perm, "location") && !sv_true("location"));
	snprintf(P_title, sizeof P_title, "%s", name);
	snprintf(P_intro, sizeof P_intro, "%s",
		 off ? abuf("%s is off for the whole TV, so no app can use it. Turn it on in Settings › Privacy.", name)
		     : abuf("Apps that asked to use %s.", name));
	has_intro = true;
	gnew("");
	const App *const *apps;
	int na;
	settings_apps_list(perm, &apps, &na);
	if (na) {
		for (int i = 0; i < na; i++) {
			int has;
			appst_perm(apps[i]->id, perm, &has);
			SRow *r = rw("tog", abuf("pa-%s", apps[i]->id), apps[i]->name);
			r->icon = abuf("app:%s", apps[i]->id);
			r->on = has && !off;
			r->on_set = true;
			r->key = NULL;
			r->act = abuf("appperm:%s:%s", apps[i]->id, perm);
			if (off) {
				r->dis = true;
				r->why = "Turn it on in Settings › Privacy first";
			}
		}
	} else {
		rw("info", NULL, "No apps use this");
	}
	return true;
}

static bool fill_update(void)
{
	snprintf(P_title, sizeof P_title, "System update");
	has_intro = false;
	gnew("");
	SRow *r = rw("info", "up-st",
		     sv_true("updBusy") ? "Checking for updates…"
		     : sv_true("updAt") ? "Open TV is up to date" : "Not checked yet");
	r->busy = sv_true("updBusy");
	r = rw("info", NULL, "Version");
	r->v = "Open TV 1.0 (concept)";
	r = rw("info", NULL, "Last checked");
	r->v = sv_true("updAt") ? "Earlier today" : "Never";
	r = rw("act", "up-check", "Check now");
	r->act = "sysupdate";
	if (sv_true("updBusy")) {
		r->dis = true;
		r->why = "Already checking";
	}
	gnew("");
	r = rw("tog", NULL, "Download updates automatically");
	r->key = "sysAutoUpd";
	r->d = "Installs while the TV is in standby";
	return true;
}

static bool fill_about(void)
{
	snprintf(P_title, sizeof P_title, "About");
	has_intro = false;
	gnew("This TV");
	SRow *r = rw("info", NULL, "Device name");
	r->v = sv("tvName");
	r = rw("info", NULL, "Model");
	r->v = "Open TV reference box (QEMU)";
	r = rw("info", NULL, "Resolution");
	r->v = sv("res");
	r = rw("info", NULL, "Wi-Fi MAC");
	r->v = "6A:2F:91:C4:0B:7E";
	r = rw("info", NULL, "Serial number");
	r->v = "OTV-4F2A-91C3";
	gnew("Software");
	r = rw("info", NULL, "Open TV");
	r->v = "1.0 (concept)";
	r = rw("info", NULL, "System");
	r->v = "Buildroot Linux (aarch64)";
	r = rw("info", NULL, "Build");
	r->v = "otv 0.1 (os-build)";
	r = rw("act", "ab-lic", "Open-source licences");
	r->act = "licences";
	return true;
}

/* sub-page titles for the crumb path */
static const char *sub_title(const char *id, const char *arg)
{
	if (!strcmp(id, "wifi")) return "Wi-Fi networks";
	if (!strcmp(id, "saved")) return "Saved networks";
	if (!strcmp(id, "wifinet")) return arg;
	if (!strcmp(id, "eth")) return "Ethernet";
	if (!strcmp(id, "speed")) return "Network speed test";
	if (!strcmp(id, "advpic")) return "Advanced picture";
	if (!strcmp(id, "app")) { const App *a = app_info(arg); return a ? a->name : "App"; }
	if (!strcmp(id, "input")) { const App *a = app_info(arg); return a ? a->name : "Input"; }
	if (!strcmp(id, "btdev")) { BtDev *d = bt_find(arg); return d ? d->name : "Device"; }
	if (!strcmp(id, "btpair")) return "Pair a device";
	if (!strcmp(id, "perms")) return "App permissions";
	if (!strcmp(id, "perm")) {
		const char *n = arg;
		perm_info(arg, &n, NULL);
		return n;
	}
	if (!strcmp(id, "update")) return "System update";
	if (!strcmp(id, "about")) return "About";
	return "";
}

static bool fill_sub(const char *id, const char *arg)
{
	if (!strcmp(id, "wifi")) return fill_wifi();
	if (!strcmp(id, "saved")) return fill_saved();
	if (!strcmp(id, "wifinet")) return fill_wifinet(arg);
	if (!strcmp(id, "eth")) return fill_eth();
	if (!strcmp(id, "speed")) return fill_speed();
	if (!strcmp(id, "advpic")) return fill_advpic();
	if (!strcmp(id, "app")) return fill_app(arg);
	if (!strcmp(id, "input")) return fill_input(arg);
	if (!strcmp(id, "btdev")) return fill_btdev(arg);
	if (!strcmp(id, "btpair")) return fill_btpair();
	if (!strcmp(id, "perms")) return fill_perms();
	if (!strcmp(id, "perm")) return fill_perm(arg);
	if (!strcmp(id, "update")) return fill_update();
	if (!strcmp(id, "about")) return fill_about();
	return false;
}

/* runs once when a sub-page opens (SUBENTER) */
void sub_enter_hook(const char *id)
{
	if (!strcmp(id, "wifi")) wifi_scan_start();
	else if (!strcmp(id, "btpair")) bt_scan_start();
	else if (!strcmp(id, "speed")) {
		if (!sv("speedPing")[0] && online())
			speed_start();
	} else if (!strcmp(id, "update")) {
		if (!sv_true("updAt") && !sv_true("updBusy"))
			sys_update_start();
	}
}

/* ── main pane construction ───────────────────────────────────────────── */
static void main_fill(Layer *L)
{
	SetCtx *c = set_ctx();
	nR = 0;
	nG = 0;
	cur_g = -1;
	nkey = 0;
	arena_n = 0;
	P_title[0] = 0;
	P_intro[0] = 0;
	has_intro = false;

	/* drop nodes beyond the sidebar */
	L->ncnt = NSP;

	bool ok = false;
	while (c->nsub) {
		ok = fill_sub(c->sub[c->nsub - 1].id, c->sub[c->nsub - 1].arg);
		if (ok)
			break;
		c->nsub--; /* page no longer makes sense (app uninstalled, device forgotten) */
	}
	if (!ok) {
		c->nsub = 0;
		P_title[0] = 0;
		has_intro = false;
		for (int i = 0; i < NSP; i++)
			if (!strcmp(SPAGES[i].id, c->page)) {
				snprintf(P_title, sizeof P_title, "%s", SPAGES[i].name);
				break;
			}
		if (!strcmp(c->page, "network")) fill_network();
		else if (!strcmp(c->page, "picture")) fill_picture();
		else if (!strcmp(c->page, "sound")) fill_sound();
		else if (!strcmp(c->page, "apps")) fill_apps();
		else if (!strcmp(c->page, "inputs")) fill_inputs();
		else if (!strcmp(c->page, "remotes")) fill_remotes();
		else if (!strcmp(c->page, "privacy")) fill_privacy();
		else if (!strcmp(c->page, "storage")) fill_storage();
		else if (!strcmp(c->page, "system")) fill_system();
	}

	/* stable keys: r-<pre>-<gi>-<i> */
	char pre[192];
	snprintf(pre, sizeof pre, "%s", c->page);
	for (int i = 0; i < c->nsub; i++) {
		char seg[128];
		snprintf(seg, sizeof seg, "/%s%s%s", c->sub[i].id,
			 c->sub[i].arg[0] ? ":" : "", c->sub[i].arg);
		strncat(pre, seg, sizeof(pre) - strlen(pre) - 1);
	}
	int ki = 0;
	for (int gi = 0; gi < nG; gi++)
		for (int i = 0; i < G_[gi].n && ki < MAXR; i++)
			snprintf(key_arena[ki++], sizeof(key_arena[0]), "r-%s-%d-%d", pre, gi, i);
	ki = 0;
	for (int gi = 0; gi < nG; gi++)
		for (int i = 0; i < G_[gi].n && ki < MAXR; i++)
			R_[G_[gi].i0 + i].k = key_arena[ki++];

	/* layout: crumb 20px @136, h1 52px @164, intro @240 (h1 mb 32 - intro's -16) */
	float gy = 256;
	if (has_intro) {
		char wrapped[512];
		int lines = text_wrap(F21, tx(P_intro), 960, wrapped, sizeof wrapped);
		gy = 240 + lines * 31.5f + 28;
	}
	groups_y0 = gy;
	for (int gi = 0; gi < nG; gi++) {
		if (!G_[gi].n) {
			G_[gi].card.w = 0;
			continue;
		}
		G_[gi].gt_y = gy;
		if (G_[gi].title && G_[gi].title[0])
			gy += 18 + 12;
		float top = gy;
		rows_build(L, &R_[G_[gi].i0], G_[gi].n, 704, gy, 1112);
		gy += rows_height(&R_[G_[gi].i0], G_[gi].n, 1112);
		G_[gi].card = rrect(704, top, 1112, gy - top);
		gy += 28;
	}
	lscroll_keep(L, rrect(704, 96, 1112, 936), (int)(gy + 60 - 96));
}

/* ── rows_build: append FNodes for a row table ────────────────────────── */
float rows_height(const SRow *rows, int n, float w)
{
	(void)w;
	float h = 0;
	for (int i = 0; i < n; i++)
		h += row_h(&rows[i]);
	return h;
}

void rows_build(Layer *L, const SRow *rows, int n, float x, float y, float w)
{
	float yy = y;
	for (int i = 0; i < n; i++) {
		const SRow *row = &rows[i];
		float h = row_h(row);
		FNode *f;
		bool sld = !strcmp(row->t, "sld");
		if (sld && !row->dis) {
			f = lnode_slider(L, rrect(x, yy, w, h), row->k, row->key);
		} else {
			f = lnode(L, rrect(x, yy, w, h), row->k, "", false);
			if (row->dis) {
				lnode_dis(f, row->why);
			} else if (row->act && row->act[0]) {
				snprintf(f->act, sizeof(f->act), "%s", row->act);
			} else if (!strcmp(row->t, "tog") && row->key) {
				snprintf(f->act, sizeof(f->act), "tog:%s", row->key);
			} else if (!strcmp(row->t, "opt") && row->key) {
				snprintf(f->act, sizeof(f->act), "opt:%s", row->key);
			}
		}
		yy += h;
	}
}

/* ── build / draw ─────────────────────────────────────────────────────── */
static void settings_build(Layer *L, const char *param)
{
	SetCtx *c = set_ctx();
	if (param && param[0] && strcmp(param, c->page)) {
		c->page = page_id(param);
		c->nsub = 0;
		c->last_main[0] = 0;
	}
	for (int i = 0; i < NSP; i++)
		lnode(L, rrect(124, 208 + i * 74, 504, 68),
		      abuf("sp-%s", SPAGES[i].id), abuf("spage:%s", SPAGES[i].id),
		      !strcmp(SPAGES[i].id, c->page));
	main_fill(L);
}

static const char *upr(const char *s)
{
	static char b[96];
	snprintf(b, sizeof b, "%s", s);
	for (char *p = b; *p; p++)
		*p = (char)toupper((unsigned char)*p);
	return b;
}

static int draw_lines(void *f, gw_surface *s, float x, float y, const char *str,
		      Col c, float lh)
{
	char buf[640];
	int lines = text_wrap(f, tx(str), 960, buf, sizeof buf);
	char *p = buf;
	for (int i = 0; i < lines; i++) {
		char *nl = strchr(p, '\n');
		if (nl)
			*nl = 0;
		text_tl(f, s, x, y + i * lh, p, c);
		if (nl)
			p = nl + 1;
		else
			break;
	}
	return lines;
}

static void bar_draw(gw_surface *s, R r, Stor st)
{
	char l1[64], l2[64];
	snprintf(l1, sizeof l1, "%.1f GB of %d GB used", st.used, (int)st.total);
	snprintf(l2, sizeof l2, "%.1f GB free", st.total - st.used);
	text_tl(F22, s, r.x + 32, r.y + 24 + 16, l1, C_TEXT);
	void *f22m = font_get(500, 22);
	text_tl(f22m, s, r.x + r.w - 32 - text_w(f22m, l2), r.y + 24 + 16, l2, C_T3);
	R bar = rrect(r.x + 32, r.y + 66, r.w - 64, 12);
	gfx_rrect(s, bar, 6, (Col){ 255, 255, 255, 30 });
	struct { float v; Col c; } seg[] = {
		{ st.system, { 142, 142, 147, 255 } },
		{ st.apps, C_ACCENT },
		{ st.data, { 155, 123, 255, 255 } },
		{ st.rec, C_LIVE },
		{ st.cache, C_MINT },
	};
	float bx = bar.x;
	for (unsigned i = 0; i < sizeof(seg) / sizeof(*seg); i++) {
		float w = bar.w * seg[i].v / st.total;
		if (w <= 0)
			continue;
		gfx_fill(s, rrect(bx, bar.y, w, 12), seg[i].c);
		bx += w;
	}
}

static void settings_draw(gw_surface *s, Layer *L, float now)
{
	SetCtx *c = set_ctx();
	(void)now;

	/* sidebar */
	glass(s, rrect(96, 96, 560, 888), 40);
	text_tl(F44, s, 140, 136, "Settings", C_TEXT);
	for (int i = 0; i < NSP; i++)
		nit_draw(s, &L->n[i], SPAGES[i].name, SPAGES[i].icon, SPAGES[i].badge,
			 !strcmp(SPAGES[i].id, c->page));

	/* main pane, scrolled as one */
	R region = L->sreg;
	gfx_clip(region);
	float oy = -(float)L->scroll_y;

	/* crumb: Settings > Page > sub titles */
	char crumb[512] = "Settings > ";
	strncat(crumb, page_name(c->page), sizeof(crumb) - strlen(crumb) - 1);
	for (int i = 0; i < c->nsub; i++) {
		strncat(crumb, " > ", sizeof(crumb) - strlen(crumb) - 1);
		strncat(crumb, sub_title(c->sub[i].id, c->sub[i].arg),
			sizeof(crumb) - strlen(crumb) - 1);
	}
	text_tl(F20, s, 712, 136 + oy, tx(crumb), C_T3);
	text_tl(F52, s, 712, 164 + oy, tx(P_title), C_TEXT);
	if (has_intro && P_intro[0])
		draw_lines(F21, s, 712, 240 + oy, P_intro, C_T2, 31.5f);

	for (int gi = 0; gi < nG; gi++) {
		if (!G_[gi].n || G_[gi].card.w <= 0)
			continue;
		if (G_[gi].title && G_[gi].title[0])
			text_tl(F18, s, 716, G_[gi].gt_y + oy, upr(G_[gi].title), C_T3);
		R card = G_[gi].card;
		card.y += oy;
		glass(s, card, 28);
		for (int j = 0; j < G_[gi].n; j++) {
			int idx = NSP + G_[gi].i0 + j;
			if (idx >= L->ncnt)
				break;
			SRow *row = &R_[G_[gi].i0 + j];
			R rr = L->n[idx].r;
			rr.y += oy;
			if (rr.y + rr.h < region.y || rr.y > region.y + region.h)
				continue;
			row_draw(s, L, idx, row, rr, now, j == G_[gi].n - 1);
			if (!strcmp(row->t, "bar"))
				bar_draw(s, rr, stor_stats());
		}
	}
	gfx_clip_off();
}

/* ── focus hooks (sidebar ↔ main handoff, page switching, back) ───────── */
static void focus_first_row(void)
{
	Layer *L = scene_screen_layer();
	if (L && L->ncnt > NSP)
		scene_focus_key(L->n[NSP].k);
}

static void rebuild_refocus(void)
{
	Layer *L = scene_screen_layer();
	main_fill(L);
	L->scroll_y = 0;
	focus_first_row();
}

static bool set_onmove(int dir)
{
	Layer *L = scene_screen_layer();
	if (!L || L->focus < 0)
		return false;
	bool in_main = L->focus >= NSP;
	if (dir == GWK_RIGHT && !in_main) {
		SetCtx *c = set_ctx();
		if (c->last_main[0])
			scene_focus_key(c->last_main);
		else
			focus_first_row();
		return true;
	}
	if (dir == GWK_LEFT && in_main) {
		char key[32];
		snprintf(key, sizeof key, "sp-%s", set_ctx()->page);
		scene_focus_key(key);
		return true;
	}
	return false;
}

static void set_onfocus(FNode *f)
{
	Layer *L = scene_screen_layer();
	int idx = (int)(f - L->n);
	SetCtx *c = set_ctx();
	if (idx >= NSP) {
		snprintf(c->last_main, sizeof c->last_main, "%s", f->k);
		return;
	}
	const char *pg = f->k + 3; /* "sp-<id>" */
	if (!strcmp(pg, c->page))
		return;
	c->page = page_id(pg);
	c->nsub = 0;
	c->last_main[0] = 0;
	main_fill(L);
	L->scroll_y = 0;
}

static bool set_onback(void)
{
	Layer *L = scene_screen_layer();
	SetCtx *c = set_ctx();
	if (c->nsub) {
		char from[48];
		snprintf(from, sizeof from, "%s", c->sub[--c->nsub].from);
		main_fill(L);
		L->scroll_y = 0;
		if (from[0])
			scene_focus_key(from);
		else
			focus_first_row();
		return true;
	}
	if (L->focus >= NSP) {
		char key[32];
		snprintf(key, sizeof key, "sp-%s", c->page);
		scene_focus_key(key);
		return true;
	}
	return false;
}

void settings_hooks(Layer *L)
{
	L->onmove = set_onmove;
	L->onfocus = set_onfocus;
	L->onback = set_onback;
}

bool settings_onback(void) { return set_onback(); }

bool settings_refresh_main(void)
{
	Layer *L = scene_screen_layer();
	if (!L)
		return true;
	bool in_main = L->focus >= NSP;
	char k[48] = "";
	if (L->focus >= 0)
		snprintf(k, sizeof k, "%s", L->n[L->focus].k);
	main_fill(L);
	if (scene_in_layers())
		return true;
	if (k[0])
		scene_focus_key(k);
	if (L->focus < 0) {
		if (in_main)
			focus_first_row();
		else {
			char key[32];
			snprintf(key, sizeof key, "sp-%s", set_ctx()->page);
			scene_focus_key(key);
		}
	}
	return true;
}

/* ── sub-page open/close (act.c drives these) ─────────────────────────── */
void sub_open(const char *spec)
{
	SetCtx *c = set_ctx();
	if (c->nsub >= SUB_MAX)
		return;
	const char *colon = strchr(spec, ':');
	char id[16] = "", arg[96] = "";
	if (colon) {
		snprintf(id, sizeof id, "%.*s", (int)(colon - spec), spec);
		snprintf(arg, sizeof arg, "%s", colon + 1);
	} else {
		snprintf(id, sizeof id, "%s", spec);
	}
	Layer *L = scene_screen_layer();
	char from[48] = "";
	if (L->focus >= NSP) /* remember the row to come back to */
		snprintf(from, sizeof from, "%s", L->n[L->focus].k);
	snprintf(c->sub[c->nsub].id, sizeof c->sub[0].id, "%s", id);
	snprintf(c->sub[c->nsub].arg, sizeof c->sub[0].arg, "%s", arg);
	snprintf(c->sub[c->nsub].from, sizeof c->sub[0].from, "%s", from);
	c->nsub++;
	c->last_main[0] = 0;
	rebuild_refocus();
	sub_enter_hook(id);
}

bool sub_active(void) { return set_ctx()->nsub > 0; }

void sub_close(void) { set_onback(); }

const Screen settings_screen = { "settings", settings_build, settings_draw, settings_onback };

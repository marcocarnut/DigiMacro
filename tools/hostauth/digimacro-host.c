/*
 * digimacro-host -- give a DigiMacro board the key to its secrets.
 *
 * What follows {secret} in a macro is kept encrypted on the board, which
 * plays it only while the host has sent the key lately. This sends it: to
 * every DigiMacro plugged in whose serial number has a line in the key file.
 * With -e it sends it again every so many seconds, which turns "lately" into
 * "for as long as this runs" -- run it from the session, and stop it when the
 * screen locks.
 *
 * But first the board proves it is the board: a copy can carry the serial
 * number, and would gladly take the key. PROVE sends it eight random bytes,
 * and the answer must be them encrypted under the key's check value --
 * which only the real board holds, and which this computes from the key.
 * A board that answers anything else is not sent the key.
 *
 * The key file is what the app downloads, one line per board:
 *
 *      # comments are ignored
 *      7DD5DC7E 0123456789abcdef0123456789abcdef
 *
 * (v2's host keys, sixteen digits, are for {host}, which firmware 16 has no
 * more: such a line is skipped, with a word.)
 *
 * by default ~/.config/digimacro/host.key on Linux and
 * %APPDATA%\digimacro\host.key on Windows, and it should be readable by you
 * alone. No library either way: hidraw on Linux, which needs the udev rule in
 * web/60-digimacro.rules, and Windows' own hid.dll, which needs nothing.
 *
 *      make                    Linux
 *      make windows            digimacro-host.exe, with MinGW
 */
#define _CRT_RAND_S                 /* rand_s(), on Windows */
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#ifdef _WIN32
#include <windows.h>
#include <setupapi.h>
#include <hidsdi.h>
#include <hidpi.h>
#define strcasecmp _stricmp
#else
#include <dirent.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <unistd.h>
#include <linux/hidraw.h>
#include <sys/random.h>
#endif

#include "xtea.h"

#define VND_ID      2           /* the vendor collection's report */
#define VND_LEN     36
#define KEYAUTH     0x6A
#define PROVE       0x6B
#define KEY_LEN     16
#define MAX_BOARDS  16

/* 16c0:27db is obdev's shared ID for keyboards told apart by serial number,
   and their terms say both ends must do the telling: a board's serial starts
   with its author's domain and a colon, and this looks for that before it
   believes a device is a DigiMacro. Firmware 5 and older predate the prefix,
   and are known instead by their manufacturer and product names. Firmware 6
   sent its serial cut short, as "postcogi", and matches no key: move it to v2. */
#define SERIAL_PREFIX   "postcogito.org:"
#define OLD_NAME        "digimacro.postcogito.org DigiMacro"

struct key { char serial[16]; unsigned char key[KEY_LEN]; int last; };
static struct key keys[MAX_BOARDS];
static int nkeys;

/* ---- the key file ---------------------------------------------------- */

static int hexval(int c)
{
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

static int load_keys(const char *path)
{
    char line[256], serial[64], hex[64];
    FILE *f = fopen(path, "r");

    if (!f) { perror(path); return -1; }
#ifndef _WIN32
    /* A key anyone can read is a key anyone can send. */
    struct stat st;
    if (fstat(fileno(f), &st) == 0 && (st.st_mode & 077))
        fprintf(stderr, "%s: readable by others; chmod 600 it\n", path);
#endif

    while (fgets(line, sizeof line, f)) {
        const char *l = line + strspn(line, " \t");
        if (*l == '#' || *l == '\n' || *l == 0) continue;
        if (nkeys == MAX_BOARDS) {
            fprintf(stderr, "%s: only the first %d boards are used\n", path, MAX_BOARDS);
            break;
        }
        if (sscanf(l, "%63s %63s", serial, hex) != 2) {
            fprintf(stderr, "%s: skipping a malformed line\n", path);
            continue;
        }
        if (strlen(hex) == 16) {
            fprintf(stderr, "%s: skipping %s's v2 host key: firmware 16 uses the "
                    "32-digit key the page makes now\n", path, serial);
            continue;
        }
        if (strlen(serial) >= sizeof keys[0].serial || strlen(hex) != 2 * KEY_LEN) {
            fprintf(stderr, "%s: skipping a malformed line\n", path);
            continue;
        }
        struct key *k = &keys[nkeys];
        int good = 1;
        for (int i = 0; i < KEY_LEN; i++) {
            int hi = hexval(hex[2 * i]), lo = hexval(hex[2 * i + 1]);
            if (hi < 0 || lo < 0) good = 0;
            k->key[i] = hi << 4 | lo;
        }
        if (!good) {
            fprintf(stderr, "%s: skipping a key that is not hex\n", path);
            continue;
        }
        strcpy(k->serial, serial);
        k->last = -1;
        nkeys++;
    }
    fclose(f);
    return nkeys;
}

static struct key *key_for(const char *serial)
{
    for (int i = 0; i < nkeys; i++)
        if (!strcasecmp(keys[i].serial, serial)) return &keys[i];
    return NULL;
}

/* The board's own digits, if this is a DigiMacro at all; NULL if not. */
static const char *digits_of(const char *serial, const char *name)
{
    size_t n = strlen(SERIAL_PREFIX);
    if (!strncmp(serial, SERIAL_PREFIX, n)) return serial + n;
    if (!strcmp(name, OLD_NAME)) return serial;
    return NULL;
}

/* What could come of it: */
#define TRUSTED     1               /* the board holds the key */
#define REFUSED     0               /* wrong key, or within a second of one */
#define UNREACHED   (-1)            /* could not be asked */
#define IMPOSTOR    (-2)            /* answered PROVE wrongly: not sent */
#define TOO_OLD     (-3)            /* refused PROVE: firmware before 16 */

/* Say what changed, so a loop running for hours stays quiet. */
static void report(struct key *k, const char *digits, int r, int verbose)
{
    if (verbose || r != k->last)
        printf("%s: %s\n", digits,
               r == TRUSTED ? "holds the key" :
               r == REFUSED ? "refused: wrong key, or asked again within a second" :
               r == IMPOSTOR ? "failed to prove itself -- a copy of the board, or a key "
                               "that is not its own: the key was not sent" :
               r == TOO_OLD ? "firmware older than 16, which has no secrets: update it" :
               "could not be reached");
    k->last = r;
}

/* One request: a feature report to set, then one to get, the answer left in
   buf without the report ID -- which comes back in front or not, the one
   thing the HID specification leaves to taste; no command is 2, so it can
   tell. Defined below for each system; 0, or -1 if it could not be asked. */
typedef struct port port;
static int exchange(port *p, unsigned char *buf);

static int ask(port *p, int cmd, const unsigned char *data, int n,
               unsigned char answer[VND_LEN])
{
    unsigned char buf[1 + VND_LEN];

    memset(buf, 0, sizeof buf);
    buf[0] = VND_ID;
    buf[1] = cmd;
    buf[4] = n;
    memcpy(buf + 5, data, n);
    if (exchange(p, buf) < 0) return -1;
    memcpy(answer, buf[0] == VND_ID ? buf + 1 : buf, VND_LEN);
    memset(buf, 0, sizeof buf);
    return 0;
}

static int random_bytes(unsigned char *b, int n);

/* PROVE, and only then KEYAUTH. */
static int give_key(port *p, const unsigned char *key)
{
    unsigned char c[8], want[8], k2[KEY_LEN], a[VND_LEN];
    int r = UNREACHED;

    if (random_bytes(c, sizeof c) < 0) return UNREACHED;
    memset(k2, 0, 8);
    xtea(k2, key);                      /* the check value... */
    memcpy(k2 + 8, k2, 8);              /* ...twice, PROVE's key */
    memcpy(want, c, 8);
    xtea(want, k2);

    if (ask(p, PROVE, c, 8, a) < 0) goto out;
    if (a[0] != PROVE) { r = TOO_OLD; goto out; }
    if (memcmp(a + 4, want, 8)) { r = IMPOSTOR; goto out; }
    if (ask(p, KEYAUTH, key, KEY_LEN, a) < 0) goto out;
    r = a[0] == KEYAUTH ? TRUSTED : REFUSED;
out:
    memset(k2, 0, sizeof k2);
    return r;
}

/* ---- Linux: hidraw ------------------------------------------------------ */
#ifndef _WIN32

struct port { int fd; };

static int exchange(port *p, unsigned char *buf)
{
    if (ioctl(p->fd, HIDIOCSFEATURE(1 + VND_LEN), buf) < 0) return -1;
    memset(buf, 0, 1 + VND_LEN);
    buf[0] = VND_ID;
    return ioctl(p->fd, HIDIOCGFEATURE(1 + VND_LEN), buf) < 0 ? -1 : 0;
}

static int random_bytes(unsigned char *b, int n)
{
    return getrandom(b, n, 0) == n ? 0 : -1;
}

static int send_key(const char *dev, const unsigned char *key)
{
    port p = { open(dev, O_RDWR) };
    int r;

    if (p.fd < 0) return UNREACHED;
    r = give_key(&p, key);
    close(p.fd);
    return r;
}

/* Every DigiMacro plugged in, by its hidraw node's uevent: the kernel gives
   the serial number as HID_UNIQ and the two names as HID_NAME. */
static int sweep(int verbose)
{
    DIR *d = opendir("/sys/class/hidraw");
    struct dirent *e;
    int seen = 0, bad = 0;

    if (!d) { perror("/sys/class/hidraw"); return 2; }
    while ((e = readdir(d))) {
        char path[512], line[256], serial[256] = "", name[256] = "";
        const char *digits;
        int ours = 0;
        FILE *f;

        if (e->d_name[0] == '.') continue;
        snprintf(path, sizeof path, "/sys/class/hidraw/%s/device/uevent", e->d_name);
        if (!(f = fopen(path, "r"))) continue;
        while (fgets(line, sizeof line, f)) {
            line[strcspn(line, "\n")] = 0;
            if (!strncmp(line, "HID_ID=", 7) && strstr(line, ":000016C0:000027DB"))
                ours = 1;
            if (!strncmp(line, "HID_UNIQ=", 9))
                snprintf(serial, sizeof serial, "%s", line + 9);
            if (!strncmp(line, "HID_NAME=", 9))
                snprintf(name, sizeof name, "%s", line + 9);
        }
        fclose(f);
        if (!ours || !(digits = digits_of(serial, name))) continue;
        seen++;

        struct key *k = key_for(digits);
        if (!k) {
            if (verbose) fprintf(stderr, "%s: no key for this board\n", digits);
            bad = 1;
            continue;
        }
        snprintf(path, sizeof path, "/dev/%s", e->d_name);
        int r = send_key(path, k->key);
        if (r == UNREACHED && (verbose || r != k->last))
            fprintf(stderr, "%s: %s: %s\n", digits, path, strerror(errno));
        if (r != TRUSTED) bad = 1;
        report(k, digits, r, verbose);
    }
    closedir(d);
    if (!seen && verbose) fprintf(stderr, "no DigiMacro plugged in\n");
    return bad ? 1 : seen ? 0 : 2;
}

static void pause_s(int s) { sleep(s); }

static const char *default_file(char *buf, size_t n)
{
    const char *xdg = getenv("XDG_CONFIG_HOME"), *home = getenv("HOME");
    if (xdg && *xdg) snprintf(buf, n, "%s/digimacro/host.key", xdg);
    else snprintf(buf, n, "%s/.config/digimacro/host.key", home ? home : ".");
    return buf;
}

/* ---- Windows: hid.dll ---------------------------------------------------- */
#else

struct port { HANDLE h; };

static int exchange(port *p, unsigned char *buf)
{
    if (!HidD_SetFeature(p->h, buf, 1 + VND_LEN)) return -1;
    memset(buf, 0, 1 + VND_LEN);
    buf[0] = VND_ID;
    return HidD_GetFeature(p->h, buf, 1 + VND_LEN) ? 0 : -1;
}

static int random_bytes(unsigned char *b, int n)
{
    for (int i = 0; i < n; i++) {
        unsigned int v;
        if (rand_s(&v)) return -1;
        b[i] = (unsigned char)v;
    }
    return 0;
}

static void narrow(char *out, size_t n, const wchar_t *in)
{
    size_t i = 0;
    for (; in[i] && i + 1 < n; i++) out[i] = in[i] < 128 ? (char)in[i] : '?';
    out[i] = 0;
}

/* Windows hands out each top-level collection as a device of its own, and
   keeps the keyboard one for itself, so the one to open is the vendor
   collection -- usage page 0xFF00 -- which it lets anybody have. */
static int sweep(int verbose)
{
    GUID g;
    HDEVINFO set;
    SP_DEVICE_INTERFACE_DATA ifd;
    int seen = 0, bad = 0;

    HidD_GetHidGuid(&g);
    set = SetupDiGetClassDevsA(&g, NULL, NULL, DIGCF_PRESENT | DIGCF_DEVICEINTERFACE);
    if (set == INVALID_HANDLE_VALUE) {
        fprintf(stderr, "cannot list HID devices: error %lu\n", GetLastError());
        return 2;
    }
    ifd.cbSize = sizeof ifd;
    for (DWORD i = 0; SetupDiEnumDeviceInterfaces(set, NULL, &g, i, &ifd); i++) {
        SP_DEVICE_INTERFACE_DETAIL_DATA_A *det;
        DWORD need = 0;
        HANDLE h;
        HIDD_ATTRIBUTES a;
        PHIDP_PREPARSED_DATA pp;
        HIDP_CAPS caps;
        wchar_t w[128];
        char serial[128] = "", manu[64] = "", prod[64] = "", name[160];
        const char *digits;

        SetupDiGetDeviceInterfaceDetailA(set, &ifd, NULL, 0, &need, NULL);
        if (!need || !(det = malloc(need))) continue;
        det->cbSize = sizeof *det;
        if (!SetupDiGetDeviceInterfaceDetailA(set, &ifd, det, need, NULL, NULL)) {
            free(det);
            continue;
        }
        h = CreateFileA(det->DevicePath, GENERIC_READ | GENERIC_WRITE,
                        FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_EXISTING, 0, NULL);
        free(det);
        if (h == INVALID_HANDLE_VALUE) continue;    /* a keyboard, or not ours */

        a.Size = sizeof a;
        if (!HidD_GetAttributes(h, &a) || a.VendorID != 0x16C0 || a.ProductID != 0x27DB
            || !HidD_GetPreparsedData(h, &pp)) {
            CloseHandle(h);
            continue;
        }
        HidP_GetCaps(pp, &caps);
        HidD_FreePreparsedData(pp);
        if (caps.UsagePage != 0xFF00) { CloseHandle(h); continue; }

        if (HidD_GetSerialNumberString(h, w, sizeof w)) narrow(serial, sizeof serial, w);
        if (HidD_GetManufacturerString(h, w, sizeof w)) narrow(manu, sizeof manu, w);
        if (HidD_GetProductString(h, w, sizeof w))      narrow(prod, sizeof prod, w);
        snprintf(name, sizeof name, "%s %s", manu, prod);
        if (!(digits = digits_of(serial, name))) { CloseHandle(h); continue; }
        seen++;

        struct key *k = key_for(digits);
        if (!k) {
            if (verbose) fprintf(stderr, "%s: no key for this board\n", digits);
            bad = 1;
            CloseHandle(h);
            continue;
        }

        port p = { h };
        int r = give_key(&p, k->key);
        if (r == UNREACHED && (verbose || r != k->last))
            fprintf(stderr, "%s: error %lu\n", digits, GetLastError());
        if (r != TRUSTED) bad = 1;
        report(k, digits, r, verbose);
        CloseHandle(h);
    }
    SetupDiDestroyDeviceInfoList(set);
    if (!seen && verbose) fprintf(stderr, "no DigiMacro plugged in\n");
    return bad ? 1 : seen ? 0 : 2;
}

static void pause_s(int s) { Sleep(1000 * (DWORD)s); }

static const char *default_file(char *buf, size_t n)
{
    const char *app = getenv("APPDATA");
    snprintf(buf, n, "%s\\digimacro\\host.key", app ? app : ".");
    return buf;
}

#endif

/* ---- both ----------------------------------------------------------------- */

int main(int argc, char **argv)
{
    char def[512];
    const char *file = NULL;
    int every = 0;

    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "-k") && i + 1 < argc) file = argv[++i];
        else if (!strcmp(argv[i], "-e") && i + 1 < argc) {
            if ((every = atoi(argv[++i])) <= 0) goto usage;
        } else goto usage;
    }
    if (!file) file = default_file(def, sizeof def);
    if (load_keys(file) <= 0) {
        fprintf(stderr, "%s: no keys\n", file);
        return 2;
    }
    if (!every) return sweep(1);
    for (;;) {
        sweep(0);
        pause_s(every);
    }

usage:
    fprintf(stderr, "usage: digimacro-host [-k keyfile] [-e seconds]\n");
    return 2;
}

/*
 * digimacro-host -- prove to a DigiMacro board that it is plugged into a
 * computer it should trust.
 *
 * Macros containing {host} stop at that point unless the host has sent the
 * board's host key lately. This sends it: to every DigiMacro plugged in whose
 * serial number has a line in the key file. With -e it sends it again every
 * so many seconds, which turns "lately" into "for as long as this runs" --
 * run it from the session, and stop it when the screen locks.
 *
 * The key file is what the app downloads, one line per board:
 *
 *      # comments are ignored
 *      7DD5DC7E 0123456789abcdef
 *
 * by default ~/.config/digimacro/host.key, and it should be readable by you
 * alone. Linux only: it writes a feature report straight to hidraw, which
 * needs the udev rule in web/60-digimacro.rules and no library at all.
 *
 *      cc -O2 -Wall -o digimacro-host digimacro-host.c
 */
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <unistd.h>
#include <linux/hidraw.h>

#define VND_ID      2           /* the vendor collection's report */
#define VND_LEN     36
#define HOSTAUTH    0x64
#define REFUSED     0xEE
#define KEY_LEN     8
#define MAX_BOARDS  16

struct key { char serial[16]; unsigned char key[KEY_LEN]; int last; };
static struct key keys[MAX_BOARDS];
static int nkeys;

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
    struct stat st;
    FILE *f = fopen(path, "r");

    if (!f) { perror(path); return -1; }
    /* A key anyone can read is a key anyone can send. */
    if (fstat(fileno(f), &st) == 0 && (st.st_mode & 077))
        fprintf(stderr, "%s: readable by others; chmod 600 it\n", path);

    while (fgets(line, sizeof line, f) && nkeys < MAX_BOARDS) {
        if (line[0] == '#' || sscanf(line, "%63s %63s", serial, hex) != 2)
            continue;
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

/* 1 if the board took it, 0 if it refused, -1 if it could not be asked. */
static int send_key(const char *dev, const unsigned char *key)
{
    unsigned char buf[1 + VND_LEN];
    int fd = open(dev, O_RDWR), ok;

    if (fd < 0) return -1;
    memset(buf, 0, sizeof buf);
    buf[0] = VND_ID;
    buf[1] = HOSTAUTH;
    buf[4] = KEY_LEN;
    memcpy(buf + 5, key, KEY_LEN);
    if (ioctl(fd, HIDIOCSFEATURE(sizeof buf), buf) < 0) { close(fd); return -1; }

    memset(buf, 0, sizeof buf);
    buf[0] = VND_ID;
    if (ioctl(fd, HIDIOCGFEATURE(sizeof buf), buf) < 0) { close(fd); return -1; }
    close(fd);

    /* The answer comes back with or without the report ID in front, which is
       the one thing the HID specification leaves to taste; no command is 2. */
    ok = buf[0] == VND_ID ? buf[1] : buf[0];
    return ok == HOSTAUTH ? 1 : 0;
}

/* Every DigiMacro plugged in, by its hidraw node's uevent: the kernel names
   the board's serial number there as HID_UNIQ. */
static int sweep(int verbose)
{
    DIR *d = opendir("/sys/class/hidraw");
    struct dirent *e;
    int seen = 0, bad = 0;

    if (!d) { perror("/sys/class/hidraw"); return 2; }
    while ((e = readdir(d))) {
        char path[512], line[256], serial[64] = "";
        int ours = 0;
        FILE *f;

        if (e->d_name[0] == '.') continue;
        snprintf(path, sizeof path, "/sys/class/hidraw/%s/device/uevent", e->d_name);
        if (!(f = fopen(path, "r"))) continue;
        while (fgets(line, sizeof line, f)) {
            if (!strncmp(line, "HID_ID=", 7) && strstr(line, ":000016C0:000027DB"))
                ours = 1;
            if (!strncmp(line, "HID_UNIQ=", 9))
                sscanf(line + 9, "%63s", serial);
        }
        fclose(f);
        if (!ours) continue;
        seen++;

        struct key *k = key_for(serial);
        if (!k) {
            if (verbose) fprintf(stderr, "%s: no key for this board\n", serial);
            continue;
        }
        snprintf(path, sizeof path, "/dev/%s", e->d_name);
        int r = send_key(path, k->key);
        if (r < 0) {
            fprintf(stderr, "%s: %s: %s\n", serial, path, strerror(errno));
            bad = 1;
        } else if (!r) {
            bad = 1;
        }
        /* Say what changed, so a loop running for hours stays quiet. */
        if (verbose || r != k->last)
            printf("%s: %s\n", serial, r > 0 ? "trusted" : r == 0
                   ? "refused: wrong key, or asked again within a second"
                   : "could not be reached");
        k->last = r;
    }
    closedir(d);
    if (!seen && verbose) fprintf(stderr, "no DigiMacro plugged in\n");
    return bad ? 1 : seen ? 0 : 2;
}

int main(int argc, char **argv)
{
    char def[512];
    const char *file = NULL, *home = getenv("HOME"), *xdg = getenv("XDG_CONFIG_HOME");
    int every = 0, opt;

    while ((opt = getopt(argc, argv, "k:e:")) != -1) {
        if (opt == 'k') file = optarg;
        else if (opt == 'e') every = atoi(optarg);
        else goto usage;
        if (opt == 'e' && every <= 0) goto usage;
    }
    if (optind != argc) goto usage;

    if (!file) {
        if (xdg && *xdg) snprintf(def, sizeof def, "%s/digimacro/host.key", xdg);
        else snprintf(def, sizeof def, "%s/.config/digimacro/host.key", home ? home : ".");
        file = def;
    }
    if (load_keys(file) <= 0) {
        fprintf(stderr, "%s: no keys\n", file);
        return 2;
    }
    if (!every) return sweep(1);
    for (;;) {
        sweep(0);
        sleep(every);
    }

usage:
    fprintf(stderr, "usage: digimacro-host [-k keyfile] [-e seconds]\n");
    return 2;
}

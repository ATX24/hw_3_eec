/*
 * rapl: run a command and report the energy it consumed, using the Intel RAPL
 * counters exposed by the Linux powercap interface.
 *
 *   usage: rapl [-r REPEATS] [-o out.csv] [-l LABEL] COMMAND [ARGS...]
 *
 * The command's own stdout/stderr pass through untouched; rapl's report goes
 * to stderr. With -o, one CSV row per run is appended to the given file.
 *
 * RAPL counters are package-wide: everything running on the socket is
 * counted, not just COMMAND.
 */
#define _GNU_SOURCE
#include <dirent.h>
#include <errno.h>
#include <getopt.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

#define POWERCAP "/sys/class/powercap"
#define MAX_ZONES 32

struct zone {
    char path[300];
    char label[80];   /* e.g. "package-0" or "package-0/dram" */
    int is_dram;
    uint64_t max_range;
};

static struct zone zones[MAX_ZONES];
static int nzones;

static int read_u64(const char *dir, const char *file, uint64_t *out)
{
    char p[512];
    snprintf(p, sizeof p, "%s/%s", dir, file);
    FILE *f = fopen(p, "r");
    if (!f)
        return -1;
    unsigned long long v;
    int ok = fscanf(f, "%llu", &v) == 1;
    fclose(f);
    if (!ok)
        return -1;
    *out = v;
    return 0;
}

static int read_str(const char *dir, const char *file, char *buf, size_t n)
{
    char p[512];
    snprintf(p, sizeof p, "%s/%s", dir, file);
    FILE *f = fopen(p, "r");
    if (!f)
        return -1;
    if (!fgets(buf, n, f)) {
        fclose(f);
        return -1;
    }
    fclose(f);
    buf[strcspn(buf, "\n")] = 0;
    return 0;
}

static int cmp_zone(const void *a, const void *b)
{
    return strcmp(((const struct zone *)a)->path, ((const struct zone *)b)->path);
}

static void discover_zones(void)
{
    DIR *d = opendir(POWERCAP);
    if (!d) {
        perror("rapl: " POWERCAP);
        exit(1);
    }
    struct dirent *e;
    while ((e = readdir(d)) && nzones < MAX_ZONES) {
        /* intel-rapl:N is a package, intel-rapl:N:M is a subzone */
        if (strncmp(e->d_name, "intel-rapl:", 11) != 0)
            continue;
        struct zone *z = &zones[nzones];
        snprintf(z->path, sizeof z->path, POWERCAP "/%.255s", e->d_name);
        uint64_t tmp;
        if (read_u64(z->path, "energy_uj", &tmp) != 0) {
            fprintf(stderr, "rapl: cannot read %s/energy_uj: %s\n", z->path, strerror(errno));
            exit(1);
        }
        if (read_u64(z->path, "max_energy_range_uj", &z->max_range) != 0)
            z->max_range = 0;
        char name[32] = "?";
        read_str(z->path, "name", name, sizeof name);
        z->is_dram = strcmp(name, "dram") == 0;
        nzones++;
    }
    closedir(d);
    if (nzones == 0) {
        fprintf(stderr, "rapl: no intel-rapl zones found under " POWERCAP "\n");
        exit(1);
    }
    qsort(zones, nzones, sizeof zones[0], cmp_zone);

    /* Build labels; subzones are prefixed with their parent package name. */
    for (int i = 0; i < nzones; i++) {
        char name[32] = "?";
        read_str(zones[i].path, "name", name, sizeof name);
        const char *base = strrchr(zones[i].path, '/') + 1;
        if (strchr(base + 11, ':')) {
            char parent[300], pname[32] = "?";
            memcpy(parent, zones[i].path, sizeof parent);
            *strrchr(parent, ':') = 0;
            read_str(parent, "name", pname, sizeof pname);
            snprintf(zones[i].label, sizeof zones[i].label, "%s/%s", pname, name);
        } else {
            snprintf(zones[i].label, sizeof zones[i].label, "%s", name);
        }
    }
}

static void sample(uint64_t *v)
{
    for (int i = 0; i < nzones; i++)
        read_u64(zones[i].path, "energy_uj", &v[i]);
}

static double now(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec + ts.tv_nsec * 1e-9;
}

static void usage(void)
{
    fprintf(stderr,
            "usage: rapl [-r REPEATS] [-o out.csv] [-l LABEL] COMMAND [ARGS...]\n"
            "  -r N      run the command N times (default 1)\n"
            "  -o FILE   append one CSV row per run to FILE\n"
            "  -l LABEL  label for the CSV rows (default: the command line)\n");
    exit(2);
}

int main(int argc, char **argv)
{
    int repeats = 1;
    const char *csv = NULL;
    const char *label = NULL;
    int c;
    while ((c = getopt(argc, argv, "+r:o:l:h")) != -1) {
        switch (c) {
        case 'r': repeats = atoi(optarg); break;
        case 'o': csv = optarg; break;
        case 'l': label = optarg; break;
        default: usage();
        }
    }
    if (optind >= argc || repeats < 1)
        usage();
    char **cmd = &argv[optind];

    char cmdline[1024] = "";
    for (char **a = cmd; *a; a++) {
        strncat(cmdline, *a, sizeof cmdline - strlen(cmdline) - 2);
        if (a[1])
            strcat(cmdline, " ");
    }
    if (!label)
        label = cmdline;

    discover_zones();

    FILE *out = NULL;
    if (csv) {
        struct stat st;
        int fresh = stat(csv, &st) != 0 || st.st_size == 0;
        out = fopen(csv, "a");
        if (!out) {
            perror(csv);
            return 1;
        }
        if (fresh) {
            fprintf(out, "label,run,seconds,pkg_joules,dram_joules");
            for (int i = 0; i < nzones; i++)
                fprintf(out, ",%s", zones[i].label);
            fprintf(out, ",exit_status\n");
        }
    }

    double *pkg_runs = calloc(repeats, sizeof(double));
    double *sec_runs = calloc(repeats, sizeof(double));
    int status = 0;

    for (int r = 0; r < repeats; r++) {
        uint64_t before[MAX_ZONES], after[MAX_ZONES];
        fflush(NULL);
        sample(before);
        double t0 = now();

        pid_t pid = fork();
        if (pid < 0) {
            perror("rapl: fork");
            return 1;
        }
        if (pid == 0) {
            execvp(cmd[0], cmd);
            fprintf(stderr, "rapl: cannot run '%s': %s\n", cmd[0], strerror(errno));
            _exit(127);
        }
        int ws;
        waitpid(pid, &ws, 0);

        double t1 = now();
        sample(after);
        status = WIFEXITED(ws) ? WEXITSTATUS(ws) : 128 + WTERMSIG(ws);

        double joules[MAX_ZONES], pkg = 0, dram = 0;
        for (int i = 0; i < nzones; i++) {
            uint64_t d = after[i] >= before[i]
                             ? after[i] - before[i]
                             : after[i] + (zones[i].max_range - before[i]); /* counter wrapped */
            joules[i] = d / 1e6;
            if (zones[i].is_dram)
                dram += joules[i];
            else
                pkg += joules[i];
        }
        double secs = t1 - t0;
        pkg_runs[r] = pkg;
        sec_runs[r] = secs;

        fprintf(stderr, "[rapl] %s", cmdline);
        if (repeats > 1)
            fprintf(stderr, "  (run %d/%d)", r + 1, repeats);
        fprintf(stderr, "\n");
        fprintf(stderr, "[rapl]   %-18s %10.3f s\n", "time", secs);
        for (int i = 0; i < nzones; i++)
            fprintf(stderr, "[rapl]   %-18s %10.3f J\n", zones[i].label, joules[i]);
        fprintf(stderr, "[rapl]   %-18s %10.3f J   (avg %.1f W)\n", "package total", pkg, pkg / secs);
        fprintf(stderr, "[rapl]   %-18s %10.3f J   (avg %.1f W)\n", "dram total", dram, dram / secs);
        if (status != 0)
            fprintf(stderr, "[rapl]   WARNING: command exited with status %d\n", status);

        if (out) {
            fprintf(out, "\"%s\",%d,%.6f,%.6f,%.6f", label, r + 1, secs, pkg, dram);
            for (int i = 0; i < nzones; i++)
                fprintf(out, ",%.6f", joules[i]);
            fprintf(out, ",%d\n", status);
            fflush(out);
        }
    }

    if (repeats > 1) {
        double ms = 0, mp = 0, vs = 0, vp = 0;
        for (int r = 0; r < repeats; r++) {
            ms += sec_runs[r];
            mp += pkg_runs[r];
        }
        ms /= repeats;
        mp /= repeats;
        for (int r = 0; r < repeats; r++) {
            vs += (sec_runs[r] - ms) * (sec_runs[r] - ms);
            vp += (pkg_runs[r] - mp) * (pkg_runs[r] - mp);
        }
        fprintf(stderr, "[rapl] summary over %d runs: time %.3f +/- %.3f s, package %.3f +/- %.3f J\n",
                repeats, ms, sqrt(vs / (repeats - 1)), mp, sqrt(vp / (repeats - 1)));
    }

    if (out)
        fclose(out);
    return status;
}

// pi.c — Standalone Pi Calculator with Live Streaming
// Compile: cl /O2 /MT /I"gmpy2_include" pi.c /link /LIBPATH:"gmpy2_lib" gmp.lib
// Usage:
//   pi [precision]           — compute to N digits
//   pi --live [-o FILE]      — streaming mode

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <gmp.h>

// ───── Binary Splitting ───────────────────────────────────────

static void bs_inner(int a, int b, mpz_t p, mpz_t q, mpz_t t) {
    if (b - a == 1) {
        mpz_set_si(p, -24);
        mpz_mul_si(p, p, 6L * a - 5);
        mpz_mul_si(p, p, 2L * a - 1);
        mpz_mul_si(p, p, 6L * a - 1);
        mpz_set_ui(q, (unsigned long)a);
        mpz_mul_ui(q, q, (unsigned long)a);
        mpz_mul_ui(q, q, (unsigned long)a);
        mpz_mul_ui(q, q, 640320U);
        mpz_mul_ui(q, q, 640320U);
        mpz_mul_ui(q, q, 640320U);
        mpz_set_si(t, 545140134L);
        mpz_mul_si(t, t, a);
        mpz_add_ui(t, t, 13591409);
        mpz_mul(t, t, p);
        return;
    }
    int m = (a + b) / 2;
    mpz_t p1, q1, t1, p2, q2, t2;
    mpz_inits(p1, q1, t1, p2, q2, t2, NULL);
    bs_inner(a, m, p1, q1, t1);
    bs_inner(m, b, p2, q2, t2);
    mpz_mul(p, p1, p2);
    mpz_mul(q, q1, q2);
    mpz_mul(t, t2, p1);
    mpz_addmul(t, t1, q2);
    mpz_clears(p1, q1, t1, p2, q2, t2, NULL);
}

// ───── Incremental BS Cache ───────────────────────────────────

typedef struct {
    mpz_t P, Q, T;
    int n;
} BSCache;

static void cache_init(BSCache* c) {
    mpz_inits(c->P, c->Q, c->T, NULL);
    c->n = 0;
}

static void cache_clear(BSCache* c) {
    mpz_clears(c->P, c->Q, c->T, NULL);
}

static void cache_extend(BSCache* c, int target) {
    if (target <= c->n) return;
    if (c->n == 0) {
        mpz_t P_unused;
        mpz_init(P_unused);
        bs_inner(1, target, P_unused, c->Q, c->T);
        mpz_set(c->P, P_unused);
        mpz_clear(P_unused);
        c->n = target;
        return;
    }
    mpz_t pn, qn, tn, p_merged, q_merged, t_merged;
    mpz_inits(pn, qn, tn, p_merged, q_merged, t_merged, NULL);
    bs_inner(c->n, target, pn, qn, tn);
    mpz_mul(p_merged, c->P, pn);
    mpz_mul(q_merged, c->Q, qn);
    mpz_mul(t_merged, tn, c->P);
    mpz_addmul(t_merged, c->T, qn);
    mpz_swap(c->P, p_merged);
    mpz_swap(c->Q, q_merged);
    mpz_swap(c->T, t_merged);
    c->n = target;
    mpz_clears(pn, qn, tn, p_merged, q_merged, t_merged, NULL);
}

// Compute pi string from cache at given precision (truncated)
static char* cache_to_string(BSCache* c, int precision) {
    int N = precision, safety = 5;
    mpz_t radicand, sqrt_scaled, num, den, pi_scaled;
    mpz_inits(radicand, sqrt_scaled, num, den, pi_scaled, NULL);

    mpz_ui_pow_ui(radicand, 10, 2u * (unsigned)(N + safety));
    mpz_mul_ui(radicand, radicand, 10005);
    mpz_sqrt(sqrt_scaled, radicand);
    mpz_clear(radicand);

    mpz_mul(num, sqrt_scaled, c->Q);
    mpz_mul_ui(num, num, 426880);
    mpz_mul_ui(den, c->Q, 13591409);
    mpz_add(den, den, c->T);
    mpz_tdiv_q(pi_scaled, num, den);

    mpz_clears(sqrt_scaled, num, den, NULL);

    char* full = mpz_get_str(NULL, 10, pi_scaled);
    mpz_clear(pi_scaled);

    int flen = (int)strlen(full);
    int out_len = N + 2;  // "3." + N digits
    char* result = (char*)malloc((size_t)out_len + 1);
    if (!result) { free(full); return NULL; }

    result[0] = full[0];
    result[1] = '.';
    int avail = flen - 1;
    if (avail >= N)
        memcpy(result + 2, full + 1, (size_t)N);
    else {
        memcpy(result + 2, full + 1, (size_t)avail);
        memset(result + 2 + avail, '0', (size_t)(N - avail));
    }
    result[out_len] = '\0';
    free(full);
    return result;
}

// ───── Single-shot mode ──────────────────────────────────────

static char* compute_pi(int precision) {
    int n_terms = precision / 14 + 3;
    BSCache c;
    cache_init(&c);
    cache_extend(&c, n_terms);
    char* result = cache_to_string(&c, precision);
    cache_clear(&c);
    return result;
}

// ───── Live mode ─────────────────────────────────────────────

static volatile int stop_flag = 0;

static void handle_sigint(int sig) {
    (void)sig;
    stop_flag = 1;
}

#ifdef _WIN32
#include <windows.h>
static double get_time() {
    LARGE_INTEGER t, f;
    QueryPerformanceFrequency(&f);
    QueryPerformanceCounter(&t);
    return (double)t.QuadPart / (double)f.QuadPart;
}
#else
#include <time.h>
static double get_time() {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec + ts.tv_nsec * 1e-9;
}
#endif

static void live_mode(const char* outfile) {
    signal(SIGINT, handle_sigint);

    BSCache cache;
    cache_init(&cache);

    // Output "3."
    printf("3.");
    fflush(stdout);

    FILE* f = NULL;
    if (outfile) {
        f = fopen(outfile, "w");
        if (f) { fprintf(f, "3."); fflush(f); }
    }

    char* previous = strdup("3.");
    int n_terms = 15;  // start ~100 digits worth
    double start = get_time();
    int total_digits = 0;

    while (!stop_flag) {
        cache_extend(&cache, n_terms);

        int cur_prec = (n_terms - 3) * 14;
        if (cur_prec < 50) cur_prec = 50;

        char* result = cache_to_string(&cache, cur_prec);
        if (!result) break;

        int prev_len = (int)strlen(previous);
        int curr_len = (int)strlen(result);

        if (curr_len > prev_len) {
            // Output only new digits
            printf("%s", result + prev_len);
            fflush(stdout);
            if (f) {
                fprintf(f, "%s", result + prev_len);
                fflush(f);
            }
        }

        total_digits = curr_len - 2;
        free(previous);
        previous = result;

        double elapsed = get_time() - start;
        fprintf(stderr, "\r  %d digits, %.1f s  [Ctrl+C to stop]",
                total_digits, elapsed);
        fflush(stderr);

        // Next batch: geometric growth
        n_terms = (int)((double)n_terms * 1.5 + 1);
    }

    double elapsed = get_time() - start;
    fprintf(stderr, "\n  Stopped. Total: %d digits in %.1f s\n",
            total_digits, elapsed);

    if (f) {
        fclose(f);
        fprintf(stderr, "  Saved to %s\n", outfile);
    }

    free(previous);
    cache_clear(&cache);
}

// ───── CLI ────────────────────────────────────────────────────

int main(int argc, char** argv) {
    int precision = 100;
    const char* outfile = NULL;
    int quiet = 0;
    int live = 0;

    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-o") == 0 && i + 1 < argc)
            outfile = argv[++i];
        else if (strcmp(argv[i], "-q") == 0)
            quiet = 1;
        else if (strcmp(argv[i], "--live") == 0)
            live = 1;
        else if (strcmp(argv[i], "-h") == 0 || strcmp(argv[i], "--help") == 0) {
            printf("Usage: pi [options]\n");
            printf("  pi [precision]           compute N digits\n");
            printf("  pi --live [-o FILE]      live streaming mode\n");
            printf("  pi -h                    this help\n");
            printf("Options:\n");
            printf("  -o FILE   write to file\n");
            printf("  -q        quiet (suppress timing)\n");
            return 0;
        } else {
            precision = atoi(argv[i]);
            if (precision < 1) precision = 100;
        }
    }

    if (live) {
        live_mode(outfile);
        return 0;
    }

    if (!quiet)
        fprintf(stderr, "Computing pi to %d digits...\n", precision);

    double t0 = get_time();
    char* pi_str = compute_pi(precision);
    double t1 = get_time();

    if (!pi_str) {
        fprintf(stderr, "Error: out of memory\n");
        return 1;
    }

    if (outfile) {
        FILE* f = fopen(outfile, "w");
        if (f) {
            fprintf(f, "%s\n", pi_str);
            fclose(f);
            if (!quiet)
                fprintf(stderr, "Saved to %s\n", outfile);
        } else {
            fprintf(stderr, "Error: cannot write %s\n", outfile);
        }
    } else {
        printf("%s\n", pi_str);
    }

    if (!quiet) {
        if (t1 - t0 < 1.0)
            fprintf(stderr, "Time: %.1f ms\n", (t1 - t0) * 1000.0);
        else
            fprintf(stderr, "Time: %.2f s\n", t1 - t0);
    }

    free(pi_str);
    return 0;
}


#include "config.h"
#include "uart.h"
#include "waveshare_fp.h"
#include "user_db.h"
#include "gate.h"

#include <pthread.h>
#include <stdio.h>
#include <stdarg.h>
#include <unistd.h>
#include <signal.h>
#include <string.h>
#include <time.h>
#include <stdlib.h>


/* glas za ctrl+C to jest stop */
static volatile int g_stop = 0;


static pthread_mutex_t fp_m = PTHREAD_MUTEX_INITIALIZER;

/* flag za enrollment kako bi drugi tred za skeniranje pauzirao, kada se desava enrollment */
static volatile int g_pause_scanner = 0;

/* nakon ctrl+c zaustavi sve tredove */
static void on_sigint(int s) { (void)s; g_stop = 1; }


/*
 * u sledecoj funkciji logujemo eventove i ispisujemo ih
 * 
 */
static void log_line(const char* fmt, ...) {
    static FILE* f = NULL;
    if (!f) {
        system("mkdir -p logs");
        f = fopen(LOG_FILE, "a");
        if (!f) return;
    }
    time_t t = time(NULL);
    struct tm tm;
    localtime_r(&t, &tm);
    char ts[64];
    strftime(ts, sizeof ts, "%Y-%m-%d %H:%M:%S", &tm);

    fprintf(f, "%s ", ts);
    va_list ap;
    va_start(ap, fmt);
    vfprintf(f, fmt, ap);
    va_end(ap);
    fprintf(f, "\n");
    fflush(f);
}

static pthread_mutex_t last_m = PTHREAD_MUTEX_INITIALIZER;
static int last_id = -1;
static time_t last_ts = 0;


/* sacuvaj poslednji uspesno amtchovan ID model i dodaj ga */
static void set_last(int id) {
    pthread_mutex_lock(&last_m);
    last_id = id;
    last_ts = time(NULL);
    pthread_mutex_unlock(&last_m);
}



/* iscitaj poslednji matchovan templejt + i vreme. Vrati 1 ako je validan */
static int get_last(int* id, time_t* ts) {
    pthread_mutex_lock(&last_m);
    *id = last_id;
    *ts = last_ts;
    pthread_mutex_unlock(&last_m);
    return (*id != -1);
}


/* sleep funkcija u milisekundama */
static void msleep(int ms) { usleep(ms * 1000); }

typedef struct { fp_dev_t fp; } scan_ctx_t;


/*
 * Pozadinski tred
 *  - cita da li je trenutno prislonjen prst i fleguje
 *  - prikuplja sliku
 *  - na osnovu slike generise buffer kojim puni buffer u RAM-u
 *  - Pretrazuje ga u skenerovoj memoriji
 *  - ako ga matchuje updateuje ga da moze da pristupa
 */
static void* scanner_thread(void* arg) {
    scan_ctx_t* ctx = (scan_ctx_t*)arg;
    log_line("Scanner started");
    while (!g_stop) {
        
        if (g_pause_scanner) { msleep(50); continue; }
        /* 1) proveri da li je prst prisutan na skeneru */
        int present = 0;
        pthread_mutex_lock(&fp_m);
        int fd_r = fp_finger_detect(&ctx->fp, &present);
        pthread_mutex_unlock(&fp_m);
        if (fd_r != 0) { msleep(100); continue; }
        if (!present) { msleep(60); continue; }

        /* 2) prikupi sliku i ako se vraca neki erorr probaj jos par puta (maks 15)*/
        int got_img = 0;
        for (int i = 0; i < 15; i++) {
            pthread_mutex_lock(&fp_m);
            int gi_r = fp_get_image(&ctx->fp);
            pthread_mutex_unlock(&fp_m);
            if (gi_r == 0) { got_img = 1; break; }
            msleep(80);
        }
        if (!got_img) { log_line("GET_IMAGE fail (after retries)"); msleep(150); continue; }
        /* 3) konvertuj sliku u templejt ocekivanog oblika i stavi ga u buffer */
        pthread_mutex_lock(&fp_m);
        int gen_r = fp_generate(&ctx->fp, 0);
        pthread_mutex_unlock(&fp_m);
        if (gen_r != 0) { log_line("GENERATE fail"); msleep(150); continue; }

        /* 4) nadji taj template u memoriji skenera */
        int matched = -1;
        pthread_mutex_lock(&fp_m);
        int r = fp_search(&ctx->fp, 0, SEARCH_START_ID, SEARCH_END_ID, &matched);
        pthread_mutex_unlock(&fp_m);
        if (r == 0) {
            /* ako je nadjen sacuvaj ga d amoze da se kopristi za autorizaciju */
            log_line("SCAN matched_id=%d", matched);
            set_last(matched);
            /* i sleep kao resenje za debounce da ne loaduje jedno te isto dok pokusavamo da sacuvamo vec loadovano */
            msleep(600);
        } else {
            log_line("SEARCH no match");
            msleep(200);
        }
    }
    return NULL;
}


/*
 * get_image moze da filuje pa  ovaj helper ponavlja get image X puta sa delayom
 * 
 */
static int capture_with_retries(fp_dev_t* fp) {
    for (int i = 0; i < ENROLL_GET_IMAGE_RETRIES; i++) {
        if (fp_get_image(fp) == 0) return 0;
        msleep(ENROLL_GET_IMAGE_RETRY_DELAY_MS);
    }
    return -1;
}


/*
 * busywait dok ne oseti prst 0/1 flag sa prisutnost prsta
 * 
 */
static int wait_present(fp_dev_t* fp, int want_present, int timeout_ms) {
    int elapsed = 0;
    while (elapsed < timeout_ms && !g_stop) {
        int present = 0;
        if (fp_finger_detect(fp, &present) != 0) { msleep(80); elapsed += 80; continue; }
        if (present == want_present) return 0;
        msleep(80);
        elapsed += 80;
    }
    return -1;
}


/*proveri da li unet pin se poklapa sa onim koji smo postavili u konfiguracijskom fajlu */
static int pin_ok(const char* pin) { return (pin && strcmp(pin, ACCESS_PIN) == 0); }



/*
 * Enrollment procedura
 *  1) pauziraj skener tred i lokuj mutex.
 *  2) sacuvaj isti prst 3 razlicita put au ram buffer.
 *  3) merge-uj sva tri sample-a i napravi jedan kombinovani .
 *  4) uzmi slobodan template ID.
 *  5) stavi ga u memoriju skenera i dodaj ga  u whitelistu za acces.
 * 
 */
static int do_enroll(fp_dev_t* fp, const char* name) {
    int rc = 0;
    log_line("ENROLL requested name=%s", name);

    // pauziraj background scanner
    g_pause_scanner = 1;
    msleep(200);
    pthread_mutex_lock(&fp_m);

    // korak 1/3

    printf("ENROLL: Place NEW finger on sensor (1/3)...\n");
    fflush(stdout);
    log_line("ENROLL: waiting finger (1/3)");
    if (wait_present(fp, 1, 20000) != 0) { log_line("ENROLL timeout waiting finger (1/3)"); rc = -101; goto cleanup; }
    if (capture_with_retries(fp) != 0) { log_line("ENROLL GET_IMAGE fail (1/3)"); rc = -102; goto cleanup; }
    if (fp_generate(fp, 0) != 0) { log_line("ENROLL GENERATE fail (1/3)"); rc = -103; goto cleanup; }

    /* reci useru da skloni prst tako da se slededci ocitava kao novi */
    printf("ENROLL: REMOVE finger.\n");
    fflush(stdout);
    log_line("ENROLL: remove finger");
    if (wait_present(fp, 0, 20000) != 0) { log_line("ENROLL timeout waiting remove (after 1/3)"); rc = -104; goto cleanup; }

    // korak 2/3
    
    printf("ENROLL: Place SAME finger again (2/3)...\n");
    fflush(stdout);
    log_line("ENROLL: waiting finger (2/3)");
    if (wait_present(fp, 1, 20000) != 0) { log_line("ENROLL timeout waiting finger (2/3)"); rc = -105; goto cleanup; }
    if (capture_with_retries(fp) != 0) { log_line("ENROLL GET_IMAGE fail (2/3)"); rc = -106; goto cleanup; }
    if (fp_generate(fp, 1) != 0) { log_line("ENROLL GENERATE fail (2/3)"); rc = -107; goto cleanup; }


    /* opet kaoiznad */
    printf("ENROLL: REMOVE finger.\n");
    fflush(stdout);
    log_line("ENROLL: remove finger (after 2/3)");
    if (wait_present(fp, 0, 20000) != 0) { log_line("ENROLL timeout waiting remove (after 2/3)"); rc = -109; goto cleanup; }

    // korak 3/3 : sacuvaj -> stavi u buff2 -> narpavi merge ->0

    printf("ENROLL: Place SAME finger again (3/3)...\n");
    fflush(stdout);
    log_line("ENROLL: waiting finger (3/3)");
    if (wait_present(fp, 1, 20000) != 0) { log_line("ENROLL timeout waiting finger (3/3)"); rc = -110; goto cleanup; }
    if (capture_with_retries(fp) != 0) { log_line("ENROLL GET_IMAGE fail (3/3)"); rc = -111; goto cleanup; }
    if (fp_generate(fp, 2) != 0) { log_line("ENROLL GENERATE fail (3/3)"); rc = -112; goto cleanup; }

    /* mergeuj sad sva tri u jedan najkvalitetniji */
    if (fp_merge(fp, 0, 3) != 0) { log_line("ENROLL MERGE fail (count=3 to buf0)"); rc = -113; goto cleanup; }

    // uzmi slobodan templejt ID
    uint16_t new_id_u16 = 0;
    if (fp_get_empty_id(fp, SEARCH_START_ID, SEARCH_END_ID, &new_id_u16) != 0) {
   
        int tmp = userdb_max_id(USERS_FILE) + 1;
        if (tmp < SEARCH_START_ID) tmp = SEARCH_START_ID;
        new_id_u16 = (uint16_t)tmp;
    }
    int new_id = (int)new_id_u16;

    uint16_t dup = 0;
    int st = fp_store(fp, (uint16_t)new_id, 0, &dup);
    if (st != 0) {
        log_line("ENROLL STORE fail id=%d (module_ret=%d dup=%u)", new_id, -st, dup);
        rc = -114;
        goto cleanup;
    }

    if (!userdb_append(USERS_FILE, new_id, name, "user")) {
        log_line("ENROLL: failed to append users file");
        rc = -115;
        goto cleanup;
    }

    log_line("ENROLL OK id=%d name=%s", new_id, name);
    printf("ENROLL OK: id=%d name=%s\n", new_id, name);
    fflush(stdout);

cleanup:
    /* pusti mutex i rilisuj thread time i iskljuci flag za pauuzu */
    pthread_mutex_unlock(&fp_m);
    g_pause_scanner = 0;
    return rc;
}


/* ispisi supportovane komande */
static void print_help(void) {
    printf("Commands:\n");
    printf("  help\n");
    printf("  list\n");
    printf("  add <PIN> <Name>\n");
    printf("  unlock <PIN>\n");
    printf("  exit\n");
}


/* ispisi trenutne accesible ID-jeve */
static void cmd_list_users(void) {
    FILE* f = fopen(USERS_FILE, "r");
    if (!f) { printf("Cannot open %s\n", USERS_FILE); return; }
    char line[256];
    printf("Users:\n");
    while (fgets(line, sizeof line, f)) fputs(line, stdout);
    fclose(f);
}



int main(void) {
   
    signal(SIGINT, on_sigint);

    /* daj pristup uartu senzoru i napravi instancu*/
    int fd = uart_open(SERIAL_PORT, BAUDRATE);
    if (fd < 0) { printf("UART open failed (%s)\n", SERIAL_PORT); return 1; }

    /* inicijalizuj model senzora */
    fp_dev_t fp;
    if (fp_init(&fp, fd, SENSOR_SID, SENSOR_DID) != 0) {
        printf("Fingerprint init failed\n");
        uart_close(fd);
        return 1;
    }

    
    gate_init();

    scan_ctx_t sctx = { .fp = fp };
    pthread_t ts;
    /* startuj skener tred */
    pthread_create(&ts, NULL, scanner_thread, &sctx);

    log_line("PIN thread started");
    print_help();

    char line[256];

    while (!g_stop) {
        printf("Command (unlock/add/help/list/exit): ");
        fflush(stdout);
        if (!fgets(line, sizeof line, stdin)) break;

        size_t n = strlen(line);
        while (n && (line[n-1] == '\n' || line[n-1] == '\r')) line[--n] = 0;
        if (n == 0) continue;

        char* cmd = strtok(line, " ");
        if (!cmd) continue;

        if (strcmp(cmd, "help") == 0) {
            print_help();
        } else if (strcmp(cmd, "list") == 0) {
            cmd_list_users();
        } else if (strcmp(cmd, "exit") == 0) {
            g_stop = 1;
        } else if (strcmp(cmd, "unlock") == 0) {
            char* pin = strtok(NULL, " ");
            if (!pin_ok(pin)) { log_line("PIN invalid (cmd=unlock)"); printf("PIN invalid\n"); continue; }

            int id; time_t ts_last;
            if (!get_last(&id, &ts_last)) {
                printf("No recent scan. Scan finger first.\n");
                continue;
            }
            time_t now = time(NULL);
            if (now - ts_last > AUTH_WINDOW_SECONDS) {
                printf("Last scan too old. Scan again.\n");
                continue;
            }

            char name[128] = {0}, role[64] = {0};
            if (userdb_has_id(USERS_FILE, id, name, sizeof name, role, sizeof role)) {
                log_line("ACCESS GRANTED id=%d name=%s role=%s", id, name, role);
                printf("ACCESS GRANTED id=%d name=%s role=%s\n", id, name, role);
                gate_open_ms(GATE_OPEN_MS);
            } else {
                log_line("ACCESS DENIED id=%d (not in %s)", id, USERS_FILE);
                printf("ACCESS DENIED id=%d\n", id);
            }
        } else if (strcmp(cmd, "add") == 0) {
            char* pin = strtok(NULL, " ");
            char* name = strtok(NULL, "");
            if (!pin_ok(pin)) { log_line("PIN invalid (cmd=add)"); printf("PIN invalid\n"); continue; }
            if (!name || !*name) { printf("Usage: add <PIN> <Name>\n"); continue; }

            int r = do_enroll(&fp, name);
            if (r != 0) {
                log_line("ENROLL FAILED: error=%d", r);
                printf("ENROLL FAILED: error=%d (see %s)\n", r, LOG_FILE);
            }
        } else {
            printf("Unknown command. Type 'help'.\n");
        }
    }

    log_line("Stopped");
    gate_close();
    gate_cleanup();
    uart_close(fd);
    return 0;
}
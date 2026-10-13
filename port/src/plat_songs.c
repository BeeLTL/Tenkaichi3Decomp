/* The manifest of songs added to the music select (see plat_songs.h). Runs at start-up: it gives the game the
   added tracks' list offsets and names, and the file layer where their ADX files live. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "plat_songs.h"

char gPortSongNames[PORT_SONG_MAX][64];
int gPortSongCount;
int gPortSongOffsets[PORT_SONG_MAX];

typedef struct SongAlias {
    char rel[48];     /* the path the game asks for, under the data root */
    char target[192]; /* where it is served from */
} SongAlias;

static SongAlias sAliases[PORT_SONG_MAX];
static int sAliasCount;
static int sDone;

static const char *data_root(void) {
    const char *r = getenv("BT3_DATA");
    return r != NULL ? r : "gamedata";
}

static char *trim(char *s) {
    char *e;
    while (*s == ' ' || *s == '\t') {
        s++;
    }
    e = s + strlen(s);
    while (e > s && (e[-1] == ' ' || e[-1] == '\t' || e[-1] == '\r' || e[-1] == '\n')) {
        *--e = '\0';
    }
    return s;
}

static char sDir[256]; /* the folder the songs are in (plat_extras.c) */
static char sFiles[PORT_SONG_MAX][128];

extern int PortExtras_Dir(const char *kind, const char *env, char *out, unsigned size);
extern int PortExtras_Scan(const char *dir, const char *ext, char names[][128], int max);
extern void PortExtras_NameFromFile(const char *file, char *name, unsigned size);
extern void PortSongs_ConvertMp3(const char *dir); /* gs/song_mp3.c: the folder's MP3 files, written as ADX */

void PortSongs_Init(void);

const char *PortSongs_Dir(void) {
    PortSongs_Init(); /* (the overlay may ask before the game has started) */
    return sDir;
}

/* Adds one song: its place in the music list, its name, and the file the game's id for it stands for. */
static void add_song(const char *file, const char *name) {
    int i, offset, index;

    for (i = 0; i < gPortSongCount; i++) {
        if (strcmp(sFiles[i], file) == 0) {
            return;
        }
    }
    if (gPortSongCount >= PORT_SONG_MAX) {
        fprintf(stderr, "bt3: songs: more than %d added, %s is left out\n", PORT_SONG_MAX, file);
        return;
    }
    i = gPortSongCount;
    snprintf(sFiles[i], sizeof(sFiles[i]), "%s", file);
    offset = PORT_SONG_FIRST_OFFSET + i;
    index = PORT_SONG_FILE_INDEX + i; /* PORT_BGM_FILE(offset), as an index of the second archive */
    gPortSongOffsets[i] = offset;
    snprintf(gPortSongNames[i], 64, "%s", name);
    snprintf(sAliases[sAliasCount].rel, sizeof(sAliases[sAliasCount].rel), "pzs3us2/%05d.bin", index);
    snprintf(sAliases[sAliasCount].target, sizeof(sAliases[sAliasCount].target), "%s/%s", sDir, file);
    sAliasCount++;
    gPortSongCount++;
}

void PortSongs_Init(void) {
    char path[512];
    char line[256];
    FILE *fp;
    int i;

    if (sDone) {
        return;
    }
    sDone = 1;
    if (!PortExtras_Dir("songs", "BT3_SONGS", sDir, sizeof(sDir))) {
        return;
    }
    PortSongs_ConvertMp3(sDir);
    /* the list first (its names and its order), then whatever else is in the folder, by name */
    snprintf(path, sizeof(path), "%s/songs.txt", sDir);
    fp = fopen(path, "rb");
    while (fp != NULL && fgets(line, sizeof(line), fp) != NULL) {
        char *bar;
        if (line[0] == '\0' || line[0] == '#' || line[0] == '\n' || line[0] == '\r') {
            continue;
        }
        bar = strchr(line, '|');
        if (bar == NULL) {
            continue;
        }
        *bar = '\0';
        {
            /* a list line may name the MP3: its ADX is the file, and none means the MP3 could not be converted */
            char *file = trim(line), adx[160];
            size_t l = strlen(file);
            if (l > 4 && l < sizeof(adx) && strcmp(file + l - 4, ".mp3") * strcmp(file + l - 4, ".MP3") == 0) {
                FILE *f;
                snprintf(adx, sizeof(adx), "%.*s.adx", (int)(l - 4), file);
                snprintf(path, sizeof(path), "%s/%s", sDir, adx);
                if ((f = fopen(path, "rb")) == NULL) {
                    continue;
                }
                fclose(f);
                file = adx;
            }
            add_song(file, trim(bar + 1));
        }
    }
    if (fp != NULL) {
        fclose(fp);
    }
    {
        static char found[64][128];
        int n = PortExtras_Scan(sDir, ".adx", found, 64), k;
        for (k = 0; k < n; k++) {
            char name[64];
            PortExtras_NameFromFile(found[k], name, sizeof(name));
            add_song(found[k], name);
        }
    }
    if (gPortSongCount > 0) {
        fprintf(stderr, "bt3: songs: %d added from %s\n", gPortSongCount, sDir);
    }
}

const char *PortSongs_Name(int index) {
    return index >= 0 && index < gPortSongCount ? gPortSongNames[index] : "";
}

int PortSongs_Count(void) {
    return gPortSongCount;
}

int PortSongs_Offset(int index) {
    if (index < 0 || index >= gPortSongCount) {
        return 0;
    }
    return gPortSongOffsets[index];
}

/* Whether `path` is the file of a song added from outside the disc (for the sound code: such a file is played
   round and round like the disc's music, though it has no loop points of its own). */
int PortSongs_IsFile(const char *path) {
    size_t pl = strlen(path), tl;
    int i;
    for (i = 0; i < sAliasCount; i++) {
        tl = strlen(sAliases[i].target);
        if (tl != 0 && tl <= pl && strcmp(path + pl - tl, sAliases[i].target) == 0) {
            return 1;
        }
    }
    return 0;
}

int PortSongs_Alias(const char *rel, char *out, unsigned n) {
    int i;
    for (i = 0; i < sAliasCount; i++) {
        if (strcmp(sAliases[i].rel, rel) == 0) {
            snprintf(out, n, "%s", sAliases[i].target);
            return 1;
        }
    }
    return 0;
}

/* BT3_TEST_BGM=<entry>: every battle is set up with that entry of the music list, whatever the menus chose
   (testing: 0x1A is the first added song). */
int Port_TestBgm(void) {
    const char *e = getenv("BT3_TEST_BGM");
    return e != NULL && e[0] != '\0' ? (int)strtol(e, NULL, 0) : -1;
}

// Maintained by Simonas Kazlauskas, 2012.
//
// Original version written by John Lindgren11, 2011.
// It can be found on http://e4rat-l.bananarocker.org/
//
// Replacement for e4rat-preload, which was written by Andreas Rid, 2011.
#include <errno.h>
#include <fcntl.h>
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#define VERBOSE 0
#define LIST "/var/lib/e4rat/startup.log"
#define INIT "/bin/systemd"
#define MAX_EARLY 1000
#define BLOCK 100
#define BUF 1048576 // = 1 MiB

// Some systems may not declare strdup in string.h.
char* strdup(const char* source);

static void die(const char *msg){
    printf("Error: %s.\n", msg);
    exit(EXIT_FAILURE);
}

typedef struct {
    int dev;
    uint64_t inode;
    char *path;
} FileDesc;

static FileDesc **sorted = 0;
static int listlen = 0;

// qsort helper for file list entries. Sorts by device and inode.
static int sort_cb(const void *_a, const void *_b){
    FileDesc *a = *(FileDesc **)_a;
    FileDesc *b = *(FileDesc **)_b;

    if(a->dev < b->dev){
        return -1;
    }
    if(a->dev > b->dev){
        return 1;
    }
    if(a->inode < b->inode){
        return -1;
    }
    if(a->inode > b->inode){
        return 1;
    }
    return 0;
}

static void free_sorted_file_list() {
    for (int i = 0; i < listlen; i++) {
        free(sorted[i]->path);
	free(sorted[i]);
    }
    free(sorted);
    sorted = NULL;
}

// Parses a line from the file list. Returns NULL in case of parse errors.
// Expected format of the line:
//     (device) (inode) (path)
// Example:
//     2049 2223875 /bin/bash
static FileDesc *parse_line(const char *line){
    int dev = 0;

    while(*line >= '0' && *line <= '9'){
        dev = dev * 10 + (*line++ - '0');
    }
    if(*line++ != ' '){
        return NULL;
    }

    uint64_t inode = 0;
    while(*line >= '0' && *line <= '9'){
        inode = inode * 10 + ((*line++) - '0');
    }
    if(*line++ != ' '){
        return NULL;
    }

    FileDesc *f = malloc(sizeof(FileDesc));
    if (!f){
        die("Failed to allocate memory while parsing file list!");
    }
    f->dev = dev;
    f->inode = inode;
    f->path = strdup(line);
    if (!f->path){
        die("Failed to allocate memory for file path");
    }

    return f;
}

// Loads the file list and sorts it by device and inode.
static void load_list(void){
    #if VERBOSE > 0
        printf("Loading %s.\n", LIST);
    #endif

    FileDesc **list = 0;
    FILE *stream = fopen(LIST, "r");
    int listsize = 0;
    if (!stream) {
        die(strerror(errno));
    }

    while(1){
        char buf[512];
        if(!fgets(buf, sizeof buf, stream)){
            break;
        }
        if(buf[0] && buf[strlen(buf) - 1] == '\n'){
            buf[strlen (buf) - 1] = 0;
        }
        FileDesc *f = parse_line(buf);
        if(!f){
            continue;
        }
        if(listlen >= listsize){
            listsize = listsize ? listsize * 2 : 256;
            list = realloc(list, sizeof(FileDesc *) * listsize);
        }
        list[listlen++] = f;
    }

    fclose(stream);

    sorted = malloc(sizeof(FileDesc *) * listlen);
    if (!list || !sorted){
        die("Failed to allocate memory while loading file list");
    }
    memcpy(sorted, list, sizeof(FileDesc *) * listlen);
    qsort(sorted, listlen, sizeof(FileDesc *), sort_cb);
    free(list);
}

static void load_inodes(const int a, const int b){
    struct stat s;
    for(int i = a; i < ((b < listlen) ? b : listlen); i++){
        stat(sorted[i]->path, &s);
    }
}

static void exec_init(char **argv){
    #if VERBOSE > 0
        printf("Executing %s.\n", INIT);
    #endif

    switch(fork()){
        case -1:
            die(strerror(errno));
        case 0:
            return;
        default:
            free_sorted_file_list();
            execv(INIT, argv);
            die(strerror(errno));
    }
}

static void load_files(int a, int b){
    void *buf = malloc(BUF);
    if(!buf){
        die("Failed to allocate preload buffer");
    }
    for(int i = a; i < b && i < listlen; i ++){
        int handle = open(sorted[i]->path, O_RDONLY);
        if(handle < 0){
            continue;
        }
        while(read(handle, buf, BUF) > 0){}
        close(handle);
    }
    free(buf);
}

int main(int argc, char **argv){
    (void) argc;
    int early_load = 0;

    load_list();
    #if VERBOSE > 0
        printf("Preloading %d files.\n", listlen);
    #endif

    // Preload a third of the list or MAX_EARLY files (whichever is
    // smaller), then start init.
    early_load = listlen * 0.33;
    if(early_load > MAX_EARLY){
        early_load = MAX_EARLY;
    }

    load_inodes(0, early_load);
    load_files(0, early_load);
    exec_init(argv);

    // After init starts, load the files in chunks of size BLOCK.
    for(int i = early_load; i < listlen; i += BLOCK){
        load_inodes(i, i + BLOCK);
        load_files(i, i + BLOCK);
    }

    free_sorted_file_list();
    exit(EXIT_SUCCESS);
}

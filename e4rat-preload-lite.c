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

typedef struct {
    int dev;
    uint64_t inode;
    char *path;
} FileDesc;

static FileDesc **list = NULL;
static FileDesc **sorted = NULL;
static int listlen = 0;

#ifndef strdup
char *strdup(const char *source);
#endif

static int sort_cb(const void *_a, const void *_b){
    // qsort helper for file list entries. Sorts by device and inode.
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


static void die(const char *msg){
    printf("Error: %s.\n", msg);
    exit(EXIT_FAILURE);
}


static void free_list(FileDesc **list){
    for (int i = 0; i < listlen; i++) {
        free(list[i]->path);
        free(list[i]);
    }
    free(list);
    list = NULL;
}


static FileDesc *parse_line(const char *line){
    // Parses a line from the file list. Returns NULL in case of parse errors.
    // Expected format of the line:
    //     (device) (inode) (path)
    // Example:
    //     2049 2223875 /bin/bash
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
    if(!f) die("Failed to allocate memory while parsing file list!");
    f->dev = dev;
    f->inode = inode;
    f->path = strdup(line);
    if(!f->path) die("Failed to allocate memory for file path");

    return f;
}


static void load_list(void){
    // Loads list and parses contents into FileDescs
    #if VERBOSE > 0
        printf("Loading %s.\n", LIST);
    #endif

    FILE *stream = fopen(LIST, "r");
    if (!stream) die(strerror(errno));

    int listsize = 0;
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

    list = realloc(list, sizeof(FileDesc *) * listlen);
    sorted = malloc(sizeof(FileDesc *) * listlen);
    if(!list || !sorted) die("Could not allocate memory for lists");
    memcpy(sorted, list, sizeof(FileDesc *) * listlen);
    qsort(sorted, listlen, sizeof(FileDesc *), sort_cb);
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
            execv(INIT, argv);
            die(strerror(errno));
    }
}


static void load_files(int a, int b){
    void *buf = malloc(BUF);
    if(!buf) die("Failed to allocate preload buffer");

    for(int i = a; i < b && i < listlen; i ++){
        int handle = open(list[i]->path, O_RDONLY);
        if(handle < 0){
            continue;
        }
        while(read(handle, buf, BUF) > 0){}
        close(handle);
    }
    free(buf);
}


int main(int argc, char **argv){
    int early_load = 0;

    load_list();
    #if VERBOSE > 0
        printf("Preloading %d files.\n", listlen);
    #endif

    early_load = listlen * 0.33;
    if(early_load > MAX_EARLY) early_load = MAX_EARLY;

    // Preload a third of the list or MAX_EARLY files (whichever is smaller),
    // then start init.
    load_inodes(0, early_load);
    load_files(0, early_load);
    exec_init(argv);

    // And continue preloading files further in chunks of BLOCK files.
    for(int i = early_load; i < listlen; i += BLOCK){
        load_inodes(i, i + BLOCK);
        load_files(i, i + BLOCK);
    }

    free_list(list);
    // As sorted originally was a copy of list, it's contents are already
    // freed by the time we are freeing sorted list
    free(sorted);
    exit(EXIT_SUCCESS);
}

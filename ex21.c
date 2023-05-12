
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <ctype.h>

#define CACHE_SIZE 1024
#define ARG_COUNT  3
#define ERROR      -1
#define SUCCESS    0

typedef struct {
    int fd;
    char cache[CACHE_SIZE];
    unsigned int pos;
    unsigned int cache_len;
} OpenFile;

typedef enum {
    FALSE = 0,
    TRUE
} bool;

typedef enum {
    IDENTICAL = 1,
    DIFFERENT,
    SIMILAR
} CompareStatus;

int reset_file(OpenFile *file);
int open_file(OpenFile *file, char *file_path);
bool read_from_file(OpenFile *file, char *ch);
CompareStatus compare_files(OpenFile *file_1, OpenFile *file_2);
bool are_identical(OpenFile *file_1, OpenFile *file_2);
bool are_similar(OpenFile *file_1, OpenFile *file_2);
char upper(char ch);

int main(int argc, char *argv[]) {
    if (argc != ARG_COUNT) {
        return ERROR;
    }

    char *file_path_1 = argv[1];
    char *file_path_2 = argv[2];

    OpenFile file_1, file_2;
    if (open_file(&file_1, file_path_1) == ERROR) {
        return ERROR;
    }
    if (open_file(&file_2, file_path_2) == ERROR) {
        close(file_1.fd);
        return ERROR;
    }

    CompareStatus result = compare_files(&file_1, &file_2);
    close(file_1.fd);
    close(file_2.fd);
    return result;
}

bool are_similar(OpenFile *file_1, OpenFile *file_2) {
    char ch1, ch2;
    int reachedEOF1 = FALSE;
    int reachedEOF2 = FALSE;
    while (!reachedEOF1 && !reachedEOF2) {
        // ignore spaces
        do {
            reachedEOF1 = !read_from_file(file_1, &ch1);
        } while (!reachedEOF1 && isspace(ch1));
        
        do {
            reachedEOF2 = !read_from_file(file_2, &ch2);
        } while (!reachedEOF2 && isspace(ch2));

        // if reached end-of-file with both files, then they are similar
        if (reachedEOF1 && reachedEOF2) {
            return TRUE;
        }

        // if reached end-of-file in only one file, then they are not similar
        if (reachedEOF1 != reachedEOF2) {
            return FALSE;
        }

        // ignore casing
        if (toupper(ch1) != toupper(ch2)) {
            return FALSE;
        }
    }
    
    return TRUE;
}

bool are_identical(OpenFile *file_1, OpenFile *file_2) {
    char ch1, ch2;
    int reachedEOF1 = FALSE;
    int reachedEOF2 = FALSE;
    while (!reachedEOF1 && !reachedEOF2) {
        reachedEOF1 = !read_from_file(file_1, &ch1);
        reachedEOF2 = !read_from_file(file_2, &ch2);
        if (reachedEOF1 != reachedEOF2) {
            return FALSE;
        }
        if (ch1 != ch2) {
            return FALSE;
        }
    }
    return TRUE;
}

int open_file(OpenFile *file, char *file_path) {
    int fd = open(file_path, O_RDONLY);
    if (fd == ERROR) {
        return ERROR;
    }

    file->fd = fd;
    file->pos = 0;
    file->cache_len = 0;

    return SUCCESS;
}

// returns FALSE if reached end-of-file
bool read_from_file(OpenFile *file, char *ch) {
    // did we reach all of the characters in our cache?
    if (file->pos >= file->cache_len) {
        // read up to CACHE_SIZE characters into cache
        ssize_t byte_count = read(file->fd, file->cache, CACHE_SIZE);
        if (byte_count == 0) {
            // reached end-of-file
            return FALSE;
        }

        // reset cache pointer and update len
        file->pos = 0;
        file->cache_len = byte_count;
    }

    // read a character into 'ch' from our cache
    *ch = file->cache[file->pos];
    // advance to the next character in the cache
    file->pos++;

    return TRUE;
}

int reset_file(OpenFile *file) {
    // reset file pointers back to start
    if (lseek(file->fd, 0, SEEK_SET) == ERROR) {
        return ERROR;
    }
    file->cache_len = 0;
    file->pos = 0;
}

CompareStatus compare_files(OpenFile *file_1, OpenFile *file_2) {
    if (are_identical(file_1, file_2)) {
        return IDENTICAL;
    }

    if (reset_file(file_1) == ERROR || reset_file(file_2) == ERROR) {
        return ERROR;
    }

    if (are_similar(file_1, file_2)) {
        return SIMILAR;
    }
    
    return DIFFERENT;
}

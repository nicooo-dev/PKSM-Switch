#include <stdio.h>
#include <stdlib.h>

extern "C" ssize_t pksmcore_getline(char** lineptr, size_t* n, FILE* stream) {
    if (!lineptr || !n || !stream) {
        return -1;
    }

    if (*lineptr == NULL || *n == 0) {
        *n = 128;
        *lineptr = static_cast<char*>(malloc(*n));
        if (*lineptr == NULL) {
            *n = 0;
            return -1;
        }
    }

    size_t pos = 0;
    int c;
    while ((c = fgetc(stream)) != EOF) {
        // Ensure space for char + null terminator
        if (pos + 1 >= *n) {
            size_t newSize = (*n) * 2;
            if (newSize < (*n) + 128) {
                newSize = (*n) + 128;
            }
            char* newBuf = static_cast<char*>(realloc(*lineptr, newSize));
            if (newBuf == NULL) {
                return -1;
            }
            *lineptr = newBuf;
            *n = newSize;
        }

        (*lineptr)[pos++] = static_cast<char>(c);
        if (c == '\n') {
            break;
        }
    }

    if (pos == 0 && c == EOF) {
        return -1;
    }

    (*lineptr)[pos] = '\0';
    return static_cast<ssize_t>(pos);
}

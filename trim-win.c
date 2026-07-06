/*  A program to trims .mca files to remove unwanted chunks
    Copyright (C) 2026 Anonymous1212144

    This program is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program.  If not, see <https://www.gnu.org/licenses/>.
*/

#include <stdlib.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <threads.h>
#include <Windows.h>

typedef struct
{
    cnd_t notify;
    mtx_t q_lock;
    mtx_t r_lock;
    HANDLE findh;
    uint_fast64_t *chunks;
    char *head;
    size_t capacity;
    size_t index;
    size_t count;
    size_t len_chunks;
    uint_fast8_t done;
} Globals;

// Compare 2 ull, used by qsort
int ullcomp(const void *a, const void *b)
{
    const uint_fast64_t x = *(const uint_fast64_t *)a;
    const uint_fast64_t y = *(const uint_fast64_t *)b;
    return (x > y) - (x < y);
}

// Print the 2 error messages and exit
void handle_error(const char *prompt)
{
    perror(prompt);
    getchar();
    exit(EXIT_FAILURE);
}

// Print the given error message and exit
void exit_error(const char *prompt)
{
    puts(prompt);
    getchar();
    exit(EXIT_FAILURE);
}

// Find the last chunk that should be kept for the region file
const uint_fast64_t *bsearch_c2(
    const uint_fast64_t *const chunks,
    const uint_fast64_t target,
    size_t i0,
    size_t i1)
{
    while (i1 - i0 > 1)
    {
        const size_t j = (i0 + i1) >> 1;
        if ((chunks[j] & 0xFFFFFFFFFFFFFC00) > target)
            i1 = j;
        else
            i0 = j;
    }
    return &(chunks[i1]);
}

// Find the first chunk and call above function that should be kept for the region file
const uint_fast64_t *bsearch_c(
    const uint_fast64_t *const chunks,
    const uint_fast64_t target,
    size_t i0,
    size_t i1,
    const size_t **const top)
{
    while (1)
    {
        const size_t j = (i0 + i1) >> 1;
        uint_fast64_t x = chunks[j] & 0xFFFFFFFFFFFFFC00;
        if (x < target)
        {
            if (j == i0)
                break;
            i0 = j;
        }
        else
        {
            if (x == target && !*top)
                *top = bsearch_c2(chunks, target, j, i1);
            if (j == i1)
                break;
            i1 = j;
        }
    }
    return &(chunks[i1]);
}

// Function for each thread. The threads self organize into roles they are needed
int thread_func(void *arg)
{
    Globals *const globals = (Globals *)arg;
    uint8_t *buffer = NULL;
    size_t buffer_size = 0;
    uint_fast8_t done = 0;
    while (1)
    {
        mtx_lock(&globals->q_lock);
        while (1)
        {
            if (globals->count << 1 < globals->capacity)
            {
                if (!done)
                {
                    if (mtx_trylock(&globals->r_lock) == thrd_success)
                    {
                        if (globals->done)
                        {
                            mtx_unlock(&globals->r_lock);
                            done = 1;
                        }
                        else
                        {
                            // The reader role. Looks through dictionary to find file names and populate queue
                            mtx_unlock(&(globals->q_lock));
                            uint_fast8_t working;
                            while (1) {
                                WIN32_FIND_DATAA fileData;
                                if (!FindNextFileA(globals->findh, &fileData))
                                {
                                    globals->done = 1;
                                    mtx_unlock(&(globals->r_lock));
                                    mtx_lock(&globals->q_lock);
                                    cnd_broadcast(&globals->notify);
                                    done = 1;
                                    break;
                                }
                                mtx_lock(&globals->q_lock);
                                size_t index = (globals->index + globals->count) % globals->capacity;
                                strncpy(globals->head + (index << 6), fileData.cFileName, 64);
                                working = (++globals->count < globals->capacity);
                                cnd_signal(&globals->notify);
                                if (!working) {
                                    mtx_unlock(&(globals->r_lock));
                                    break;
                                }
                                mtx_unlock(&globals->q_lock);
                            }
                            continue;
                        }
                    }
                    else
                    {
                        if (globals->count)
                            break;
                        else
                        {
                            cnd_wait(&globals->notify, &globals->q_lock);
                            continue;
                        }
                    }
                }
                if (globals->count)
                    break;
                else
                {
                    mtx_unlock(&(globals->q_lock));
                    free(buffer);
                    return 0;
                }
            }
            else
                break;
        }

        // Process file role. Take names from queue and process the file
        char name[65];
        strncpy(name, globals->head + (globals->index << 6), 64);
        globals->index = (globals->index + 1) % globals->capacity;
        globals->count--;
        mtx_unlock(&globals->q_lock);

        name[64] = 0;
        char *ni = name;
        if (*ni++ != 'r' || *ni++ != '.')
            continue;

        int n = *ni++;
        uint_fast8_t neg;
        if (n == '-')
        {
            neg = 1;
            n = *ni++;
        }
        else
            neg = 0;

        if (n < '0' || n > '9')
            continue;

        uint_fast64_t rx = n - '0';

        const char *ne = name + 64;
        while (ni < ne)
        {
            n = *ni++;
            if (n < '0' || n > '9')
                break;
            rx = rx * 10 + n - '0';
        }
        if (n != '.' || ni == ne)
            continue;
        if (neg)
            rx *= -1;

        n = *ni++;
        if (n == '-')
        {
            neg = 1;
            n = *ni++;
        }
        else
            neg = 0;

        if (n < '0' || n > '9')
            continue;

        uint_fast64_t rz = n - '0';

        while (ni < ne)
        {
            n = *ni++;
            if (n < '0' || n > '9')
                break;
            rz = rz * 10 + n - '0';
        }

        if (ni + 3 >= ne || n != '.' || *ni++ != 'm' || *ni++ != 'c' || *ni++ != 'a' || *ni)
            continue;
        if (neg)
            rz *= -1;

        const uint_fast64_t chunk = rx << 37 | (rz & 0x7FFFFFF) << 10;
        const uint_fast64_t *end = NULL;
        const uint_fast64_t *const start = bsearch_c(globals->chunks, chunk, 0, globals->len_chunks, &end);

        if (!end)
        {
            remove(name);
            continue;
        }

        const uint_fast16_t elements = end - start;
        uint_fast64_t *const c_keep = (uint_fast64_t *)malloc(elements * sizeof(uint_fast64_t));
        if (!c_keep)
            handle_error("Error while processing region files");

        FILE *file;
        file = fopen(name, "rb+");

        if (file == NULL)
            handle_error("Error opening region files");

        fseek(file, 0, SEEK_END);
        const size_t outlen = ftell(file);
        if (outlen < 8192)
        {
            fclose(file);
            free(c_keep);
            continue;
        }

        rewind(file);
        uint8_t header[4096];
        fread(header, 1, 4096, file);

        size_t p = 0;
        uint_fast64_t *c_keep_ptr = c_keep;
        const uint_fast64_t *c_ptr = start;
        for (uint_fast16_t i = 0; i < elements; i++)
        {
            const size_t j = (*c_ptr++ & 1023) << 2;
            memset(header + p, 0, j - p);

            // 24 8 12 - offset, size, index
            *c_keep_ptr++ = (uint_fast64_t)header[j] << 36 |
                            (uint_fast64_t)header[j | 1] << 28 |
                            (uint_fast64_t)header[j | 2] << 20 |
                            (uint_fast64_t)header[j | 3] << 12 |
                            (j & 0xFFF);
            p = j + 4;
        }
        memset(header + p, 0, 4096 - p);
        qsort(c_keep, elements, sizeof(uint_fast64_t), ullcomp);

        c_keep_ptr = c_keep;
        fseek(file, 4096, SEEK_CUR);
        for (uint_fast16_t i = 0; i < elements; i++)
        {
            const long f_pos = ftell(file);
            const long diff = ((*c_keep_ptr & 0xFFFFFF00000) >> 8) - f_pos;
            const size_t len = *c_keep_ptr & 0xFF000;
            const size_t h_index = *c_keep_ptr & 0xFFF;
            c_keep_ptr++;
            if (!len)
                continue;
            if (!diff)
            {
                fseek(file, len, SEEK_CUR);
                continue;
            }
            if (len > buffer_size)
            {

                buffer = (uint8_t *)realloc(buffer, len);
                if (!buffer)
                    handle_error("Error compacting region files");
                buffer_size = len;
            }
            header[h_index] = f_pos >> 28;
            header[h_index | 1] = f_pos >> 20;
            header[h_index | 2] = f_pos >> 12;

            fseek(file, diff, SEEK_CUR);
            fread(buffer, 1, len, file);
            fseek(file, f_pos, SEEK_SET);
            fwrite(buffer, 1, len, file);
        }
        long file_end = ftell(file);
        fseek(file, 0, SEEK_SET);
        fwrite(header, 1, 4096, file);
        free(c_keep);
        fclose(file);

        HANDLE handle = CreateFileA(name, GENERIC_WRITE, 0, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
        if (handle == INVALID_HANDLE_VALUE)
            exit_error("Error truncating region files");
        SetFilePointer(handle, file_end, NULL, FILE_BEGIN);
        SetEndOfFile(handle);
        CloseHandle(handle);
    }
    free(buffer);
    return 0;
}

// Initialize the queue
void init_queue(Globals *const globals, const size_t capacity)
{
    cnd_init(&globals->notify);
    mtx_init(&globals->q_lock, mtx_plain);
    mtx_init(&globals->r_lock, mtx_plain);
    globals->head = (char *)malloc(capacity << 6);
    globals->capacity = capacity;
    globals->index = 0;
    globals->count = 0;
    globals->done = 0;
}

int main(int argc, char *argv[])
{
    size_t num_threads = 2;
    const char *file_name = "chunks.txt";

    if (argc > 1)
        num_threads = strtoull(argv[1], NULL, 0);
    if (!num_threads)
        exit_error("Error: Invalid thread count");
    if (argc > 2)
        file_name = argv[2];

    Globals *const globals = (Globals *)malloc(sizeof(Globals));
    if (globals == NULL)
        handle_error("Error initializing variables");

    FILE *file;
    file = fopen(file_name, "rb");

    if (file == NULL)
        handle_error("Error opening input file");

    fseek(file, 0, SEEK_END);
    const size_t outlen = ftell(file);
    rewind(file);

    char *data = (char *)malloc(outlen + 1);
    if (data == NULL)
        handle_error("Error loading input file");

    fread(data, 1, outlen, file);
    fclose(file);

    data[outlen] = 0;
    globals->chunks = NULL;
    globals->len_chunks = 0;
    const char *index = data;
    for (size_t i = 0;; i++)
    {
        char *next;

        const uint_fast64_t num1 = strtoull(index, &next, 0);
        if (next == index)
        {
            if (i == 0)
                exit_error("Error: Bad input file");
            globals->chunks = (uint_fast64_t *)realloc(globals->chunks, i * sizeof(uint_fast64_t));
            if (globals->chunks == NULL)
                handle_error("Error parsing input file");
            globals->len_chunks = i;
            qsort(globals->chunks, globals->len_chunks, sizeof(uint_fast64_t), ullcomp);
            break;
        }
        index = next;

        const uint_fast64_t num2 = strtoull(index, &next, 0);
        if (next == index)
            exit_error("Error: Bad input file");
        index = next;

        if (i >= globals->len_chunks)
        {
            globals->len_chunks = globals->len_chunks << 1 | 1;
            globals->chunks = (uint_fast64_t *)realloc(globals->chunks, globals->len_chunks * sizeof(uint_fast64_t));
            if (globals->chunks == NULL)
                handle_error("Error parsing input file");
        }

        // 27 27 5 5 - region x, z coordinate; chunk z, x region coordinate
        globals->chunks[i] = (num1 & 0xFFFFFFE0) << 32 |
                             (num2 & 0xFFFFFFE0) << 5 |
                             (num2 & 31) << 5 |
                             (num1 & 31);
    }
    free(data);

    init_queue(globals, num_threads << 4);

    {
        WIN32_FIND_DATAA fileData;
        globals->findh = FindFirstFileA("*", &fileData);
        if (globals->findh == INVALID_HANDLE_VALUE)
            handle_error("Error opening folder");
        size_t index = (globals->index + globals->count) % globals->capacity;
        strncpy(globals->head + (index << 6), fileData.cFileName, 64);
    }

    if (--num_threads)
    {
        thrd_t *t = (thrd_t *)malloc(num_threads * sizeof(thrd_t));
        for (size_t i = 0; i < num_threads; i++)
            thrd_create(&t[i], thread_func, globals);
        thread_func(globals);
        for (size_t i = 0; i < num_threads; i++)
            thrd_join(t[i], NULL);
        free(t);
    }
    else
        thread_func(globals);

    free(globals->head);
    free(globals->chunks);
    free(globals);
    return 0;
}

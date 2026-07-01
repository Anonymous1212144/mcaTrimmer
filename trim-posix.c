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

#include <dirent.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <threads.h>
#include <unistd.h>

typedef struct
{
    cnd_t notify;
    mtx_t q_lock;
    mtx_t r_lock;
    char *head;
    size_t capacity;
    size_t index;
    size_t count;
    uint_fast8_t done;
} Queue;

const size_t name_len = sizeof(((struct dirent *)0)->d_name);
const size_t c_size = sizeof(uint_fast64_t);

uint_fast64_t *chunks = NULL;
size_t len_chunks = 0;

Queue queue;
DIR *folder;

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
uint_fast64_t *bsearch_c2(const uint_fast64_t target, size_t i0, size_t i1)
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
uint_fast64_t *bsearch_c(const uint_fast64_t target, const size_t **const top)
{
    size_t i0 = 0;
    size_t i1 = len_chunks;
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
                *top = bsearch_c2(target, j, i1);
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
    (void)arg;
    while (1)
    {
        // The reader role. Looks through dictionary to find file names and populate queue
        if ((queue.count < queue.capacity) && !queue.done && (mtx_trylock(&(queue.r_lock)) == thrd_success))
        {
            uint_fast8_t working;
            do
            {
                struct dirent *f = readdir(folder);
                if (!f)
                {
                    closedir(folder);
                    queue.done = 1;
                    cnd_broadcast(&(queue.notify));
                    break;
                }
                mtx_lock(&(queue.q_lock));
                size_t index = (queue.index + queue.count) % queue.capacity;
                memcpy(queue.head + (index * name_len), f->d_name, name_len);
                working = (++queue.count < queue.capacity);
                cnd_signal(&(queue.notify));
                mtx_unlock(&(queue.q_lock));
            } while (working);
            mtx_unlock(&(queue.r_lock));
        }

        // Process file role. Take names from queue and process the file
        mtx_lock(&(queue.q_lock));

        while (1)
        {
            if (queue.count)
                break;
            if (queue.done)
            {
                mtx_unlock(&(queue.q_lock));
                return 0;
            }
            cnd_wait(&(queue.notify), &(queue.q_lock));
        }

        char name[name_len];
        memcpy(name, queue.head + (queue.index * name_len), name_len);
        queue.index = (queue.index + 1) % queue.capacity;
        queue.count--;
        mtx_unlock(&(queue.q_lock));

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

        const char *ne = name + name_len;
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

        if (ni + 2 >= ne || n != '.' || *ni++ != 'm' || *ni++ != 'c' || *ni != 'a')
            continue;
        if (neg)
            rz *= -1;

        const uint_fast64_t chunk = (rx << 37) | ((rz & 0x7FFFFFF) << 10);
        const uint_fast64_t *end = NULL;
        uint_fast64_t *start = bsearch_c(chunk, &end);

        if (!end)
        {
            remove(name);
            continue;
        }

        const uint_fast16_t elements = end - start;
        uint_fast64_t *const c_keep = (uint_fast64_t *)malloc(elements * c_size);
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
        uint_fast64_t *c_ptr = start;
        for (uint_fast16_t i = 0; i < elements; i++)
        {
            const size_t j = (*(c_ptr++) & 1023) << 2;
            memset(header + p, 0, j - p);

            // 24 8 12 - offset, size, index
            *(c_keep_ptr++) = ((uint_fast64_t)header[j] << 36) |
                              ((uint_fast64_t)header[j | 1] << 28) |
                              ((uint_fast64_t)header[j | 2] << 20) |
                              ((uint_fast64_t)header[j | 3] << 12) |
                              (j & 0xFFF);
            p = j + 4;
        }
        memset(header + p, 0, 4096 - p);
        qsort(c_keep, elements, c_size, ullcomp);

        uint8_t buffer[0xFF000];
        c_keep_ptr = c_keep;
        fseek(file, 4096, SEEK_CUR);
        for (uint_fast16_t i = 0; i < elements; i++)
        {
            const long f_pos = ftell(file);
            const long diff = ((*c_keep_ptr & 0xFFFFFF00000) >> 8) - f_pos;
            const size_t len = (*c_keep_ptr & 0xFF000);
            const size_t h_index = *c_keep_ptr & 0xFFF;
            c_keep_ptr++;
            if (!len)
                continue;
            if (!diff)
            {
                fseek(file, len, SEEK_CUR);
                continue;
            }
            header[h_index] = f_pos >> 28;
            header[h_index | 1] = f_pos >> 20;
            header[h_index | 2] = f_pos >> 12;

            fseek(file, diff, SEEK_CUR);
            fread(buffer, 1, len, file);
            fseek(file, f_pos, SEEK_SET);
            fwrite(buffer, 1, len, file);
        }
        ftruncate(fileno(file), ftell(file));
        fseek(file, 0, SEEK_SET);
        fwrite(header, 1, 4096, file);

        free(c_keep);
        fclose(file);
    }
    return 0;
}

// Initialize the queue
void init_queue(size_t capacity)
{
    cnd_init(&(queue.notify));
    mtx_init(&(queue.q_lock), mtx_plain);
    mtx_init(&(queue.r_lock), mtx_plain);
    queue.head = (char *)malloc(capacity * name_len);
    queue.capacity = capacity;
    queue.index = 0;
    queue.count = 0;
    queue.done = 0;
}

int main(int argc, char *argv[])
{
    size_t num_threads = 2;
    char *file_name = "chunks.txt";

    if (argc > 1)
        num_threads = strtoull(argv[1], NULL, 0);
    if (!num_threads)
        exit_error("Error: Invalid thread count");
    if (argc > 2)
        file_name = argv[2];

    {
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

        const char *index = data;
        for (size_t i = 0;; i++)
        {
            char *next;

            const uint_fast64_t num1 = strtoull(index, &next, 0);
            if (next == index)
            {
                if (i == 0)
                    exit_error("Error: Bad input file");
                chunks = (uint_fast64_t *)realloc(chunks, i * c_size);
                if (chunks == NULL)
                    handle_error("Error parsing input file");
                len_chunks = i;
                qsort(chunks, len_chunks, c_size, ullcomp);
                break;
            }
            index = next;

            const uint_fast64_t num2 = strtoull(index, &next, 0);
            if (next == index)
                exit_error("Error: Bad input file");
            index = next;

            if (i >= len_chunks)
            {
                len_chunks = (len_chunks << 1) | 1;
                chunks = (uint_fast64_t *)realloc(chunks, len_chunks * c_size);
                if (chunks == NULL)
                    handle_error("Error parsing input file");
            }

            // 27 27 5 5 - region x, z coordinate; chunk z, x region coordinate
            chunks[i] = ((num1 & 0xFFFFFFE0) << 32) |
                        ((num2 & 0xFFFFFFE0) << 5) |
                        ((num2 & 31) << 5) |
                        (num1 & 31);
        }
        free(data);
    }

    folder = opendir(".");
    if (folder == NULL)
        handle_error("Error opening folder");

    init_queue(num_threads);

    if (--num_threads)
    {
        thrd_t t[num_threads];
        for (size_t i = 0; i < num_threads; i++)
            thrd_create(&t[i], thread_func, NULL);
        thread_func(NULL);
        for (size_t i = 0; i < num_threads; i++)
            thrd_join(t[i], NULL);
    }
    else
        thread_func(NULL);

    return 0;
}

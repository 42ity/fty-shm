/*  =========================================================================
    benchmark - fty-shm benchmark

    Copyright (C) 2018 Eaton

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License along
    with this program; if not, write to the Free Software Foundation, Inc.,
    51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
    =========================================================================
*/

/*
@header
    benchmark - fty-shm benchmark
@discuss
@end
*/

#include <fty_shm.h>
#include <getopt.h>
#include <iostream>
#include <sys/time.h>

static const char help_text[]
    = "benchmark [options] ...\n"
      "  -d, --directory=DIR   set a custom storage directory for testing\n"
      "  -r, --write           only benchmark writes\n"
      "  -r, --read            only benchmark reads\n"
      "  -h, --help            display this help text and exit\n";

#define NUM_METRICS 10000

#define METRIC_LEN 10
#define METRIC_FMT "m%08d"

#define VALUE_LEN 10
#define VALUE_FMT "v%08d"

static void timestamp(void)
{
    struct timeval tv, tv_diff;
    static struct timeval tv_last;

    gettimeofday(&tv, NULL);
    if (tv.tv_usec < tv_last.tv_usec) {
        tv_diff.tv_usec = tv.tv_usec - tv_last.tv_usec + 1000000;
        tv_diff.tv_sec = tv.tv_sec - tv_last.tv_sec - 1;
    } else {
        tv_diff.tv_usec = tv.tv_usec - tv_last.tv_usec;
        tv_diff.tv_sec = tv.tv_sec - tv_last.tv_sec;
    }
    printf("%ld.%06ld + %ld.%06ld\n", tv.tv_sec, tv.tv_usec,
            tv_diff.tv_sec, tv_diff.tv_usec);
    tv_last = tv;
}

int main(int argc, char **argv)
{
    int i;
    bool do_read = true, do_write = true;

    static struct option long_opts[] = {
        { "help", no_argument, 0, 'h' },
        { "directory", required_argument, 0, 'd' },
        { "write", no_argument, 0, 'w' },
        { "read", no_argument, 0, 'r' }
    };

    int c = 0;
    while (c >= 0) {
        c = getopt_long(argc, argv, "hd:rw", long_opts, 0);

        switch (c) {
        case 'h':
            std::cout << help_text;
            return 0;
        case 'd':
            fty_shm_set_test_dir(optarg);
            break;
        case 'r':
            do_write = false;
            break;
        case 'w':
            do_read = false;
            break;
        case '?':
            std::cerr << help_text;
            return 1;
        default:
            // Should not happen
            c = -1;
        }
    }

    timestamp();
    char *names = new char[NUM_METRICS * METRIC_LEN];
    char *values = new char[NUM_METRICS * VALUE_LEN];
    char **res_values = new char*[NUM_METRICS * sizeof(char *)];
    char **res_units = new char*[NUM_METRICS * sizeof(char *)];

    for (i = 0; i < NUM_METRICS; i++) {
        sprintf(names + i * METRIC_LEN, METRIC_FMT, i);
        sprintf(values + i * VALUE_LEN, VALUE_FMT, i);
    }
    timestamp();
    if (do_write) {
        for (i = 0; i < NUM_METRICS; i++)
            fty_shm_write_metric("bench_asset", names + i * METRIC_LEN,
                    values + i * METRIC_LEN, "unit", 300);
        timestamp();
    }
    if (do_read) {
        for (i = 0; i < NUM_METRICS; i++)
            fty_shm_read_metric("bench_asset", names + i * METRIC_LEN,
                    &res_values[i], &res_units[i]);
        timestamp();
    }

    delete[](names);
    delete[](values);
    for (i = 0; i < NUM_METRICS; i++) {
        free(res_values[i]);
        free(res_units[i]);
    }
    delete[](res_values);
    delete[](res_units);

    return 0;
}


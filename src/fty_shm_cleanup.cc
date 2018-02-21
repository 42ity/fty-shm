/*  =========================================================================
    fty_shm_cleanup - Garbage collector for fty-shm

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
    fty_shm_cleanup - Garbage collector for fty-shm
@discuss
@end
*/

#include <getopt.h>
#include <iostream>
#include <string.h>
#include <unistd.h>

#include "fty_shm.h"
#include "internal.h"

static const char help_text[]
    = "fty-shm-cleanup [options] ...\n"
      "  -v, --verbose         show verbose output\n"
      "  -s, --single-pass     do a single iteration and exit\n"
      "  -d, --directory=DIR   set a custom storage directory for testing\n"
      "  -h, --help            display this help text and exit\n";

int main(int argc, char* argv[])
{
    bool verbose = false, single = false;

    static struct option long_opts[] = {
        { "help", no_argument, 0, 'h' },
        { "verbose", no_argument, 0, 'v' },
        { "single-pass", no_argument, 0, 's' },
        { "directory", required_argument, 0, 'd' }
    };

    int c = 0;
    while (c >= 0) {
        c = getopt_long(argc, argv, "hvsd:", long_opts, 0);

        switch (c) {
        case 'v':
            verbose = true;
            break;
        case 'h':
            std::cout << help_text;
            return 0;
        case 's':
            single = true;
            break;
        case 'd':
            fty_shm_set_test_dir(optarg);
            break;
        case '?':
            std::cerr << help_text;
            return 1;
        default:
            // Should not happen
            c = -1;
        }
    }
    //  Insert main code here
    if (verbose)
        std::cout << "fty_shm_cleanup - Garbage collector for fty-shm" << std::endl;

    while (true) {
        if (fty_shm_cleanup(verbose) < 0)
            std::cerr << "fty-shm cleanup returned error: " << strerror(errno) << std::endl;
        if (single)
            break;
	// Use a prime to reduce the likelihood that the cleanup keeps running
	// at the same time as other periodic tasks (unless those tasks use
	// the same prime <g>)
	sleep(317);
    }
    return 0;
}

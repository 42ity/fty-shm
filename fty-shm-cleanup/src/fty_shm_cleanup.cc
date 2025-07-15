/*  =========================================================================
    fty_shm_cleanup - Garbage collector for fty-shm

    Copyright (C) 2018 - 2020 Eaton

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

/// fty_shm_cleanup - Garbage collector for fty-shm

#include <iostream>
#include <string.h>
#include <unistd.h>
#include <dirent.h>
#include <sys/stat.h>
#include <fty_log.h>

#ifndef streq
#define streq(a, b) (strcmp(a, b) == 0)
#endif

#define TTL_LEN 11

// returns 0 if success (ttl is set), else <0
static int parse_ttl(char* str, time_t& ttl)
{
    if (!(str && (*str))) {
        return -1;
    }

    // Ends str at the latest '\n'
    char* p = strrchr(str, '\n');
    if (p) { *p = 0; }

    char *err = NULL;
    ttl = int(strtol(str, &err, 10));
    if (err != str + TTL_LEN - 1) {
        errno = ERANGE;
        return -1;
    }

    return 0;
}

// -2 : file deletion failed
// -1 : invalid file or metric/data
//  0 : outdated data (file removed)
//  1 : up to date data
static int clean_outdated_data(const std::string& filename)
{
    FILE* file = fopen(filename.c_str(), "r");
    if (!file) {
        log_error("open %s failed (%s)", filename.c_str(), strerror(errno));
        return -1;
    }

    struct stat st;
    if (fstat(fileno(file), &st) != 0) {
        fclose(file);
        log_error("stat %s failed (%s)", filename.c_str(), strerror(errno));
        return -1; // invalid file
    }

    // read file in buf (first line)
    char buf[128] = "";
    fgets(buf, sizeof(buf), file);
    fclose(file);
    file = nullptr;

    // get ttl
    time_t ttl = -1;
    if (parse_ttl(buf, ttl) != 0) {
        return -1;
    }

    // data still valid ?
    if (ttl >= 0) {
        time_t now = time(nullptr);
        if ((now - st.st_mtime) > ttl) {
            errno = ESTALE;
            if (remove(filename.c_str()) != 0) {
                log_error("remove %s failed (%s)", filename.c_str(), strerror(errno));
                return -2; // rm failed
            }
            return 0; // removed
        }
    }

    return 1; // up to date
}

// cleanup outdated metrics from PATH
// returns 0 if success, else <0
static int fty_shm_cleanup(const std::string& dirpath, size_t &removedFilesCnt, bool verbose)
{
    if (verbose) {
        std::cout << "shm cleanup directory '" << dirpath << "'\n";
    }

    DIR* dir = opendir(dirpath.c_str());
    if (!dir) {
        log_error("opendir %s failed (%s)", dirpath.c_str(), strerror(errno));
        return -1;
    }

    struct dirent* ent;
    while ((ent = readdir(dir))) {
        if (streq(ent->d_name, ".") || streq(ent->d_name, "..")) {
            continue;
        }

        const std::string filename(dirpath + "/" + ent->d_name);
        if (ent->d_type == DT_DIR) { // recursive
            fty_shm_cleanup(filename, removedFilesCnt, verbose);
        }
        else {
            int r = clean_outdated_data(filename);
            if (r == 0) {
                removedFilesCnt++;
            }
        }
    }

    closedir(dir);
    return 0;
}

static const char help_text[] =
    "fty-shm-cleanup [options] ...\n"
    "  -v  verbose output\n"
    "  -h  display this help text and exit\n";

int main(int argc, char* argv[])
{
    bool verbose = false;

    // handle args
    for (int i = 1; i < argc; i++) {
        const char* arg = argv[i];
        if (streq(arg, "-v")) {
            verbose = true;
        }
        else if (streq(arg, "-h")) {
            std::cout << help_text;
            return EXIT_SUCCESS;
        }
        else {
            std::cerr << help_text;
            std::cerr << "Unknown argument '" << std::string(arg) << "'" << std::endl;
            return EXIT_FAILURE;
        }
    }

    const std::string agentName = "fty-shm-cleanup";
    const std::string path{"/run/42shm"};

    ftylog_setInstance(agentName.c_str(), "");

    std::cout << agentName << " started...\n";

    size_t rmCnt = 0;
    int r = fty_shm_cleanup(path, rmCnt, verbose);
    if (r != 0) {
        std::cerr << agentName << " failed (r: " << r << ", " << rmCnt << " metric(s) removed)\n";
        return EXIT_FAILURE;
    }

    std::cout << agentName << " ended (" << rmCnt << " metric(s) removed)\n";
    return EXIT_SUCCESS;
}

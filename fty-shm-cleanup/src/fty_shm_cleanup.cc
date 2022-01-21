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

#define TTL_LEN 11

static int parse_ttl(char* ttl_str, time_t& ttl)
{
    // Delete the '\n'
    int len = int(strlen(ttl_str) -1);
    if (ttl_str[len] == '\n') {
        ttl_str[len] = '\0';
    }

    char *err = NULL;
    int res = int(strtol(ttl_str, &err, 10));
    if (err != ttl_str + TTL_LEN - 1) {
        errno = ERANGE;
        return -1;
    }

    ttl = res;
    return 0;
}

// -2 : file deletion failed
// -1 : invalid file or metric/data
//  0 : outdated data (file removed)
//  1 : up to date data
static int clean_outdated_data(std::string filename)
{
    FILE* file = fopen(filename.c_str(), "r");
    if (!file) {
        log_error("open %s failed (%s)", filename.c_str(), strerror(errno));
        return -1;
    }

    struct stat st;
    if(fstat(fileno(file), &st) < 0) {
        fclose(file);
        log_error("stat %s failed (%s)", filename.c_str(), strerror(errno));
        return -1; // invalid file
    }

    // read file in buf
    char buf[128] = "";
    fgets(buf, sizeof(buf), file);
    fclose(file);
    file = nullptr;

  //get ttl
  time_t ttl = -1;
  if (parse_ttl(buf, ttl) < 0) {
    return -1;
  }

  //data still valid ?
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
static int fty_shm_cleanup(const std::string& directory_path, size_t &removedFilesCnt, bool verbose)
{
    if (verbose) {
        log_info("shm cleanup directory '%s'", directory_path.c_str());
    }

    DIR *dir = opendir(directory_path.c_str());
    if (dir == nullptr) {
        log_error("opendir %s failed (%s)", directory_path.c_str(), strerror(errno));
        return -1;
    }

    struct dirent *ent;
    while ((ent = readdir(dir)) != nullptr) {
        if (strcmp(ent->d_name, ".") == 0 || strcmp(ent->d_name, "..") == 0)
            continue;
        std::string filename(directory_path);
        filename.append("/").append(ent->d_name);
        if (ent->d_type == DT_DIR) { // recursive
            fty_shm_cleanup(filename, removedFilesCnt, verbose);
        }
        else {
            if (clean_outdated_data(filename) == 0) {
                removedFilesCnt++;
            }
        }
    }

    closedir(dir);
    return 0;
}

int main(int argc, char* argv[])
{
    const char* agent_name = "fty-shm-cleanup";
    const std::string path{"/run/42shm"};
    bool verbose = false;

    // handle args
    {
        static const char help_text[]
            = "fty-shm-cleanup [options] ...\n"
              "  -v    verbose output\n"
              "  -h    display this help text and exit\n";

        int argn;
        for (argn = 1; argn < argc; argn++) {
            const char* arg = argv[argn];
            if (strcmp(arg, "-v") == 0) {
                verbose = true;
            }
            else if (strcmp(arg, "-h") == 0) {
                std::cout << help_text;
                return EXIT_SUCCESS;
            }
            else {
                std::cerr << help_text;
                std::cerr << "unknown argument '" << std::string(arg) << "'" << std::endl;
                return EXIT_FAILURE;
            }
        }
    }

    ftylog_setInstance(agent_name, "");
    log_info("%s:\tStarted...", agent_name);

    size_t removedFilesCnt = 0;
    int r = fty_shm_cleanup(path, removedFilesCnt, verbose);
    if (r != 0) {
        log_error("%s:\tFailed (r: %d, %zu metric(s) removed)", agent_name, r, removedFilesCnt);
        return EXIT_FAILURE;
    }
    log_info("%s:\tEnded (%zu metric(s) removed)", agent_name, removedFilesCnt);
    return EXIT_SUCCESS;
}

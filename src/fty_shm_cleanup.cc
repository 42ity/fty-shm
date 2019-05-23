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
#include <dirent.h>
#include <sys/stat.h>
#include <fty-log/fty_logger.h>

#define TTL_LEN 11

int parse_ttl(char* ttl_str, time_t& ttl)
{
    char *err;
    int res;

    // Delete the '\n'
    int len = strlen(ttl_str) -1;
    if(ttl_str[len] == '\n')
      ttl_str[len] = '\0';
    res = strtol(ttl_str, &err, 10);
    if (err != ttl_str + TTL_LEN - 1) {
        errno = ERANGE;
        return -1;
    }
    ttl = res;
    return 0;
}

// -1 : not a valid metric/data
//  0 : outdated data
//  1 : up to date data
int clean_outdated_data(std::string filename) {
  struct stat st;
  char buf[128];
  time_t now, ttl;
  FILE* file = fopen(filename.c_str(), "r");

  if (!file) {
    log_error("Cannot open %s for cleaning", filename.c_str());
    return -1;
  }

  if(fstat(fileno(file), &st) < 0) {
    fclose(file);
    return -1;
  }

  //get ttl
  fgets(buf, sizeof(buf), file);
  
  if (parse_ttl(buf, ttl) < 0) {
    fclose(file);
    return -1;
  }

  //data still valid ?
  if (ttl) {
        now = time(NULL);
        if (now - st.st_mtime > ttl) {
            errno = ESTALE;
            fclose(file);
            remove(filename.c_str());
            return 0;
        }
  }
  return 1;
}

int fty_shm_cleanup(std::string directory_path, bool verbose) {
  DIR *dir;
  struct dirent *ent;
  if ((dir = opendir (directory_path.c_str())) != NULL) {
    while ((ent = readdir (dir)) != NULL) {
      if(strcmp(ent->d_name, ".") == 0 || strcmp(ent->d_name, "..") == 0)
        continue;
      std::string filename(directory_path);
      filename.append("/").append(ent->d_name);
      if(ent->d_type == DT_DIR) {
        fty_shm_cleanup(filename, false);
      } else {
        clean_outdated_data(filename);
      }
    }
    closedir (dir);
  }
  return 0;
}

static const char help_text[]
    = "fty-shm-cleanup [options] ...\n"
      "  -v, --verbose         show verbose output\n"
      "  -h, --help            display this help text and exit\n";

int main(int argc, char* argv[])
{
    bool verbose = false;

    static struct option long_opts[] = {
        { "help", no_argument, 0, 'h' },
        { "verbose", no_argument, 0, 'v' }
    };
    
    std::string path("/run/42shm");

    int c = 0;
    while (c >= 0) {
        c = getopt_long(argc, argv, "hv", long_opts, 0);

        switch (c) {
        case 'v':
            verbose = true;
            break;
        case 'h':
            std::cout << help_text;
            return 0;
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
        log_debug ("fty-shm-cleanup - Garbage collector for fty-shm");

    if (fty_shm_cleanup(path, verbose) < 0)
        log_error ("fty-shm cleanup returned error: %s", strerror(errno));
    return 0;
}

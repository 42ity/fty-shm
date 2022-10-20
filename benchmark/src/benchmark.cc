/*  =========================================================================
    benchmark - fty-shm benchmark

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

/*
@header
    benchmark - fty-shm benchmark
@discuss
@end
*/

#include "fty_shm.h"
#include <algorithm>
#include <assert.h>
#include <dirent.h>
#include <fcntl.h>
#include <getopt.h>
#include <iomanip>
#include <iostream>
#include <limits.h>
#include <linux/fs.h>
#include <map>
#include <random>
#include <regex>
#include <string.h>
#include <sys/stat.h>
#include <sys/syscall.h>
#include <sys/time.h>
#include <unistd.h>
#include <unordered_set>


static const char help_text[] =
    "benchmark [options] ...\n"
    "  -d, --directory=DIR   set a custom storage directory for testing\n"
    "  -c, --clean           clean and delete the custom storage directory (work only if -d)\n"
    "  -r, --write           only benchmark writes\n"
    "  -r, --read            only benchmark reads\n"
    "  -b, --benchmark=NAME  select benchmark to run (use -b help for a list)\n"
    "  -h, --help            display this help text and exit\n";

#define NUM_METRICS 10000

#define METRIC_LEN 10
#define METRIC_FMT "m%08d"

#define VALUE_LEN 10
#define VALUE_FMT "v%08d"

class Benchmark
{
public:
    Benchmark()
        : do_read(true)
        , do_write(true)
        , tv_last()
    {
        gettimeofday(&tv_start, nullptr);
    };
    typedef void (Benchmark::*benchmark_fn)();
    void c_api_bench();
    void cpp_api_bench();
    bool do_read, do_write;

private:
    struct timeval tv_start, tv_last;
    struct timeval tv_diff(const struct timeval& tv1, const struct timeval& tv2);
    void           timestamp(const std::string& message);
};

struct timeval Benchmark::tv_diff(const struct timeval& tv1, const struct timeval& tv2)
{
    struct timeval res;

    if (tv2.tv_usec < tv1.tv_usec) {
        res.tv_usec = tv2.tv_usec - tv1.tv_usec + 1000000;
        res.tv_sec  = tv2.tv_sec - tv1.tv_sec - 1;
    } else {
        res.tv_usec = tv2.tv_usec - tv1.tv_usec;
        res.tv_sec  = tv2.tv_sec - tv1.tv_sec;
    }
    return res;
}

std::ostream& operator<<(std::ostream& os, const struct timeval& tv)
{
    return os << tv.tv_sec << "." << std::setw(6) << std::setfill('0') << tv.tv_usec;
}

void Benchmark::timestamp(const std::string& message)
{
    struct timeval tv, tv_step, tv_elapsed;

    gettimeofday(&tv, nullptr);
    tv_elapsed = tv_diff(tv_start, tv);
    std::cout << std::setfill(' ') << std::setw(8) << message << ": " << tv_elapsed;
    if (tv_last.tv_sec) {
        tv_step = tv_diff(tv_last, tv);
        std::cout << " +" << tv_step;
    }
    std::cout << std::endl;
    tv_last = tv;
}


void Benchmark::c_api_bench()
{
    int    i;
    std::ostringstream Onames;
    std::ostringstream Ovalues;

    for (i = 0; i < NUM_METRICS; i++) {
        Onames << "m" << std::setw(8) << std::setfill('0') << std::right << std::to_string(i) << " ";;
        Ovalues << "v" << std::setw(8) << std::setfill('0') << std::right << std::to_string(i) << " ";
    }

    timestamp("setup");

    if (do_write) {
        std::istringstream Inames(Onames.str());
        std::istringstream Ivalues(Ovalues.str());
        std::string name;
        std::string val;

        for (i = 0; i < NUM_METRICS; i++)
        {
            if(getline(Inames, name, ' ') && getline(Ivalues, val, ' '))
            {
                fty_shm_write_metric("bench_asset", name.c_str(), val.c_str(), "unit", 300);
            }
        }
        timestamp("writes");
    }

    if (do_read) {
        std::istringstream Inames(Onames.str());
        std::string name;

        for (i = 0; i < NUM_METRICS; i++)
        {
            if(getline(Inames, name, ' '))
            {
                char* res_value;
                char* res_unit;
                fty_shm_read_metric("bench_asset", name.c_str(), &res_value, &res_unit);
                zstr_free(&res_value);
                zstr_free(&res_unit);
            }
        }

        timestamp("reads");
    }
}

void Benchmark::cpp_api_bench()
{
    std::vector<std::string> names, values;
    fty::shm::shmMetrics     all_metrics;
    int                      i;

    names.reserve(NUM_METRICS);
    values.reserve(NUM_METRICS);
    for (i = 0; i < NUM_METRICS; i++) {
        char buf[METRIC_LEN];
        snprintf(buf, sizeof(buf), METRIC_FMT, i);
        names.push_back(buf);
        snprintf(buf, sizeof(buf), VALUE_FMT, i);
        values.push_back(buf);
    }
    while(1);
    timestamp("setup");
    if (do_write) {
        for (i = 0; i < NUM_METRICS; i++)
            fty::shm::write_metric("bench_asset", names[size_t(i)], values[size_t(i)], "unit", 300);
        timestamp("writes");
    }
    if (do_read) {
        std::string res_value;
        for (i = 0; i < NUM_METRICS; i++)
            fty::shm::read_metric_value("bench_asset", names[size_t(i)], res_value);
        timestamp("reads value");

        fty_proto_t* proto;
        for (i = 0; i < NUM_METRICS; i++) {
            fty::shm::read_metric("bench_asset", names[size_t(i)], &proto);
            fty_proto_destroy(&proto);
        }

        timestamp("reads proto");

        fty::shm::read_metrics(".*", ".*", all_metrics);
        timestamp("readsall");
    }
}

struct BenchmarkDesc
{
    Benchmark::benchmark_fn func;
    const char*             desc;
};

std::map<std::string, BenchmarkDesc> benchmarks = {
    {"c", {&Benchmark::c_api_bench, "Benchmark fty_shm_{read,write}_metric"}},
    {"cpp", {&Benchmark::cpp_api_bench, "Benchmark fty::shm::{read,write}_metric"}}};

int main(int argc, char** argv)
{
    Benchmark               benchmark;
    Benchmark::benchmark_fn func   = &Benchmark::cpp_api_bench;
    bool                    bclean = false;

    static struct option long_opts[] = {{"help", no_argument, 0, 'h'}, {"directory", required_argument, 0, 'd'},
        {"clean", no_argument, 0, 'c'}, {"write", no_argument, 0, 'w'}, {"read", no_argument, 0, 'r'},
        {"benchmark", required_argument, 0, 'b'}};

    int c = 0;
    while (c >= 0) {
        c = getopt_long(argc, argv, "hd:crwb:", long_opts, 0);

        switch (c) {
            case 'h':
                std::cout << help_text;
                return 0;
            case 'd':
                fty_shm_set_test_dir(optarg);
                break;
            case 'r':
                benchmark.do_write = false;
                break;
            case 'c':
                bclean = true;
                break;
            case 'w':
                benchmark.do_read = false;
                break;
            case 'b': {
                if (strcmp(optarg, "help") == 0) {
                    std::cout << "Valid options are: " << std::endl;
                    for (auto b : benchmarks)
                        std::cout << b.first << " - " << b.second.desc << std::endl;
                    return 0;
                }
                auto it = benchmarks.find(optarg);
                if (it == benchmarks.end()) {
                    std::cerr << "Unknown benchmark: " << optarg << std::endl;
                    std::cerr << "Use -b help for a list of possible benchmark names" << std::endl;
                    return 1;
                }
                func = it->second.func;
                break;
            }
            case '?':
                std::cerr << help_text;
                return 1;
            default:
                // Should not happen
                c = -1;
        }
    }

    (benchmark.*func)();
    if (bclean)
        fty_shm_delete_test_dir();

    return 0;
}

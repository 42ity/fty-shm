# Lockless metric sharing library for 42ity

## Intro

This library provides functions to store and retrieve metrics to/from shared
memory (SHM). It provides a basic C api and a more full-featured C++ api. For
any given metric, there can be any number of writers and readers and the
library guarantees that the data will be consistent.

## How to build

To build, run:

```bash
mkdir build && cd build
cmake -DCMAKE_BUILD_TYPE=Debug -DCMAKE_INSTALL_PREFIX=usr -DBUILD_TESTING=On ..
make
sudo make install
```

## Client usage // sinan test
```
fty-shm-cli [options]
fty-shm-cli [--details / -d] device [filter] print all information about the device
             device          device name or a regex
             [filter]        regex filter to select specific metric name
             --details / -d  will print full details metrics (fty_proto style) instead of one line style
  --list / -l                print list of devices known to the agent
  publish metric <quantity> <element_src> <value> <units> <ttl>
                         publish metric on shm
                         <quantity> a string name for the metric type
                         <element_src> a string name for asset where metric was detected
                         <value> a string value of the metric (for now only values convertable to double should be used
                         <units> a string like %, W, days
                         <ttl>   a number time to leave [s]
                         Auxilary data:
                             quantity=Y, where Y is value
  --verbose / -v             verbose output
  --help / -h                this information
```

## Environment variable

This library use the environment variable FTY_SHM_AUTOCLEAN to decide if it
must autodelete the outdated metrics or not. If FTY_SHM_AUTOCLEAN is set to "OFF",
the outdated metrics will not be automaticly deleted.
The environment variable FTY_SHM_TEST_POLLING_INTERVAL is set by fty_shm_set_default_polling_interval.
It will overload the fty-nut.cfg if the value is a number > to 0.

## C api

```c
// All functions return < 0 on error and set errno accordingly.

fty_shm_write_metric("myasset", "voltage", "230", "V", 300 /* TTL */);
char *value, *unit;
fty_shm_read_metric("myasset", "voltage", &value, &unit);
```

## C++ api

```c++
using namespace fty::shm;

// All string arguments are of type std::string. Same error singalling as with the C api
write_metric("myasset", "voltage", "230", "V", 300);
std::string value, unit;
read_metric_value("myasset", "voltage", value, unit);

//will create and return the proto_metric containing the data of this metric or null.
//Caller now owns proto and must destroy it when finished with it.
fty_proto_t *proto_metric;
read_metric("myasset", "voltage", &proto_metric);


//write proto_metric as shm metric. Caller still owns proto.
write_metric(metric);

//Both of strings are regex
//will fill the the shmMetrics with all metrics match the two regex.
fty::shm::shmMetrics result;
read_metrics(".*", "voltage", result);

printf("Just found %d metrics", result.size());
for(auto &metric : resultM) {
    fty_proto_print(metric);
}

//Warning : do not delete the content of shmMetrics. It will be done automatically
//at its delete.
//If you want be the owner of some of the proto metrics contains in it, just use
// resultM.getDup(index);

```
## Utilities api

```c
// Use a custom storage directory for test purposes (the passed string must
// not be freed)
int fty_shm_set_test_dir(const char* dir);
// Clean the custom storage directory (it do nothing if no test directory already set)
int fty_shm_delete_test_dir();


```


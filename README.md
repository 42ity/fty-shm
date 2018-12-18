# Lockless metric sharing library for 42ity

## Intro

This library provides functions to store and retrieve metrics to/from shared
memory (SHM). It provides a basic C api and a more full-featured C++ api. For
any given metric, there can be any number of writers and readers and the
library guarantees that the data will be consistent.

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
fty_shm_delete_asset("myasset");
```

## C++ api

```c++
using namespace fty::shm;

// All string arguments are of type std::string. Same error singalling as with the C api

write_metric("myasset", "voltage", "230", "V", 300);
std::string value, unit;
read_metric("myasset", "voltage", value, unit);
delete_asset("myasset");

Assets assets;
if (find_assets(assets) < 0)
    return;
for (auto a : assets) {
    Metrics metrics;
    if (read_asset_metrics(a, metrics) < 0)
	continue;
    std::cout << "Asset: " << a << std::endl;
    for (auto m : metrics)
        std::cout << m.first << ": " << m.second.value << m.second.unit << std::endl;
}
```

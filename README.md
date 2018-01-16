# Lockless metric sharing library for 42ity

## Intro

This library provides functions to store and retrieve metrics to/from shared
memory (SHM). It provides a basic C api and a more full-featured C++ api. For
any given metric, there can be any number of writers and readers and the
library guarantees that the data will be consistent.

## C api

```c
// All functions return < 0 on error and set errno accordingly.

fty_shm_write_metric("myasset", "voltage", "230", 300 /* TTL */);
fty_shm_read_metric("myasset", "voltage", &value);
fty_shm_delete_asset("myasset");
```

## C++ api

```c++
using namespace fty::shm;

// All string arguments are of type std::string. Same error singalling as with the C api

write_metric("myasset", "voltage", "230", 300);
read_metric("myasset", "voltage", value);
delete_asset("myasset");

Assets assets;
if (find_assets(assets) < 0)
    return;
for (auto a : assets) {
    AssetMetrics metrics;
    if (read_asset_metrics(a) < 0)
	continue;
    std::cout << "Asset: " << a << std::endl;
    for (auto m : metrics)
        std::cout << m.first << ": " << m.second << std::endl;
}
```

## TODO
* delete_asset() is not implemented.
* A janitor process is needed to clean up stale metrics.
* `/var/run/fty-shm-1` needs to be created during boot.
* Units are not handled. Either add handling or document that this is out of scope.

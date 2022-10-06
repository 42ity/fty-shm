#include <catch2/catch.hpp>
#include <fty_proto.h>
#include "public_include/fty_shm.h"

// Version of assert() that prints the errno value for easier debugging
#define check_err(expr)                                                                                                \
    do {                                                                                                               \
        if ((expr) < 0) {                                                                                              \
            fprintf(stderr, __FILE__ ":%d: Assertion `%s' failed (%s)\n", __LINE__, #expr, strerror(errno));           \
            abort();                                                                                                   \
        }                                                                                                              \
    } while (0)

// test outputs directory
#define SELFTEST_RW "."

TEST_CASE("read-write test")
{
    std::string  value;
    fty_proto_t *proto_metric;

    REQUIRE(fty_shm_set_test_dir(SELFTEST_RW) == 0);

    REQUIRE(fty::shm::write_metric("asset", "metric", "here_is_my_value", "unit?", 2) == 0);
    
    REQUIRE(fty::shm::read_metric_value("asset", "metric", value) == 0);
    assert(value == "here_is_my_value");
    REQUIRE(fty::shm::read_metric("asset", "metric", &proto_metric) == 0);
    REQUIRE(proto_metric != nullptr);

    const char* result = fty_proto_name(proto_metric);
    REQUIRE(result);
    CHECK(streq(result, "asset"));

    result = fty_proto_type(proto_metric);
    REQUIRE(result);
    CHECK(streq(result, "metric"));

    REQUIRE(fty_proto_ttl(proto_metric) == 2);
    result = fty_proto_value(proto_metric);
    REQUIRE(result);
    CHECK(streq(result, "here_is_my_value"));

    result = fty_proto_unit(proto_metric);
    REQUIRE(result);
    CHECK(streq(result, "unit?"));

    // Wait the end of the data and test no more metrics
    zclock_sleep(3000);
    value = "none";
    REQUIRE(fty::shm::read_metric_value("asset", "metric", value) < 0);

    CHECK(value == "none");

    fty_shm_delete_test_dir();
    fty_proto_destroy(&proto_metric);
}

TEST_CASE("write-read with aux test")
{
    // test write-read proto metric (with aux)
    fty_proto_t *proto_metric, *proto_metric_result;

    proto_metric = fty_proto_new(FTY_PROTO_METRIC);
    proto_metric_result = fty_proto_new(FTY_PROTO_METRIC);

    REQUIRE(fty_shm_set_test_dir(SELFTEST_RW) == 0);

    fty_proto_set_ttl(proto_metric, 2);
    fty_proto_set_name(proto_metric, "%s", "asset");
    fty_proto_set_type(proto_metric, "%s", "metric");
    fty_proto_set_value(proto_metric, "%s", "here_is_my_value");
    fty_proto_set_unit(proto_metric, "%s", "unit?");
    
    fty_proto_aux_insert(proto_metric, "myfirstaux", "%s", "value_first_aux");
    fty_proto_aux_insert(proto_metric, "mysecondaux", "%s", "value_second_aux");
    REQUIRE(fty::shm::write_metric(proto_metric) == 0);
    
    REQUIRE(fty::shm::read_metric("asset", "metric", &proto_metric_result) == 0);
    REQUIRE(proto_metric_result);
    const char * result = fty_proto_name(proto_metric_result);
    REQUIRE(result);
    CHECK(streq(result, "asset"));

    result = fty_proto_type(proto_metric_result);
    REQUIRE(result);
    CHECK(streq(result, "metric"));

    REQUIRE(fty_proto_ttl(proto_metric_result) == 2);
    result = fty_proto_value(proto_metric_result);
    REQUIRE(result);
    CHECK(streq(result, "here_is_my_value"));

    result = fty_proto_unit(proto_metric_result);
    REQUIRE(result);
    CHECK(streq(result, "unit?"));

    result = fty_proto_aux_string(proto_metric_result, "myfirstaux", "none");
    REQUIRE(result);
    CHECK(streq(result, "value_first_aux"));

    result = fty_proto_aux_string(proto_metric_result, "mysecondaux", "none");
    REQUIRE(result);
    CHECK(streq(result, "value_second_aux"));

    fty_proto_destroy(&proto_metric_result);
    fty_proto_destroy(&proto_metric);

    zclock_sleep(3000);

    fty_shm_delete_test_dir();
}

TEST_CASE("update test")
{
    // test metric "update"
    std::string value;

    REQUIRE(fty_shm_set_test_dir(SELFTEST_RW) == 0);

    REQUIRE(fty::shm::write_metric("asset", "metric", "here_is_my_value", "unit?", 2) == 0);
    REQUIRE(fty::shm::write_metric("asset", "metric", "here_is_my_real_value", "unit?", 2) == 0);
    REQUIRE(fty::shm::read_metric_value("asset", "metric", value) == 0);
    CHECK(value == "here_is_my_real_value");
    // Wait the end of the data
    zclock_sleep(3000);

    // write severals metrics and test multiple read
    REQUIRE(fty::shm::write_metric("asset", "metric", "here_is_my_value", "unit?", 5) == 0);
    REQUIRE(fty::shm::write_metric("asset2", "metric", "here_is_my_other_value", "unit?", 5) == 0);
    REQUIRE(fty::shm::write_metric("asset", "metric2", "here_is_my_value_2", "unit?", 5) == 0);
    REQUIRE(fty::shm::write_metric("asset2", "metric2", "here_is_my_other_value_2", "unit?", 5) == 0);
    REQUIRE(fty::shm::read_metric_value("asset", "metric", value) == 0);
    CHECK(value == "here_is_my_value");
    REQUIRE(fty::shm::read_metric_value("asset2", "metric", value) == 0);
    CHECK(value == "here_is_my_other_value");
    REQUIRE(fty::shm::read_metric_value("asset", "metric2", value) == 0);
    CHECK(value == "here_is_my_value_2");
    REQUIRE(fty::shm::read_metric_value("asset2", "metric2", value) == 0);
    CHECK(value == "here_is_my_other_value_2");

    {
        fty::shm::shmMetrics resultM;
        fty::shm::read_metrics(".*", ".*", resultM);
        CHECK(resultM.size() == 4);
        for (auto& metric : resultM) {
            const char* resultT = fty_proto_type(metric);
            const char* result = fty_proto_name(metric);
            REQUIRE(result);
            CHECK((streq(result, "asset") == 0 || streq(result, "asset2") == 0));

            REQUIRE(resultT);
            CHECK((streq(resultT, "metric") == 0 || streq(resultT, "metric2") == 0));
            if (streq(result, "asset") == 0) {
                if (streq(resultT, "metric") == 0) {
                    result = fty_proto_value(metric);
                    REQUIRE(result);
                    CHECK(streq(result, "here_is_my_value") == 0);
                } else {
                    result = fty_proto_value(metric);
                    REQUIRE(result);
                    CHECK(streq(result, "here_is_my_value_2") == 0);
                }
            } else {
                if (streq(resultT, "metric2") == 0) {
                    result = fty_proto_value(metric);
                    REQUIRE(result);
                    CHECK(streq(result, "here_is_my_other_value") == 0);
                } else {
                    result = fty_proto_value(metric);
                    REQUIRE(result);
                    CHECK(streq(result, "here_is_my_other_value_2") == 0);
                }
            }
        }
    }
    // test regex
    {
        fty::shm::shmMetrics resultM;
        fty::shm::read_metrics(".*2", ".*", resultM);
        CHECK(resultM.size() == 2);
    }
    {
        fty::shm::shmMetrics resultM;
        fty::shm::read_metrics(".*", ".*2", resultM);
        CHECK(resultM.size() == 2);
    }
    {
        fty::shm::shmMetrics resultM;
        fty::shm::read_metrics("asset", ".*", resultM);
        CHECK(resultM.size() == 2);
    }
    {
        fty::shm::shmMetrics resultM;
        fty::shm::read_metrics(".*", "metric", resultM);
        CHECK(resultM.size() == 2);
    }

    REQUIRE(fty::shm::write_metric("other2", "metric", "here_is_my_value", "unit?", 5) == 0);
    REQUIRE(fty::shm::write_metric("other", "metric", "here_is_my_value", "unit?", 5) == 0);
    REQUIRE(fty::shm::write_metric("other2_asset", "metric", "here_is_my_value", "unit?", 5) == 0);
    REQUIRE(fty::shm::write_metric("asset_other", "metric", "here_is_my_value", "unit?", 5) == 0);

    {
        fty::shm::shmMetrics resultM;
        fty::shm::read_metrics("^asset.*", ".*", resultM);
        CHECK(resultM.size() == 5);
    }
    {
        fty::shm::shmMetrics resultM;
        fty::shm::read_metrics(".*other.*", ".*", resultM);
        CHECK(resultM.size() == 4);
    }
    {
        fty::shm::shmMetrics resultM;
        fty::shm::read_metrics("(^asset|other)((?!2).)*", ".*", resultM);
        CHECK(resultM.size() == 4);
    }

    fty_shm_delete_test_dir();
}

TEST_CASE("autoclean test")
{
    // verify the autoclean
    REQUIRE(fty_shm_set_test_dir(SELFTEST_RW) == 0);

    REQUIRE(fty::shm::write_metric("long", "duration", "here_the_metric", "stand", 10) == 0);

    // get the number of "file" in the directory
    DIR*           dir;
    struct dirent* ent;
    int            dir_number = 0;
    std::string    dir_metric(SELFTEST_RW);
    dir_metric.append("/").append(FTY_SHM_METRIC_TYPE);
    if ((dir = opendir(dir_metric.c_str())) != nullptr) {
        while ((ent = readdir(dir)) != nullptr) {
            dir_number++;
        }
        closedir(dir);
    } else {
        /* could not open directory */
        perror("");
        CHECK(false);
    }


    // we must have file number
    CHECK(dir_number == 3);
    // wait the expiration of some metrics
    zclock_sleep(6000);

    {
        fty::shm::shmMetrics resultM;
        fty::shm::read_metrics(".*", ".*", resultM);
        CHECK(resultM.size() == 1);
    }

    // get the new number of "files"
    dir_number = 0;
    if ((dir = opendir(dir_metric.c_str())) != nullptr) {
        while ((ent = readdir(dir)) != nullptr) {
            dir_number++;
        }
        closedir(dir);
    } else {
        /* could not open directory */
        perror("");
        CHECK(false);
    }

    // only one metric file left
    CHECK(dir_number == 3);
    fty_shm_delete_test_dir();
}

TEST_CASE("poll interval test")
{
    fty_shm_set_default_polling_interval(30);
    REQUIRE(fty_get_polling_interval() == 30);
    
    fty_shm_set_default_polling_interval(35);
    REQUIRE(fty_get_polling_interval() == 35);
}

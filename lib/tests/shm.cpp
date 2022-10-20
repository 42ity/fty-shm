#include <catch2/catch.hpp>
#include <fty_proto.h>
#include "public_include/fty_shm.h"

// test outputs directory
#define SELFTEST_RW "."

TEST_CASE("read-write test")
{
    std::string  value;
    fty_proto_t *proto_metric = nullptr;


printf("XXXX\n");

    // adjust test directory
    REQUIRE(fty_shm_set_test_dir(SELFTEST_RW) == 0);

    char longAssetName[PATH_MAX];

    // create an asset name longer than max allowed
    for(auto& i : longAssetName)
    {
        i = 's';
    }

    longAssetName[PATH_MAX - 1] = '\0';

printf("AAA\n");

    // write_metric returns fail
    REQUIRE(fty::shm::write_metric(longAssetName, "metric", "here_is_my_value", "unit?", 2) < 0);

    // create asset names with invalid characters
    const char * invalidAssetName1 = "te/st";
    const char * invalidAssetName2 = "te@st";


printf("999999\n");

    // write_metric returns fail
    REQUIRE(fty::shm::write_metric(invalidAssetName1, "metric", "here_is_my_value", "unit?", 2) < 0);
    REQUIRE(fty::shm::write_metric(invalidAssetName2, "metric", "here_is_my_value", "unit?", 2) < 0);


printf("8888\n");

    // pass invalid parameters to write_metric(returns fail)
    REQUIRE(fty::shm::write_metric("", "", "", "", -1) < 0);

printf("xx\n");
    REQUIRE(fty::shm::write_metric("asset", "metric", "", "unit?", -1) < 0);

printf("yy\n");
    REQUIRE(fty::shm::write_metric("asset", "", "here_is_my_value", "unit?", -1) < 0);

printf("zz\n");
    REQUIRE(fty::shm::write_metric("", "metric", "here_is_my_value", "unit?", -1) < 0);


printf("ttt\n");
    // pass null to write_metric(returns fail)
    REQUIRE(fty::shm::write_metric(nullptr) < 0);

printf("BBB\n");

    // pass negative ttl value to write_metric
    // neg. values accepted as zero
    fty_proto_t *proto_neg;
    REQUIRE(fty::shm::write_metric("asset_with_negative_ttl", "metric", "here_is_my_value", "unit?", -1) == 0);
    REQUIRE(fty::shm::read_metric("asset_with_negative_ttl", "metric", &proto_neg) == 0);
    REQUIRE(streq(fty_proto_value(proto_neg), "here_is_my_value"));
    REQUIRE(fty_proto_ttl(proto_neg) == 0);
    fty_proto_set_ttl(proto_neg, 1);
    REQUIRE(fty::shm::write_metric(proto_neg) == 0);

    // write metrics with ordinary allowed parameters
    REQUIRE(fty::shm::write_metric("asset", "metric", "here_is_my_value", "", 2) == 0);
    REQUIRE(fty::shm::write_metric("asset", "metric", "here_is_my_value", "unit?", 2) == 0);

printf("CCCC\n");

    // read values with the same parameters
    // succesful and equals to written values
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

    // try to read non-existing asset
    const char * nonExistingAsset = "no-asset";
    value = "none";

    REQUIRE(fty::shm::read_metric_value(nonExistingAsset, "metric", value) < 0);
    CHECK(value == "none");

    // Wait the end of the data and test no more metrics
    zclock_sleep(3000);

    REQUIRE(fty::shm::read_metric_value("asset", "metric", value) < 0);

    CHECK(value == "none");

    fty_shm_delete_test_dir();
    fty_proto_destroy(&proto_metric);
    fty_proto_destroy(&proto_neg);
}

TEST_CASE("write-read with aux test")
{
    // test write-read proto metric (with aux)
    fty_proto_t *proto_metric, *proto_metric_result;

    proto_metric = fty_proto_new(FTY_PROTO_METRIC);
    proto_metric_result = fty_proto_new(FTY_PROTO_METRIC);

    REQUIRE(fty_shm_set_test_dir(SELFTEST_RW) == 0);

    // create fty_proto_t with aux
    fty_proto_set_ttl(proto_metric, 2);
    fty_proto_set_name(proto_metric, "%s", "asset");
    fty_proto_set_type(proto_metric, "%s", "metric");
    fty_proto_set_value(proto_metric, "%s", "here_is_my_value");
    fty_proto_set_unit(proto_metric, "%s", "unit?");
    fty_proto_aux_insert(proto_metric, "myfirstaux", "%s", "value_first_aux");
    fty_proto_aux_insert(proto_metric, "mysecondaux", "%s", "value_second_aux");

    // write proto_metric
    REQUIRE(fty::shm::write_metric(proto_metric) == 0);

    //read back
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

    // read aux values
    result = fty_proto_aux_string(proto_metric_result, "myfirstaux", "none");
    REQUIRE(result);
    CHECK(streq(result, "value_first_aux"));

    result = fty_proto_aux_string(proto_metric_result, "mysecondaux", "none");
    REQUIRE(result);
    CHECK(streq(result, "value_second_aux"));

    fty_proto_destroy(&proto_metric_result);
    fty_proto_destroy(&proto_metric);

    // wait the expiration of metrics
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
        // read multiple metrics
        fty::shm::shmMetrics resultM;

        //read wrong values, returns nothing on resultM(size() == 0)
        CHECK(fty::shm::read_metrics("", "", resultM) == 0);
        CHECK(resultM.size() == 0);

        //read all metrics available
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
    // get current interval
    int temp = fty_get_polling_interval();

    // set interval to 4, getter returns the same
    fty_shm_set_default_polling_interval(4);
    REQUIRE(fty_get_polling_interval() == 4);

    // set interval to 20, getter returns the same
    fty_shm_set_default_polling_interval(20);
    REQUIRE(fty_get_polling_interval() == 20);

    // set interval to -1, rejected & unchanged
    fty_shm_set_default_polling_interval(-1);
    REQUIRE(fty_get_polling_interval() == 20);

    // set the interval before tests
    fty_shm_set_default_polling_interval(temp);
}

TEST_CASE("shmMetrics iterators test")
{
    fty_proto_t * proto1 = fty_proto_new(FTY_PROTO_METRIC);
    fty_proto_t * proto2 = fty_proto_new(FTY_PROTO_METRIC);
    std::unique_ptr<fty::shm::shmMetrics> m = std::make_unique<fty::shm::shmMetrics>();

    // create fty_proto_t with dummy values
    fty_proto_set_name(proto1, "%s", "name1");
    fty_proto_set_value(proto1, "%s", "value1");

    fty_proto_set_name(proto2, "%s", "name2");
    fty_proto_set_value(proto2, "%s", "value2");

    // add to shmMetrics object
    m->add(proto1);
    m->add(proto2);

    // size must be == 2 (proto1, proto2)
    CHECK(m->size() == 2);

    // first element is the address of proto1
    CHECK(streq(fty_proto_name(m->get(0)), "name1"));
    CHECK(streq(fty_proto_value(m->get(0)), "value1"));
    fty_proto_set_name(m->get(0), "%s", "name1 test");
    CHECK(streq(fty_proto_name(m->get(0)), "name1 test"));
    CHECK(streq(fty_proto_name(proto1), "name1 test"));

    // index 1 is proto2
    CHECK(streq(fty_proto_name(m->get(1)), "name2"));
    CHECK(streq(fty_proto_value(m->get(1)), "value2"));

    // begin returns the address first element(proto1)
    fty::shm::shmMetrics::iterator itBegin = m->begin();
    CHECK(streq(fty_proto_name(*itBegin), fty_proto_name(proto1)));
    CHECK(streq(fty_proto_value(*itBegin), fty_proto_value(proto1)));

    // constant begin returns the address first element(proto1)
    fty::shm::shmMetrics::const_iterator itCBegin = m->cbegin();
    CHECK(streq(fty_proto_name(*itCBegin), fty_proto_name(proto1)));
    CHECK(streq(fty_proto_value(*itCBegin), fty_proto_value(proto1)));
}

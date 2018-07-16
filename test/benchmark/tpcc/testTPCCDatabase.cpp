//
// Created by Yi Lu on 7/15/18.
//

#include <gtest/gtest.h>
#include "benchmark/tpcc/Database.h"

TEST(TestTPCCDatabase, TestWarehouse) {
    scar::tpcc::warehouse::key key(1);
    EXPECT_EQ(key.W_ID, 1);
    scar::tpcc::warehouse::value value;
    value.W_NAME = "amazon";
    EXPECT_EQ(value.W_NAME, "amazon");
    scar::tpcc::warehouse::key key_ = scar::tpcc::warehouse::key(1);
    EXPECT_EQ(key, key_);
}

//
// Created by Yi Lu on 9/12/18.
//

#pragma once

#include "benchmark/ycsb/Schema.h"

namespace coco {

namespace ycsb {
struct Storage {
  ycsb::key ycsb_keys[10];
  ycsb::value ycsb_values[10];
};

} // namespace ycsb
} // namespace coco
[![Build Status](https://travis-ci.org/luyi0619/coco.svg?branch=master)](https://travis-ci.org/luyi0619/coco)

# Dependencies

```sh
sudo apt-get update
sudo apt-get install -y zip make cmake g++ libjemalloc-dev libboost-dev libgoogle-glog-dev
```

# Download

```sh
git clone https://github.com/luyi0619/coco.git
```

# Build

```
./compile.sh
```
# Run

* YCSB
Update server configuration in run_ycsb.sh and ycsb.sh
Run contention test
```
./ycsb_zipf_test.sh
```
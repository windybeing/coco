servers="172.31.35.113:10010;172.31.37.52:10010;172.31.33.57:10010;172.31.37.34:10010;172.31.42.116:10010;172.31.35.77:10010;172.31.33.250:10010;172.31.36.29:10010"
server_array=$(echo $servers | tr ";" "\n")
worker_num=12
io_num=1 # io_num=1 => 2 io threads
server_num=8
partition_num=96 # equals to warehouse table num
partitioner=hash3 # hash N => N replica
batch_size=100 # used for 
read_on_replica=true 
protocol=Silo # Silo/Scar/TwoPL/SiloGC/ScarGC/Aria  SiloSI/ScarSI

# scar/silo/2pl w/o replication
# ./bench_tpcc --logtostderr=1 \
#     --servers=$servers --id=$1 --sleep_on_retry=false \
#     --threads=$worker_num --io=$io_num --partition_num=$server_num --protocol=Scar
# ./bench_tpcc --log_dir=./log \
#     --servers=$servers --id=$1\
#     --threads=$worker_num --io=$io_num --partition_num=$server_num --protocol=Silo
# ./bench_tpcc --log_dir=./log \
#     --servers=$servers --id=$1\
#     --threads=$worker_num --io=$io_num --partition_num=$server_num --protocol=TwoPL

# # scar/silo/TwoPL with replication
# ./bench_tpcc --logtostderr=1 \
#     --servers=$servers --id=$1 --sleep_on_retry=false \
#     --threads=$worker_num --io=$io_num --partition_num=$partition_num \
#     --partitioner=$partitioner --protocol=Silo

# ./bench_tpcc --logtostderr=1 \
#     --servers=$servers --id=$1\
#     --threads=$worker_num --io=$io_num --partition_num=$partition_num \
#     --partitioner=$partitioner --protocol=Scar

# ./bench_tpcc --log_dir=./log \
#     --servers=$servers --id=$1\
#     --threads=$worker_num --io=$io_num --partition_num=$partition_num \
#     --partitioner=$partitioner --protocol=TwoPL

# # epoch
./bench_tpcc --logtostderr=1 \
    --servers=$servers --id=$1 \
    --threads=$worker_num --io=$io_num \
    --batch_size=$batch_size --read_on_replica=true --partition_num=$partition_num --partitioner=$partitioner --protocol=Aria

# ./bench_tpcc --log_dir=./log \
#     --servers=$servers --id=$1\
#     --threads=$worker_num --io=$io_num --partition_num=$server_num --protocol=SiloGC

# ./bench_tpcc --log_dir=./log \
#     --servers=$servers --id=$1\
#     --threads=$worker_num --io=$io_num --partition_num=$server_num --protocol=Aria


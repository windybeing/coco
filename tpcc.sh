all_servers=(172.31.44.88:10010 172.31.34.24:10010 172.31.34.242:10010 172.31.41.134:10010 172.31.46.64:10010 172.31.38.97:10010 172.31.36.42:10010 172.31.42.221:10010 172.31.39.159:10010 172.31.40.194:10010 172.31.41.167:10010 172.31.41.54:10010 172.31.33.219:10010 172.31.39.49:10010 172.31.39.54:10010 172.31.36.183:10010 172.31.34.206:10010 172.31.43.201:10010 172.31.38.90:10010 172.31.42.69:10010 172.31.46.77:10010 172.31.39.242:10010 172.31.41.88:10010 172.31.32.229:10010 172.31.33.66:10010 172.31.44.237:10010 172.31.45.242:10010)
# servers="172.31.44.88:10010  172.31.34.24:10010;172.31.34.242:10010"
# all_servers=(172.31.44.88:10010 172.31.34.24:10010 172.31.34.242:10010"
server_num=19
servers=${all_servers[0]}
for ((i=1;i<$server_num;i++));  
do
    servers=$servers";"${all_servers[$i]}
done
echo $servers
server_array=$(echo $servers | tr ";" "\n")
worker_num=5
io_num=1 # io_num=1 => 2 io threads
# server_num=5
# partition_num=40 # equals to warehouse table num
partition_num=48 # equals to warehouse table num
partitioner=hash3 # hash N => N replica
batch_size=2000 # used for 
read_on_replica=false
protocol=SiloGC # Silo/Scar/TwoPL/SiloGC/ScarGC/Aria  SiloSI/ScarSI
query=mixed
neworder_dist=1
payment_dist=15

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
    --servers=$servers --id=$1 --sleep_on_retry=true\
    --threads=$worker_num --io=$io_num --query=$query \
    --neworder_dist=$neworder_dist --payment_dist=$payment_dist \
    --batch_size=$batch_size --read_on_replica=$read_on_replica --partition_num=$partition_num --partitioner=$partitioner --protocol=$protocol

# ./bench_tpcc --log_dir=./log \
#     --servers=$servers --id=$1\
#     --threads=$worker_num --io=$io_num --partition_num=$server_num --protocol=SiloGC

# ./bench_tpcc --log_dir=./log \
#     --servers=$servers --id=$1\
#     --threads=$worker_num --io=$io_num --partition_num=$server_num --protocol=Aria


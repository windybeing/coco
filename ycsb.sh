# servers="172.31.44.88:10010;172.31.34.24:10010;172.31.34.242:10010;172.31.41.134:10010;172.31.46.64:10010;172.31.38.97:10010;172.31.36.42:10010;172.31.42.221:10010;172.31.39.159:10010;172.31.40.194:10010;172.31.41.167:10010;172.31.41.54:10010"
# servers="172.31.44.88:10010;172.31.34.24:10010;172.31.34.242:10010;172.31.41.134:10010;172.31.46.64:10010;172.31.38.97:10010;172.31.36.42:10010;172.31.42.221:10010;172.31.39.159:10010;172.31.40.194:10010;172.31.41.167:10010;;172.31.41.54:10010;172.31.33.219:10010;172.31.39.49:10010;172.31.39.54:10010;172.31.36.183:10010"
all_servers=(172.31.44.88:10010 172.31.34.24:10010 172.31.34.242:10010 172.31.41.134:10010 172.31.46.64:10010 172.31.38.97:10010 172.31.36.42:10010 172.31.42.221:10010 172.31.39.159:10010 172.31.40.194:10010 172.31.41.167:10010 172.31.41.54:10010 172.31.33.219:10010 172.31.39.49:10010 172.31.39.54:10010 172.31.36.183:10010 172.31.34.206:10010 172.31.43.201:10010 172.31.38.90:10010 172.31.42.69:10010 172.31.46.77:10010 172.31.39.242:10010 172.31.41.88:10010 172.31.32.229:10010 172.31.33.66:10010 172.31.44.237:10010 172.31.45.242:10010)
# servers="172.31.44.88:10010;172.31.34.24:10010;172.31.34.242:10010;172.31.41.134:10010;172.31.46.64:10010;172.31.38.97:10010;172.31.36.42:10010;172.31.42.221:10010;172.31.39.159:10010;172.31.40.194:10010;172.31.41.167:10010;172.31.41.54:10010;172.31.33.219:10010;172.31.39.49:10010;172.31.39.54:10010;172.31.36.183:10010"
# servers="172.31.44.88:10010;172.31.34.24:10010;172.31.34.242:10010;172.31.41.134:10010"
# servers="172.31.44.88:10010"
server_num=27
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
partition_num=135 # equals to worker thread num
partitioner=hash3 # hash N => N replica
read_on_replica=false
protocol=SiloGC # Silo/Scar/TwoPL/SiloGC/ScarGC/Aria  SiloSI/ScarSI
read_write_ratio=50
read_only_ratio=80
cross_ratio=10
global_key_space=true
two_partitions=false
distributed_ratio=xxx
((keys=214732800 / $partition_num))  #12 * 1024 * 272 * 64 / 125 = 1711276   2228224
echo $keys
zipf=xxx

./bench_ycsb --logtostderr=1 \
    --servers=$servers --id=$1 --sleep_on_retry=true \
    --threads=$worker_num --io=$io_num --read_write_ratio=$read_write_ratio --read_only_ratio=$read_only_ratio \
    --global_key_space=$global_key_space --keys=$keys --zipf=$zipf --two_partitions=$two_partitions --cross_ratio=$cross_ratio \
    --read_on_replica=$read_on_replica --partition_num=$partition_num --partitioner=$partitioner --protocol=$protocol --distributed_ratio=$distributed_ratio
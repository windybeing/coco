#! /bin/bash
# servers="172.31.34.24;172.31.34.242;"
all_servers=(172.31.34.24  172.31.34.242  172.31.41.134  172.31.46.64  172.31.38.97  172.31.36.42  172.31.42.221  172.31.39.159  172.31.40.194  172.31.41.167  172.31.41.54  172.31.33.219  172.31.39.49  172.31.39.54  172.31.36.183  172.31.34.206  172.31.43.201  172.31.38.90  172.31.42.69  172.31.46.77  172.31.39.242  172.31.41.88  172.31.32.229  172.31.33.66 172.31.44.237 172.31.45.242)
# servers="172.31.34.24;172.31.34.242;172.31.41.134;172.31.46.64;172.31.38.97;172.31.36.42;172.31.42.221;172.31.39.159;172.31.40.194;172.31.41.167"
# servers="172.31.34.24;172.31.34.242"
server_num=19
for ((i=0;i<$server_num-1;i++));  
do
    servers=$servers";"${all_servers[$i]}
done
server_array=$(echo $servers | tr ";" "\n")
echo $server_array

for server in $server_array; do
    ssh $server "tmux kill-session -t coco"
done
pkill bench_tpcc

for server in $server_array; do
    rsync -aqzP --port=22 --force ./tpcc.sh $server:/home/ubuntu/coco/
    rsync -aqzP --port=22 --force ./bench_tpcc $server:/home/ubuntu/coco/
done

id=1
for addr in $server_array; do
    ssh $addr "cd coco; tmux new-session -d -s coco \"./tpcc.sh $id\"" &
    ((id+=1));
done
./tpcc.sh 0 2>tmp.txt
cat tmp.txt | grep "total commit" >> result.txt
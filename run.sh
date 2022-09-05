#! /bin/bash
# servers="172.31.34.24;172.31.34.242;172.31.41.134;172.31.46.64;172.31.38.97;172.31.36.42;172.31.42.221;172.31.39.159"
servers="172.31.34.24;172.31.34.242;172.31.41.134;172.31.46.64"
server_array=$(echo $servers | tr ";" "\n")
echo $server_array

for server in $server_array; do
    ssh $server "tmux kill-session -t coco"
    rsync -aqzP --port=22 --force ./tpcc.sh $server:/home/ubuntu/coco/
    rsync -aqzP --port=22 --force ./bench_tpcc $server:/home/ubuntu/coco/
done

id=1
for addr in $server_array; do
    ssh $addr "cd coco; tmux new-session -d -s coco \"./tpcc.sh $id\"" &
    ((id+=1));
done
./tpcc.sh 0

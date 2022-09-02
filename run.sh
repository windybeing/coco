#! /bin/bash
servers="172.31.37.52;172.31.33.57;172.31.37.34;172.31.42.116;172.31.35.77;172.31.33.250;172.31.36.29"
server_array=$(echo $servers | tr ";" "\n")
server_num=8
echo $server_array
./tpcc.sh 0 &
id=1
for addr in $server_array; do
    ssh $addr "tmux kill-session -t coco"
    ssh $addr "cd coco; tmux new-session -d -s coco \"./tpcc.sh $id\"" &
    ((id+=1));
done

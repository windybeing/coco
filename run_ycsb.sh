#! /bin/bash
servers="172.31.34.24;172.31.34.242;172.31.41.134;172.31.46.64;172.31.38.97;172.31.36.42;172.31.42.221;172.31.39.159;172.31.40.194;172.31.41.167;172.31.41.54;172.31.33.219;172.31.39.49;172.31.39.54;172.31.36.183;172.31.34.206;172.31.43.201;172.31.38.90;172.31.42.69;172.31.46.77;172.31.39.242;172.31.41.88;172.31.32.229;172.31.33.66"
# servers="172.31.34.24;172.31.34.242;172.31.41.134"
# servers="172.31.34.24;172.31.34.242;172.31.41.134;172.31.46.64;172.31.38.97;172.31.36.42;172.31.42.221;172.31.39.159;172.31.40.194;172.31.41.167;172.31.41.54;172.31.33.219;172.31.39.49;172.31.39.54;172.31.36.183"
# servers="172.31.34.24;172.31.34.242;172.31.41.134;172.31.46.64;172.31.38.97;172.31.36.42;172.31.42.221;172.31.39.159;172.31.40.194;172.31.41.167;172.31.41.54;172.31.33.219;172.31.39.49;172.31.39.54;172.31.36.183"
server_array=$(echo $servers | tr ";" "\n")
echo $server_array

for server in $server_array; do
    ssh $server "tmux kill-session -t coco"
done
pkill bench_ycsb

for server in $server_array; do
    rsync -aqzP --port=22 --force ./ycsb.sh $server:/home/ubuntu/coco/
    rsync -aqzP --port=22 --force ./bench_ycsb $server:/home/ubuntu/coco/
done
for addr in $server_array; do
    ssh $addr "rm -rf .vscode-server" 
done
id=1
for addr in $server_array; do
    ssh $addr "cd coco; tmux new-session -d -s coco \"./ycsb.sh $id\"" &
    ((id+=1));
done
./ycsb.sh 0

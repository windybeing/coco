servers="172.31.37.52;172.31.33.57;172.31.37.34;172.31.42.116;172.31.35.77;172.31.33.250;172.31.36.29"
server_array=$(echo $servers | tr ";" "\n")
for server in $server_array; do
    rsync -aqzP --port=22 --force ./tpcc.sh $server:/home/ubuntu/coco/
done

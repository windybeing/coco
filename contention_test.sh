contention=(1 5 10 15 20 25)

for contention_ratio in ${contention[@]};
do
    sed -i 's/neworder_dist=xxx/neworder_dist='"$contention_ratio"'/' ./tpcc.sh
    ./run_tpcc.sh 
    sed -i 's/neworder_dist='"$contention_ratio"'/neworder_dist=xxx/' ./tpcc.sh
    sleep 60
done


theta=(0.1 0.99)
dis_ratio=(0 10 20 30 40 50 60 70 80 90 100)
# dis_ratio=(0 2)
# theta=(0.9)
cat /dev/null > result.txt
for ratio in ${dis_ratio[@]};
do 
    sed -i 's/distributed_ratio=xxx/distributed_ratio='"$ratio"'/' ./ycsb.sh
    for zipf_theta in ${theta[@]};
    do
        sed -i 's/zipf=xxx/zipf='"$zipf_theta"'/' ./ycsb.sh
        ./run_ycsb.sh 
        sed -i 's/zipf='"$zipf_theta"'/zipf=xxx/' ./ycsb.sh
        sleep 30
    done
    sed -i 's/distributed_ratio='"$ratio"'/distributed_ratio=xxx/' ./ycsb.sh
done

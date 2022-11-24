# theta=(0.1 0.2 0.3 0.4 0.5 0.6 0.7 0.8 0.9 0.95 0.99)
theta=(0.9)
for zipf_theta in ${theta[@]};
do
    sed -i 's/zipf=xxx/zipf='"$zipf_theta"'/' ./ycsb.sh
    ./run_ycsb.sh 
    sed -i 's/zipf='"$zipf_theta"'/zipf=xxx/' ./ycsb.sh
    sleep 30
done
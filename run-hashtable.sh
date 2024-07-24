
cores=(18) #1 2 4 8 12 18 36 54 72)
rw_ratios=(0 50 90 100)
read_loop_counts=(50) # 50 100 200)
write_loop_counts=(50) # 50 100 200)
num_buckets=(128)
num_dependent_buckets=(1 4 8 16)
binary=./build_ninja/test/perf-con-hashtable

out_dir=./results

mkdir -p ${out_dir}

for nb in ${num_buckets[@]}
do
	for ndb in ${num_dependent_buckets[@]}
	do
		for c in ${cores[@]}
		do
			for ratio in ${rw_ratios[@]}
			do
				for rlc in ${read_loop_counts[@]}
				do
					for wlc in ${write_loop_counts[@]}
					do
						echo core $c ratio $ratio rlc $rlc wlc $wlc
						out_file=$out_dir/hashtable-${nb}buckets-${ndb}dependentbuckets-${ratio}rwratio-${rlc}readloopcnt-${wlc}writeloopcnt.$c
						$binary --cores $c --allow_leaks --read_loop_count $rlc --write_loop_count $wlc --rw_ratio $ratio \
								--num_operations 20000000 --num_buckets $nb --num_dependent_buckets $ndb | tee $out_file
						sleep 1
					done
				done
			done
		done
	done
done

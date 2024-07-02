
cores=(1 2 4 8 12 18 36 54 72)
binary=./build_ninja/test/perf-con-hashtable

out_dir=./results

mkdir -p ${out_dir}

for c in ${cores[@]}
do
	out_file=$out_dir/hashtable.$c
    	$binary --cores $c --allow_leaks | tee $out_file
done

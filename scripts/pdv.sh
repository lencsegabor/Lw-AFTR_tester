#!/bin/bash
#Parameters
impl="lw4o6tester-PDV-snabb-10G-1023_lwB4-Fg100" # name of the tested implementation (used for logging)
dir="b" # valid values: b,f,r; b: bidirectional, f: forward (Left to Right, 6 --> 4), r: reverse (Right to Left, 6 <-- 4) 
r=117464 # frame pdv determined by thropughput measurement
fs=84 # IPv6 frame size; IPv4 frame size is always 20 bytes less 
xpts=60 # duration (in seconds) of an experiment instance
to=2000 # timeout in milliseconds
n=2 # foreground traffic, if ( frame_counter % n < m ) 
m=2 # E.g. n=m=2 is all foreground traffic; n=2,m=0 is all background traffic; n=10,m=9 is 90% fg and 10% bg
sleept=10 # sleeping time between the experiments
no_exp=20 # number of experiments
res_dir="results" # base directory for the results (they will be copied there at the end)

############################

# Genepdv log/csv file headers
echo "#############################" > pdvtest.log
echo "Tested Implementation: $impl" >> pdvtest.log
echo "Frame size: $fs:" >> pdvtest.log
echo "Direction: $dir" >> pdvtest.log
echo "Value of n: $n" >> pdvtest.log
echo "Value of m: $m" >> pdvtest.log
echo "Duration (sec): $xpts" >> pdvtest.log
echo "Frame pdv (fps): $r" >> pdvtest.log
echo "Timeout value (sec): $to"  >> pdvtest.log
echo "Sleep Time (sec): $sleept" >> pdvtest.log
date +'Date&Time: %Y-%m-%d %H:%M:%S.%N' >> pdvtest.log
echo "#############################" >> pdvtest.log

if [ "$dir" == "b" ]; then
	echo "No, Size, Dir, n, m, Duration, Rate, Timeout, Fwd-PDV, Rev-PDV" > pdv.csv
fi
if [ "$dir" == "f" ]; then
	echo "No, Size, Dir, n, m, Duration, Rate, Timeout, Fwd-PDV" > pdv.csv
fi
if [ "$dir" == "r" ]; then
	echo "No, Size, Dir, n, m, Duration, Rate, Timeout, Rev-PDV" > pdv.csv
fi


# Execute $no_exp number of experiments
for (( N=1; N <= $no_exp; N++ ))
do
	echo "Exectuting experiment #$N..."
	echo "Exectuting experiment #$N..." >> pdvtest.log
	echo "Command line is: ./build/lw4o6_tester $fs $r $xpts $to $n $m 0"
	echo "Command line is: ./build/lw4o6_tester $fs $r $xpts $to $n $m 0" >> pdvtest.log
	# Execute the test program
	./build/lw4o6_tester $fs $r $xpts $to $n $m 0 > temp.out 2>&1
	# Log and print out info
	cat temp.out >> pdvtest.log
	cat temp.out | tail
	# Check for any errors
	error=$(grep 'Error:' temp.out)
        if [ -n "$error" ]; then
		echo "Error occurred, testing must stop."
		exit -1;
	fi
	# Collect and evaluate the results (depending on the direction of the test)
	if [ "$dir" == "b" ]; then
		fwd_PDV=$(grep 'forward PDV' temp.out | awk '{print $3}')
		rev_PDV=$(grep 'reverse PDV' temp.out | awk '{print $3}')
	echo "$N, $fs, $dir, $n, $m, $xpts, $r, $to, $fwd_PDV, $rev_PDV" >> pdv.csv
	fi
	if [ "$dir" == "f" ]; then
                fwd_PDV=$(grep 'forward PDV' temp.out | awk '{print $3}')
	echo "$N, $fs, $dir, $n, $m, $xpts, $r, $to, $fwd_PDV" >> pdv.csv
	fi
	if [ "$dir" == "r" ]; then
                rev_PDV=$(grep 'reverse PDV' temp.out | awk '{print $3}')
	echo "$N, $fs, $dir, $n, $m, $xpts, $r, $to, $rev_PDV" >> pdv.csv
	fi
	echo "Sleeping for $sleept seconds..."
	echo "Sleeping for $sleept seconds..." >> pdvtest.log
	# Sleep for $sleept time to give DUT a chance to relax
	sleep $sleept 
	rm temp*
done # (end of the $no_exp number of experiments)
# Save the results (may be commented out if not needed)
dirname="$res_dir/$(date +$impl'-'$dir'-'$fs'-'$n'-'$m'-'$xpts'-'$r'-'$to'-%F-%H%M')"
mkdir -p $dirname
mv pdvtest.log $dirname/
mv pdv.csv $dirname/
mv nohup.out $dirname/ 
cp -a lw4o6.conf $dirname/

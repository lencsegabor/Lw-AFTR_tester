#!/bin/bash
#Parameters
impl="lw4o6tester-LAT-snabb-10G-1023_lwB4-Fg100" # name of the tested implementation (used for logging)
dir="b" # valid values: b,f,r; b: bidirectional, f: forward (Left to Right, 6 --> 4), r: reverse (Right to Left, 6 <-- 4) 
r=117464 # frame rate determined by thropughput measurement
fs=84 # IPv6 frame size; IPv4 frame size is always 20 bytes less 
xpts=120 # duration (in seconds) of an experiment instance
to=2000 # timeout in milliseconds
n=2 # foreground traffic, if ( frame_counter % n < m ) 
m=2 # E.g. n=m=2 is all foreground traffic; n=2,m=0 is all background traffic; n=10,m=9 is 90% fg and 10% bg
delay=60 # delay before the insertion of the first identifying tag
tags=500 # number of identifying tags
sleept=10 # sleeping time between the experiments
no_exp=20 # number of experiments
res_dir="results" # base directory for the results (they will be copied there at the end)

############################

# Generate log/csv file headers
echo "#############################" > latencytest.log
echo "Tested Implementation: $impl" >> latencytest.log
echo "Frame size: $fs:" >> latencytest.log
echo "Direction $dir:" >> latencytest.log
echo "Value of n: $n" >> latencytest.log
echo "Value of m: $m" >> latencytest.log
echo "Duration (sec): $xpts" >> latencytest.log
echo "Frame rate (fps): $r" >> latencytest.log
echo "Timeout value (sec): $to"  >> latencytest.log
echo "Delay before the first tag: $delay"  >> latencytest.log
echo "Number of identifying tags: $tags"  >> latencytest.log
echo "Sleep Time (sec): $sleept" >> latencytest.log
date +'Date&Time: %Y-%m-%d %H:%M:%S.%N' >> latencytest.log
echo "#############################" >> latencytest.log

if [ "$dir" == "b" ]; then
	echo "No, Size, Dir, n, m, Duration, Rate, Timeout, Delay, Tags, Fwd-TL, Fwd-WCL, Rev-TL, Rev-WCL" > latency.csv
fi
if [ "$dir" == "f" ]; then
	echo "No, Size, Dir, n, m, Duration, Rate, Timeout, Delay, Tags, Fwd-TL, Fwd-WCL" > latency.csv
fi
if [ "$dir" == "r" ]; then
	echo "No, Size, Dir, n, m, Duration, Rate, Timeout, Delay, Tags, Rev-TL, Rev-WCL" > latency.csv
fi


# Execute $no_exp number of experiments
for (( N=1; N <= $no_exp; N++ ))
do
	echo "Exectuting experiment #$N..."
	echo "Exectuting experiment #$N..." >> latencytest.log
	echo "Command line is: ./build/lw4o6_tester $fs $r $xpts $to $n $m $delay $tags"
	echo "Command line is: ./build/lw4o6_tester $fs $r $xpts $to $n $m $delay $tags" >> latencytest.log
	# Execute the test program
	./build/lw4o6_tester $fs $r $xpts $to $n $m $delay $tags > temp.out 2>&1
	# Log and print out info
	cat temp.out >> latencytest.log
	cat temp.out | tail
	# Check for any errors
	error=$(grep 'Error:' temp.out)
        if [ -n "$error" ]; then
		echo "Error occurred, testing must stop."
		exit -1;
	fi
	# Collect and evaluate the results (depending on the direction of the test)
	if [ "$dir" == "b" ]; then
		fwd_TL=$(grep 'forward TL' temp.out | awk '{print $3}')
		fwd_WCL=$(grep 'forward WCL' temp.out | awk '{print $3}')
		rev_TL=$(grep 'reverse TL' temp.out | awk '{print $3}')
		rev_WCL=$(grep 'reverse WCL' temp.out | awk '{print $3}')
	echo "$N, $fs, $dir, $n, $m, $xpts, $r, $to, $delay, $tags, $fwd_TL, $fwd_WCL, $rev_TL, $rev_WCL" >> latency.csv
	fi
	if [ "$dir" == "f" ]; then
                fwd_TL=$(grep 'forward TL' temp.out | awk '{print $3}')
                fwd_WCL=$(grep 'forward WCL' temp.out | awk '{print $3}')
        echo "$N, $fs, $dir, $n, $m, $xpts, $r, $to, $delay, $tags, $fwd_TL, $fwd_WCL" >> latency.csv
	fi
	if [ "$dir" == "r" ]; then
                rev_TL=$(grep 'reverse TL' temp.out | awk '{print $3}')
                rev_WCL=$(grep 'reverse WCL' temp.out | awk '{print $3}')
        echo "$N, $fs, $dir, $n, $m, $xpts, $r, $to, $delay, $tags, $rev_TL, $rev_WCL" >> latency.csv
	fi
	echo "Sleeping for $sleept seconds..."
	echo "Sleeping for $sleept seconds..." >> latencytest.log
	# Sleep for $sleept time to give DUT a chance to relax
	sleep $sleept 
	rm temp*
done # (end of the $no_exp number of experiments)
# Save the results (may be commented out if not needed)
dirname="$res_dir/$(date +$impl'-'$dir'-'$fs'-'$n'-'$m'-'$xpts'-'$r'-'$to'-'$delay'-'$tags'-%F-%H%M')"
mkdir -p $dirname
mv latencytest.log $dirname/
mv latency.csv $dirname/
mv nohup.out $dirname/ 
cp -a lw4o6.conf $dirname/

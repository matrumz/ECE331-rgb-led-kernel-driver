#!/usr/bin/perl
use strict;
use warnings;

# Get proper arguments
die "Enter 2 integer arguments for number of threads & executions/thread\n" 
							unless $#ARGV == 1; 
my $forks = $ARGV[0] - 1;
my $executions = $ARGV[1];

# Fork to number of threads requested
my $pid = 0;
for my $j (1..$forks) {
	$pid = fork() if $pid == 0;
}

my ($rr, $rg, $rb);
my $output;

# Spam ./test with random values the number of times indicated from argument
my $i = 0;
for $i (1..$executions) {
	$rr = int(rand(2048));
	$rg = int(rand(2048));
	$rb = int(rand(2048));
	# Execute ./test and make sure to capture STDIN and STDERR
	$output = `./test $rr $rg $rb 2>&1`;
	# Report if error
	if ($output ne "") { 
		print "EXECUTION $i ERROR FOR PID $pid:\n";
		printf "%04d : %04d : %04d -> %s\n", $rr, $rg, $rb, $output;
		# Clear error for next execution
		$output = "";
	}
}

print "PID $pid did $i executions\n";

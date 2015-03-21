#!/usr/bin/perl
use strict;
use warnings;

# Get proper arguments
die "Enter integer argument for number of executions\n" unless $#ARGV == 0; 
my $executions = $ARGV[0];

my ($rr, $rg, $rb);
my $output;

# Spam ./test with random values the number of times indicated from argument
for my $i (1..$executions) {
	$rr = int(rand(2048));
	$rg = int(rand(2048));
	$rb = int(rand(2048));
	# Execute ./test and make sure to capture STDIN and STDERR
	$output = `./test $rr $rg $rb 2>&1`;
	# Report if error
	if ($output ne "") { 
		print "EXECUTION $i ERROR:\n";
		printf "%04d : %04d : %04d -> %s\n", $rr, $rg, $rb, $output;
		# Clear error for next execution
		$output = "";
	}
}

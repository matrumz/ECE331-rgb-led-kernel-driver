#!/usr/bin/perl
use strict;
use warnings;

die "Enter integer argument for number of executions\n" unless $#ARGV == 0; 
my $executions = $ARGV[0];

my ($rr, $rg, $rb);
my $output;
for my $i (1..$executions) {
	$rr = int(rand(2048));
	$rg = int(rand(2048));
	$rb = int(rand(2048));
	$output = `./test $rr $rg $rb`;
	if ($output ne "") { 
		print "EXECUTION $executions ERROR:\n";
		printf "%04d : %04d : %04d -> %s\n", $rr, $rg, $rb, $output;
	}
}

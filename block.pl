#!/usr/bin/env perl

use warnings;
use strict;

my %types = (
	'water' => 'LIQUID',
	'lava' => 'LIQUID',
);

my %traits = (
	'water' => 'WATER',
	'lava' => 'LAVA',
);

my %names = ('air' => 1);

print <<EOF;
#include <glib.h>

#include "block.h"

struct block_info block_info[256] = {
	[0x00] = {"air", AIR, NO_TRAIT},
EOF

while (my $line = <>)
{
	$line =~ /^\| height="27px" \| .*? \|\| (.*?) \|\| .*? \|\| (.*)$/ or next;
	my $id = $1;
	my $name = lc $2;
	$id =~ s/.*>(.+)<.*/$1/;
	$name =~ s/<sup>.*//;
	$name =~ s/\[\[(.*?\|)?(.*?)\]\]/$2/g;
	$name =~ s/"(.*?)" state/$1/;
	$name =~ s/block of (.*)/$1 block/;
	$name =~ s/brick block/brick/;
	$name =~ s/wooden planks/wood/;
	$name =~ s/saplings/sapling/;
	$name =~ s/glowing redstone ore/redstone ore/;
	$name =~ s/wall sign/sign/;
	$name =~ s/stationary (.*)/$1/;
	$name =~ s/ *$//;
	$names{$name} = 1;
	printf "\t[0x%02x] = {\"%s\", %s, %s_TRAIT},\n",
		$id, $name, $types{$name} || 'SOLID',
		$traits{$name} || 'NO';
}

print <<EOF;
};

char *default_colors[] = {
EOF

my %colored;
open my $colors, '<', 'colors.txt' or die $!;
while (<$colors>)
{
	chomp;
	/^(.*):/ or die "invalid line: $_\n";
	if (!$names{$1})
	{
		print STDERR "invalid block: $1\n";
	}
	print "\t\"$_\",\n";
	$colored{$1} = 1;
}
foreach my $name (sort keys %names)
{
	unless ($colored{$name})
	{
		print STDERR "missing color: $name\n";
	}
}

print <<EOF
	0
};
EOF

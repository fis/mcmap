#! /usr/bin/env perl

# protocol.pl:
# converts protocol.txt into protocol.def

use strict;
use warnings;

# read spec

my %packets;
my $fields = undef;

my %types = (
	'bool' => 'FIELD_BYTE', 'byte' => 'FIELD_BYTE',
	'short' => 'FIELD_SHORT', 'int' => 'FIELD_INT', 'long' => 'FIELD_LONG',
	'float' => 'FIELD_FLOAT', 'double' => 'FIELD_DOUBLE',
	'string' => 'FIELD_STRING',
	'string_utf8' => 'FIELD_STRING_UTF8',
	'item' => 'FIELD_ITEM',
	'byte_array' => 'FIELD_BYTE_ARRAY',
	'block_array' => 'FIELD_BLOCK_ARRAY',
	'item_array' => 'FIELD_ITEM_ARRAY',
	'explosion_array' => 'FIELD_EXPLOSION_ARRAY',
	'map_array' => 'FIELD_MAP_ARRAY',
	'entity_data' => 'FIELD_ENTITY_DATA',
	'object_data' => 'FIELD_OBJECT_DATA'
	);

while (my $line = <>)
{
	chomp $line;

	if ($line =~ /^(\w+): ([x0-9a-fA-F]+)$/)
	{
		my ($name, $id) = ($1, hex $2);

		$fields = [];
		$packets{$id} = { 'name' => $name, 'fields' => $fields };
	}
	elsif ($line =~ /^- (.*?)(?::|$)/)
	{
		die "fieldspec without a packet: $line" unless defined $fields;

		my $fspec = $1;
		$fspec =~ /^\s*(\w+)\s+(\w+)\s*$/ or die "bad fieldspec: $fspec";
		my ($ftype, $fname) = ($1, $2);

		my $type = $types{$ftype} or die "unknown type: $ftype";
		push @$fields, [$type, $fname];
	}
	elsif ($line =~ /^\S/)
	{
		die "bad protocol spec line: $line";
	}
}

my @packets = sort { $a <=> $b } keys %packets;

# write X-Macros to file

foreach my $id (@packets)
{
	my ($name, @fields) = ($packets{$id}->{name}, @{$packets{$id}->{fields}});
	my $cname = uc $name;
	my $scmname = $name;
	$scmname =~ s/_/-/g;

	printf "PACKET(0x%02x, %s, \"%s\", %d", $id, $cname, $scmname, scalar @fields;
	if (@fields)
	{
		foreach my $field (@fields)
		{
			my ($ftype, $fname) = @$field;
			my $cfname = uc $fname;
			my $scmfname = $fname;
			$scmfname =~ s/_/-/g;
			printf ", FIELD(%s, %s, \"%s\")", $ftype, $cfname, $scmfname;
		}
	}
	else
	{
		print ", 0";
	}
	print ")\n";
}

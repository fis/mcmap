#! /usr/bin/env perl

# protocol.pl:
# converts protocol.txt into protocol-data.{c,h}

use strict;
use warnings;

open SPEC, '<:utf8', 'protocol.txt' or die "can't read spec: $!";
open CODE, '>:utf8', 'protocol-data.c' or die "can't write code: $!";
open HEADER, '>:utf8', 'protocol-data.h' or die "can't write header: $!";

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
	'entity_data' => 'FIELD_ENTITY_DATA'
	);

while (my $line = <SPEC>)
{
	chomp $line;

	if ($line =~ /^(\w+): ([x0-9a-fA-F]+)$/)
	{
		my ($name, $id) = (uc $1, hex $2);

		$fields = [];
		$packets{$id} = { 'name' => $name, 'fields' => $fields };
	}
	elsif ($line =~ /^- (.*?)(?::|$)/)
	{
		die "fieldspec without a packet: $line" unless defined $fields;

		my $fspec = $1;
		foreach my $field (split /\s*,\s*/, $fspec)
		{
			my $type = $types{$field} or die "unknown type: $field";
			push @$fields, $type;
		}
	}
	elsif ($line =~ /^\S/)
	{
		die "bad protocol spec line: $line";
	}
}

my @packets = sort { $a <=> $b } keys %packets;

# write packet_id enum to header

print HEADER "enum packet_id {\n";

foreach my $id (@packets)
{
	printf HEADER "  PACKET_%s = 0x%02x,\n", $packets{$id}->{'name'}, $id;
}

print HEADER "};\n";

# write packet_format array to code

foreach my $id (@packets)
{
	my ($name, $fields) = ($packets{$id}->{'name'}, $packets{$id}->{'fields'});
	next unless @$fields;

	print CODE "static enum field_type packet_format_${name}[] = { ";
	print CODE join(', ', @$fields);
	print CODE " };\n";
}

print CODE "struct packet_format_desc packet_format[] = {\n";

foreach my $id (@packets)
{
	my ($name, $fields) = ($packets{$id}->{'name'}, $packets{$id}->{'fields'});
	print CODE "  [PACKET_$name] = ";
	if (@$fields)
	{
		printf CODE '{ %d, packet_format_%s, 1 }', scalar @$fields, $name;
	}
	else
	{
		print CODE '{ 0, 0, 1 }';
	}
	print CODE ",\n";
}

print CODE "};\n";

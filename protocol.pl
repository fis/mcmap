#!/usr/bin/env perl

use strict;
use warnings;

my $state = 'none';
my @fields;
my $special;

my $debug = (shift || '') eq '-v';

my %types = (
	'bool' => 'FIELD_BYTE',
	'boolean' => 'FIELD_BYTE',
	'byte' => 'FIELD_BYTE',
	'ubyte' => 'FIELD_UBYTE',
	'unsigned byte' => 'FIELD_UBYTE',
	'short' => 'FIELD_SHORT',
	'int' => 'FIELD_INT',
	'long' => 'FIELD_LONG',
	'float' => 'FIELD_FLOAT',
	'double' => 'FIELD_DOUBLE',
	'string' => 'FIELD_STRING',
	'[[slot_data|slot]]' => 'FIELD_ITEM',
	'metadata' => 'FIELD_ENTITY_DATA',
);

my %specials = (
	'ADD_OBJECT_OR_VEHICLE' => [ 5, 'FIELD_OBJECT_DATA' ],
	'MAP_CHUNK' => [ 6, 'FIELD_BYTE_ARRAY' ],
	'MULTI_BLOCK_CHANGE' => [ 2, 'FIELD_BLOCK_ARRAY' ],
	'EXPLOSION' => [ 4, 'FIELD_EXPLOSION_ARRAY' ],
	'WINDOW_ITEMS' => [ 1, 'FIELD_ITEM_ARRAY' ],
	'ITEM_DATA' => [ 2, 'FIELD_MAP_ARRAY' ],
);

sub sanitise
{
	my $_ = uc shift;
	s/\// OR /g;
	s/&AMP;/AND/g;
	s/&/AND/g;
	s/[^\w]+/_/g;
	s/^_+|_+$//g;
	return $_;
}

while (my $line = <>)
{
	if ($state eq 'none' && $line =~ /^=== (.*) \((0x..)\) ===$/)
	{
		my $name = sanitise $1;
		my $id = $2;
		print "PACKET($id, $name, ";
		print STDERR "$name: $id\n" if $debug;
		$state = 'packet';
		$special = $specials{$name};
	}
	elsif ($state eq 'packet' && $line =~ /^\|- class="row1"$/)
	{
		$state = 'fields';
		@fields = ();
	}

	# if, not elsif; we want the row1 header from the previous transition
	if ($state eq 'fields')
	{
		if ($line =~ /^\|- class="row.*"$/)
		{
			<> if @fields == 0;
			$line = <>;
			if ($line ne "|}\n")
			{
				$line =~ /^\| class=".*" \| (.*)$/ or next;
				my $name = sanitise($1) || 'unknown';
				$line = <>;
				$line =~ /^\| class=".*" \| (.*)$/ or next;
				my $type = $types{lc $1} or die "unknown field type $1";
				next if $1 =~ /^\d/;
				push @fields, { 'type' => $type, 'name' => $name };
			}
		}

		if (defined $special)
		{
			my ($pos, $type) = @$special;
			if (@fields == $pos)
			{
				push @fields, { 'type' => $type, 'name' => 'DATA' };
				while ($line ne "|}\n")
				{
					$line = <>;
				}
			}
		}

		# again if for fall-through
		if ($line eq "|}\n")
		{
			print scalar @fields;
			if (@fields)
			{
				foreach my $field (@fields)
				{
					print STDERR "- $field->{type} $field->{name}\n" if $debug;
					print ", FIELD($field->{type}, $field->{name})";
				}
			}
			else
			{
				print ", 0";
			}
			print STDERR "\n" if $debug;
			print ")\n";
			$state = 'none';
		}
	}
}

Introduction
============

*mcmap* is a simple little glib/SDL utility that works as a proxy to
Minecraft, reads the connection traffic, and visualizes a map in a
window.

There are three main modes: a surface map (useful for general
navigation), a cross-section (single altitude level) map (useful for
finding things that are hidden in the ground) and a topographical
height-map (useful for... I don't know).

Also there are some miscellaneous features, like client-side
teleporting (though don't try it with health on).

Building
========

There's a Makefile; if you have SDL and glib dev packages installed,
and pkg-config knows where to find them, a simple `make` should
suffice.

Usage
=====

Command line
------------

In the most basic form, `./mcmap -s 600x600 host:port` (or just `host`
for the default Minecraft port 25565).  The `-s` command line option
specifies the window size.  If you leave it out, the window will be
resizable, but resize events are not handled, so something bad will
probably happen.  (Fixing this is on the hypothetical TODO list.)

Do `./mcmap -h` for a list of options.

After starting up, connect with Minecraft.  The program will
automatically exit when you disconnect from within Minecraft.

Visuals
-------

At the default zoom level, the map window has one pixel for each block
in the world.  Block colors are hand-assigned, and should be more or
less sensible: edit in map.c if you wish.  Not all block types are
listed: unlisted blocks will be black.

The map is centered around the player (not scrollable at the moment),
and the position (and look direction) of the player is indicated with
a tiny little pink triangle marker.  Other players are indicated with
3x3 pink rectangles.

In the height-map, heights in the [64, 127] range are indicated with a
gradient from yellow and red, while heights in [0, 63] use a gradient
from blue to yellow.  (This was inspired by pynemap.)

Keys
----

Map modes are selected with the number keys:

* `1`: surface map, topmost non-air block.
* `2`: cross-section map, follows player altitude.
* `3`: cross-section map, doesn't follow the player.
* `4`: topographical map.

In addition, the up/down arrow keys can be used to set the altitude
level in the cross-section map modes.  This works also in mode 2,
though moving around will reset the altitude.

The page-up/page-down keys control the zoom level.  Each press of
page-up makes blocks one pixel larger.

Right-clicking a point on the map basically executes a //goto x z on
the clicked coordinates (or nearby).

Commands
--------

Commands are prefixed with // -- chat packets starting with that
prefix will be removed from the stream and considered by mcmap.

* `//goto x z`: teleport into coordinates (x, z).

The teleporting works by first moving the player directly up to height
y=128, then moving to (x, 128, z).  Passing through solid blocks is
not possible, so teleporting will only work if you have clear view of
the sky.  Additionally you'll fall down from height 128, which is
likely to prove fatal if you're playing on a server where health is
on.

(Also, don't teleport into some ridiculously far-away coordinates.
The server seems to generate all the terrain on your flight-path, and
will probably crash.)

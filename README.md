Introduction
============

*mcmap* is a simple little glib/SDL utility that works as a proxy to
Minecraft, reads the connection traffic, and visualizes a map in a
window.

There are two main modes: a surface map (useful for general
navigation) and a cross-section (single altitude level) map (useful
for finding things that are hidden in the ground).

There are also several more complicated ideas that might or might not
get implemented later on.

Building
========

There's a Makefile; if you have SDL and glib dev packages installed,
and pkg-config knows where to find them, a simple `make` should
suffice.

Usage
=====

Command line
------------

In the most basic form, `./mcmap -r 600x600 host:port` (or just `host`
for the default Minecraft port 25565).  The `-r` command line option
specifies the window size.  If you leave it out, the window will be
resizable, but resize events are not handled, so something bad will
probably happen.  (Fixing this is on the hypothetical TODO list.)

Do `./mcmap -h` for a list of options.

After starting up, connect with Minecraft.  The program will
automatically exit when you disconnect from within Minecraft.

Visuals
-------

The map window has one pixel for each block in the world.  Block
colors are hand-assigned, and should be more or less sensible: edit in
map.c if you wish.  Not all block types are listed: unlisted blocks
will be black.

The map is centered around the player (not scrollable at the moment),
and the position (and look direction) of the player is indicated with
a tiny little pink triangle marker.  Other players are indicated with
3x3 pink rectangles.

Keys
----

Map modes are selected with the number keys:

* `1`: surface map, topmost non-air block.
* `2`: cross-section map, follows player altitude.
* `3`: cross-section map, doesn't follow the player.

In addition, the up/down arrow keys can be used to set the altitude
level in the cross-section map modes.  This works also in mode 2,
though moving around will reset the altitude.

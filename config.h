#ifndef MCMAP_CONFIG_H
#define MCMAP_CONFIG_H

/* mcmap compile-time options */

/*
 * FEAT_FULLCHUNK: Keep full (metadata + lighting) chunk information
 * in memory.  Increases memory consumption to 2.5x what it would
 * otherwise be, but is necessary for the //save command.
 */
#define FEAT_FULLCHUNK 1

#endif /* MCMAP_CONFIG_H */

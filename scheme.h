#ifndef MCMAP_SCHEME_H
#define MCMAP_SCHEME_H

/* Note that this duplicates the packet; further modifications will
   not be reflected in the Scheme object. */
SCM make_packet_smob(packet_t *p);
void init_scheme(void);

#endif /* MCMAP_SCHEME_H */

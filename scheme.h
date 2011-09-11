#ifndef MCMAP_SCHEME_H
#define MCMAP_SCHEME_H

SCM sym_client;
SCM sym_server;

/* Note that this duplicates the packet; further modifications will
   not be reflected in the Scheme object. */
SCM make_packet_smob(packet_t *p);
SCM scheme_make_packet(SCM type_symbol, SCM rest);
SCM scheme_packet_type(SCM packet_smob);
SCM scheme_packet_field(SCM packet_smob, SCM field_name);
SCM scheme_packet_fields(SCM packet_smob);
SCM scheme_packet_inject(SCM inject_to, SCM packet_smob);
SCM scheme_packet_hook(SCM type_symbol);
void init_scheme(void);

#endif /* MCMAP_SCHEME_H */

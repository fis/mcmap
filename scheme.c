#include <glib.h>
#include <libguile.h>

#include "protocol.h"
#include "scheme.h"

SCM_SMOB(scm_tc16_packet_type, "packet", sizeof(packet_t));

SCM_SMOB_FREE(scm_tc16_packet_type, smob_packet_free, packet_smob)
{
	packet_t *p = (packet_t *) SCM_SMOB_DATA(packet_smob);
	packet_free(p);
	return 0;
}

SCM_SMOB_PRINT(scm_tc16_packet_type, smob_packet_print, packet_smob, port, pstate)
{
	/* TODO: Better printing function */
	packet_t *p = (packet_t *) SCM_SMOB_DATA(packet_smob);
	scm_puts("#<packet ", port);
	scm_display(scm_from_uint(p->type), port);
	scm_puts(">", port);
	return 1;
}

SCM_SMOB_EQUALP(scm_tc16_packet_type, smob_packet_equalp, packet_smob_a, packet_smob_b)
{
	/* TODO: Better equality function */
	return scm_eq_p(packet_smob_a, packet_smob_b);
}

SCM make_packet_smob(packet_t *p)
{
	SCM_RETURN_NEWSMOB(scm_tc16_packet_type, packet_dup(p));
}

SCM_DEFINE(scheme_packet_type, "packet-type", 1, 0, 0, (SCM packet_smob),
	"Return the type of the packet.")
#define FUNC_NAME "packet-type"
{
	SCM_VALIDATE_SMOB(1, packet_smob, packet_type);
	scm_remember_upto_here_1(packet_smob);
	return scm_take_locale_symbol("dunno");
}
#undef FUNC_NAME

void init_scheme()
{
	#ifndef SCM_MAGIC_SNARFER
	#include "build/scheme.x"
	#endif
}

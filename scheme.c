#include <glib.h>
#include <libguile.h>

#include "protocol.h"
#include "scheme.h"

static scm_t_bits packet_tag;

static size_t smob_packet_free(SCM packet_smob)
{
	packet_t *p = (packet_t *) SCM_SMOB_DATA(packet_smob);
	packet_free(p);
	return 0;
}

static int smob_packet_print(SCM packet_smob, SCM port, scm_print_state *pstate)
{
	/* TODO: Better printing function */
	packet_t *p = (packet_t *) SCM_SMOB_DATA(packet_smob);
	scm_puts("#<packet ", port);
	scm_display(scm_from_uint(p->type), port);
	scm_puts(">", port);
	return 1;
}

static SCM smob_packet_equalp(SCM packet_smob_a, SCM packet_smob_b)
{
	/* TODO: Better equality function */
	return scm_eq_p(packet_smob_a, packet_smob_b);
}

SCM make_packet_smob(packet_t *p)
{
	SCM smob;
	SCM_NEWSMOB(smob, packet_tag, packet_dup(p));
	return smob;
}

void init_scheme()
{
	packet_tag = scm_make_smob_type("packet", sizeof(packet_t));
	scm_set_smob_free(packet_tag, smob_packet_free);
	scm_set_smob_print(packet_tag, smob_packet_print);
	scm_set_smob_equalp(packet_tag, smob_packet_equalp);
}

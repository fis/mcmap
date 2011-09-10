#include <glib.h>
#include <libguile.h>

#include "protocol.h"
#include "scheme.h"

static SCM packet_type_to_symbol;
static SCM symbol_to_packet_type;

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
	scm_puts("#<packet ", port);
	scm_display(scheme_packet_type(packet_smob), port);
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

SCM_DEFINE(scheme_make_packet, "make-packet", 1, 0, 0, (SCM type_symbol),
	"Make a new packet.")
#define FUNC_NAME "make-packet"
{
	SCM_VALIDATE_SYMBOL(1, type_symbol);

	SCM type = scm_hash_ref(symbol_to_packet_type, type_symbol, SCM_BOOL_F);
	if (scm_is_eq(type, SCM_BOOL_F))
		scm_out_of_range(FUNC_NAME, type_symbol);

	packet_constructor_t pc = packet_create(scm_to_uint(type));
	SCM_RETURN_NEWSMOB(scm_tc16_packet_type, packet_construct(&pc));
}
#undef FUNC_NAME

SCM_DEFINE(scheme_packet_type, "packet-type", 1, 0, 0, (SCM packet_smob),
	"Return the type of the packet.")
#define FUNC_NAME "packet-type"
{
	SCM_VALIDATE_SMOB(1, packet_smob, packet_type);
	packet_t *p = (packet_t *) SCM_SMOB_DATA(packet_smob);
	return scm_hash_ref(packet_type_to_symbol, scm_from_uint(p->type), SCM_UNDEFINED);
}
#undef FUNC_NAME

SCM_DEFINE(scheme_packet_fields, "packet-fields", 1, 0, 0, (SCM packet_smob),
	"Return the fields of the packet as a vector.")
#define FUNC_NAME "packet-fields"
{
	SCM_VALIDATE_SMOB(1, packet_smob, packet_type);
	packet_t *p = (packet_t *) SCM_SMOB_DATA(packet_smob);
    
	SCM fields = scm_c_make_vector(packet_nfields(p), SCM_BOOL_F);

	scm_t_array_handle handle;
	size_t i;
	size_t len;
	ssize_t inc;
	SCM *elt = scm_vector_writable_elements(fields, &handle, &len, &inc);

	for (i = 0; i < len; i++, elt += inc)
	{
		*elt = SCM_BOOL_T;
	}

	scm_array_handle_release(&handle);

	return fields;
}
#undef FUNC_NAME

void init_scheme()
{
	packet_type_to_symbol = scm_permanent_object(scm_make_hash_table(SCM_UNDEFINED));
	symbol_to_packet_type = scm_permanent_object(scm_make_hash_table(SCM_UNDEFINED));

	SCM packet_id;
	SCM packet_scmname;
	#define PACKET(id, cname, scmname, nfields, ...) \
		packet_id = scm_from_uint(id); \
		packet_scmname = scm_from_locale_symbol(scmname); \
		scm_hash_set_x(packet_type_to_symbol, packet_id, packet_scmname); \
		scm_hash_set_x(symbol_to_packet_type, packet_scmname, packet_id);
	#include "protocol.x"
	#undef PACKET

	#ifndef SCM_MAGIC_SNARFER
	#include "build/scheme.x"
	#endif
}

#include <glib.h>
#include <libguile.h>

#include "common.h"
#include "protocol.h"
#include "proxy.h"
#include "scheme.h"

/* This is a really ugly copy-and-modify from Guile to avoid -pedantic
   violations. TODO FIXME: Something nicer than this. */

#undef SCM_STATIC_SUBR_OBJVECT
#define SCM_STATIC_SUBR_OBJVECT(c_name, foreign) \
	static SCM_ALIGNED(8) SCM c_name[4] = { \
		SCM_PACK(scm_tc7_vector | (2 << 8)), \
		SCM_PACK(0), \
		foreign, \
		SCM_BOOL_F, /* the name */ \
	}

SCM_GLOBAL_SYMBOL(sym_client, "client");
SCM_GLOBAL_SYMBOL(sym_server, "server");

#define PACKET(id, cname, scmname, nfields, ...) \
	SCM_SYMBOL(sym_packet_##cname, scmname);
#include "protocol.x"
#undef PACKET

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
	scm_puts(" ", port);
	scm_write(scheme_packet_fields(packet_smob), port);
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

SCM_DEFINE(scheme_make_packet, "make-packet", 1, 0, 1, (SCM type_symbol, SCM rest),
	"Make a new packet.")
#define FUNC_NAME "make-packet"
{
	SCM_VALIDATE_SYMBOL(1, type_symbol);

	SCM type_scm = scm_hash_ref(symbol_to_packet_type, type_symbol, SCM_BOOL_F);
	if (scm_is_eq(type_scm, SCM_BOOL_F))
		SCM_OUT_OF_RANGE(1, type_symbol);

	unsigned type = scm_to_uint(type_scm);
	packet_constructor_t pc = packet_create(type);
	struct packet_format_desc fmt = packet_format[type];
	unsigned field;
	unsigned nargs = 1;

	for (field = 0; field < fmt.nfields && !scm_is_eq(rest, SCM_EOL); field++, rest = scm_cdr(rest))
	{
		nargs++;
		SCM v = scm_car(rest);
		char *s;

		#define INTEGRAL(t) \
			SCM_ASSERT_TYPE(scm_is_integer(v), v, field + 2, FUNC_NAME, "integer"); \
			packet_add_##t(&pc, scm_to_int64(v))

		#define FLOATING(t) \
			SCM_VALIDATE_REAL(field + 2, v); \
			packet_add_##t(&pc, scm_to_double(v))

		#define STRING(t) \
			SCM_VALIDATE_STRING(field + 2, v); \
			s = scm_to_utf8_string(v); \
			packet_add_##t(&pc, (unsigned char *) s); \
			g_free(s)

		switch (fmt.ftype[field])
		{
		case FIELD_BYTE: INTEGRAL(jbyte); break;
		case FIELD_SHORT: INTEGRAL(jshort); break;
		case FIELD_INT: INTEGRAL(jint); break;
		case FIELD_LONG: INTEGRAL(jlong); break;

		case FIELD_FLOAT: FLOATING(jfloat); break;
		case FIELD_DOUBLE: FLOATING(jdouble); break;

		case FIELD_STRING: STRING(string); break;
		case FIELD_STRING_UTF8: STRING(string_utf8); break;

		default:
			packet_constructor_free(&pc);
			scm_error(scm_out_of_range_key,
				FUNC_NAME,
				"Packets of type ~A cannot yet be constructed",
				scm_list_1(type_symbol),
				SCM_BOOL_F);
		}

		#undef INTEGRAL
		#undef FLOATING
		#undef STRING
	}

	if (field < fmt.nfields || !scm_is_eq(rest, SCM_EOL))
	{
		packet_constructor_free(&pc);
		scm_error(scm_args_number_key,
			FUNC_NAME,
			"Wrong number of arguments to " FUNC_NAME " for packet type ~A; expected ~A but received ~A",
			scm_list_3(type_symbol, scm_from_uint(fmt.nfields + 1), scm_sum(scm_from_uint(nargs), scm_length(rest))),
			SCM_BOOL_F);
	}

	SCM_RETURN_NEWSMOB(scm_tc16_packet_type, packet_construct(&pc));
}
#undef FUNC_NAME

SCM_DEFINE(scheme_packet_type, "packet-type", 1, 0, 0, (SCM packet_smob),
	"Return the type of the packet.")
#define FUNC_NAME "packet-type"
{
	SCM_VALIDATE_SMOB(1, packet_smob, packet_type);
	packet_t *p = (packet_t *) SCM_SMOB_DATA(packet_smob);
	switch (p->type)
	{
	#define PACKET(id, cname, scmname, nfields, ...) \
		case id: \
			return sym_packet_##cname;
	#include "protocol.x"
	#undef PACKET

	default:
		wtff("Invalid packet type %u", p->type);
	}
}
#undef FUNC_NAME

SCM_DEFINE(scheme_packet_fields, "packet-fields", 1, 0, 0, (SCM packet_smob),
	"Return the fields of the packet as a vector.")
#define FUNC_NAME "packet-fields"
{
	SCM_VALIDATE_SMOB(1, packet_smob, packet_type);
	packet_t *p = (packet_t *) SCM_SMOB_DATA(packet_smob);
	struct packet_format_desc fmt = packet_format[p->type];

	SCM fields = scm_c_make_vector(fmt.nfields, SCM_BOOL_F);

	scm_t_array_handle handle;
	size_t len;
	ssize_t inc;
	SCM *elt = scm_vector_writable_elements(fields, &handle, &len, &inc);

	for (size_t i = 0; i < len; i++, elt += inc)
	{
		switch (fmt.ftype[i])
		{
		case FIELD_BYTE:
		case FIELD_SHORT:
		case FIELD_INT:
		case FIELD_LONG:
			*elt = scm_from_int64(packet_long(p, i));
			break;

		case FIELD_FLOAT:
		case FIELD_DOUBLE:
			*elt = scm_from_double(packet_double(p, i));
			break;

		case FIELD_STRING:
		case FIELD_STRING_UTF8:
			{
				struct buffer buf = packet_string(p, i);
				*elt = scm_from_utf8_stringn((char *) buf.data, buf.len);
				g_free(buf.data);
			}
			break;

		default:
			/* just leave it as #f */
			break;
		}
	}

	scm_array_handle_release(&handle);

	return fields;
}
#undef FUNC_NAME

SCM_DEFINE(scheme_packet_inject, "packet-inject", 2, 0, 0, (SCM inject_to, SCM packet_smob),
	"Inject a packet to the server or client.")
#define FUNC_NAME "packet-inject"
{
	SCM_VALIDATE_SYMBOL(1, inject_to);
	SCM_VALIDATE_SMOB(2, packet_smob, packet_type);

	packet_t *p = (packet_t *) SCM_SMOB_DATA(packet_smob);
	if (scm_is_eq(inject_to, sym_client))
		inject_to_client(packet_dup(p));
	else if (scm_is_eq(inject_to, sym_server))
		inject_to_server(packet_dup(p));
	else
		SCM_OUT_OF_RANGE(1, inject_to);

	return SCM_UNSPECIFIED;
}
#undef FUNC_NAME

SCM_DEFINE(scheme_packet_hook, "packet-hook", 1, 0, 0, (SCM type_symbol),
	"Return the hook for the given packet type.")
#define FUNC_NAME "packet-hook"
{
	SCM_VALIDATE_SYMBOL(1, type_symbol);

	SCM type_scm = scm_hash_ref(symbol_to_packet_type, type_symbol, SCM_BOOL_F);
	if (scm_is_eq(type_scm, SCM_BOOL_F))
		SCM_OUT_OF_RANGE(1, type_symbol);

	return packet_hooks[scm_to_uint(type_scm)];
}
#undef FUNC_NAME

void init_scheme()
{
	symbol_to_packet_type = scm_permanent_object(scm_make_hash_table(SCM_UNDEFINED));

	#define PACKET(id, cname, scmname, nfields, ...) \
		scm_hash_set_x(symbol_to_packet_type, scm_from_utf8_symbol(scmname), scm_from_uint(id));
	#include "protocol.x"
	#undef PACKET

	#ifndef SCM_MAGIC_SNARFER
	#include "scheme.x"
	#endif
}

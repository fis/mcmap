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
#include "protocol.def"
#undef PACKET

static SCM packet_field_symbol_names[256][16];
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
	SCM *field_names = packet_field_symbol_names[type];

	unsigned field = 0;
	bool in_keywords = false;
	unsigned filled_fields = 0;

	while (filled_fields < fmt.nfields && !scm_is_eq(rest, SCM_EOL))
	{
		SCM v;
		SCM car = scm_car(rest);
		if (scm_is_keyword(car))
		{
			in_keywords = true;
			SCM kw_sym = scm_keyword_to_symbol(car);
			bool ok = false;
			for (size_t i = 0; i < fmt.nfields; i++)
			{
				if (scm_is_eq(kw_sym, field_names[i]))
				{
					field = i;
					ok = true;
					break;
				}
			}
			if (!ok)
			{
				scm_error(scm_out_of_range_key,
					FUNC_NAME,
					"Invalid field name for packet type ~A: ~A",
					scm_list_2(type_symbol, kw_sym),
					SCM_BOOL_F);
			}
			rest = scm_cdr(rest);
			v = scm_car(rest);
		}
		else
		{
			if (in_keywords)
				scm_error(scm_arg_type_key, FUNC_NAME, "Expected keyword: ~A", scm_list_1(car), SCM_BOOL_F);
			else
				v = car;
		}

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

		if (!in_keywords)
			field++;

		filled_fields++;
		rest = scm_cdr(rest);
	}

	if (filled_fields < fmt.nfields || !scm_is_eq(rest, SCM_EOL))
	{
		packet_constructor_free(&pc);
		scm_error(scm_args_number_key,
			FUNC_NAME,
			"Wrong number of arguments to " FUNC_NAME " for packet type ~A; expected ~A but received ~A",
			scm_list_3(
				type_symbol, scm_from_uint(fmt.nfields + 1),
				scm_sum(scm_from_uint(filled_fields + 1), scm_length(rest))),
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
	#include "protocol.def"
	#undef PACKET

	default:
		wtff("Invalid packet type %u", p->type);
	}

	scm_remember_upto_here_1(packet_smob);
}
#undef FUNC_NAME

SCM_DEFINE(scheme_packet_field, "packet-field", 2, 0, 0, (SCM packet_smob, SCM field_name),
	"Return the given field of the packet.")
#define FUNC_NAME "packet-field"
{
	SCM_VALIDATE_SMOB(1, packet_smob, packet_type);
	SCM_VALIDATE_SYMBOL(1, field_name);

	packet_t *p = (packet_t *) SCM_SMOB_DATA(packet_smob);
	struct packet_format_desc fmt = packet_format[p->type];
	SCM *field_names = packet_field_symbol_names[p->type];

	/* This is O(n), but n is something like 7 at most, so it doesn't
	   really matter. */
	for (size_t i = 0; i < fmt.nfields; i++)
	{
		if (scm_is_eq(field_names[i], field_name))
		{
			switch (fmt.ftype[i])
			{
			case FIELD_BYTE:
			case FIELD_SHORT:
			case FIELD_INT:
			case FIELD_LONG:
				return scm_from_int64(packet_long(p, i));

			case FIELD_FLOAT:
			case FIELD_DOUBLE:
				return scm_from_double(packet_double(p, i));

			case FIELD_STRING:
			case FIELD_STRING_UTF8:
				{
					struct buffer buf = packet_string(p, i);
					SCM value = scm_from_utf8_stringn((char *) buf.data, buf.len);
					g_free(buf.data);
					return value;
				}

			default:
				return SCM_BOOL_F;
			}
		}
	}

	scm_error(scm_out_of_range_key,
		FUNC_NAME,
		"Invalid field name for packet type ~A: ~A",
		scm_list_2(scheme_packet_type(packet_smob), field_name),
		SCM_BOOL_F);
}
#undef FUNC_NAME

SCM_DEFINE(scheme_packet_fields, "packet-fields", 1, 0, 0, (SCM packet_smob),
	"Return the fields of the packet as a vector.")
#define FUNC_NAME "packet-fields"
{
	SCM_VALIDATE_SMOB(1, packet_smob, packet_type);
	packet_t *p = (packet_t *) SCM_SMOB_DATA(packet_smob);
	struct packet_format_desc fmt = packet_format[p->type];
	SCM *field_names = packet_field_symbol_names[p->type];

	SCM fields = scm_cons(SCM_BOOL_F, SCM_EOL);
	SCM field_ptr = fields;

	for (size_t i = 0; i < fmt.nfields; i++)
	{
		/* This is inefficient, but this function is inefficient in general, so who cares */
		SCM value = scheme_packet_field(packet_smob, field_names[i]);
		SCM new_field_ptr = scm_cons(scm_cons(field_names[i], value), SCM_EOL);
		scm_set_cdr_x(field_ptr, new_field_ptr);
		field_ptr = new_field_ptr;
	}

	return scm_cdr(fields);
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

	scm_remember_upto_here_1(packet_smob);
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
	unsigned packet_id;
	unsigned field_num;

	#define PACKET(id, cname, scmname, nfields, ...) \
		/* terminate the last field_num++ of the previous packet */ \
		; \
		packet_id = id; \
		field_num = 0; \
		/* packets with no fields have a dummy "0" variable argument for strict
		   C99 compliance. we use (void) here so that clang doesn't generate
		   a warning about an unused expression as it's expanded here */ \
		(void) __VA_ARGS__
	#define FIELD(type, cname, scmname) \
		/* parentheses around this assignment so that the previous (void) cast
		   doesn't mess it up */ \
		(packet_field_symbol_names[packet_id][field_num] = scm_from_utf8_symbol(scmname)); \
		field_num++ /* a comma appears next */
	#include "protocol.def"
	/* terminate the very last field_num++ */
	;
	#undef FIELD
	#undef PACKET

	symbol_to_packet_type = scm_permanent_object(scm_make_hash_table(SCM_UNDEFINED));

	#define PACKET(id, cname, scmname, nfields, ...) \
		scm_hash_set_x(symbol_to_packet_type, scm_from_utf8_symbol(scmname), scm_from_uint(id));
	#include "protocol.def"
	#undef PACKET

	#ifndef SCM_MAGIC_SNARFER
	#include "scheme.x"
	#endif
}

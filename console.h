#ifndef MCMAP_CONSOLE_H
#define MCMAP_CONSOLE_H 1

void console_init(void);
extern int console_outfd;

/* logging and information */

void log_print(char *fmt, ...) __attribute__((format(printf, 1, 2)));
void log_die(char *fmt, ...) __attribute__((noreturn, format(printf, 1, 2)));

/* fatal error handling */

/* GNU comma pasting is not used in the *f variants to increase
   standards-compliance. Since the non-*f functions work perfectly in
   this case, it's not a big deal. */

#define dief(fmt, ...) log_die(fmt, __VA_ARGS__)
#define die(msg) dief("%s", msg)

#define wtff(fmt, ...) dief("%s:%d: " fmt, __FILE__, __LINE__, __VA_ARGS__)
#define wtf(msg) wtff("%s", msg)

#endif /* MCMAP_CONSOLE_H */

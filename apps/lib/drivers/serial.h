#ifndef USER_SERIAL_H
#define USER_SERIAL_H

/* No serial logging in userland — make it a no-op. */
static inline void serial_log(const char *msg) { (void)msg; }

#endif

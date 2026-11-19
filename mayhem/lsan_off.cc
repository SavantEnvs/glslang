// Leaks are not the bug class this fleet fuzzes for; disable LeakSanitizer at build time.
extern "C" int __lsan_is_turned_off() { return 1; }

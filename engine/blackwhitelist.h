// Load domains from list file (0 on success)
int blackwhitelist_load_list(const char *filename);

// Check hostname against list, returns match status
int blackwhitelist_check_hostname(const char *host_addr, size_t host_len);

// Add a single hostname at runtime (TRUE if it was new)
int blackwhitelist_add_hostname(const char *host);

// --skip-list: hosts that must be passed through untouched
int skiplist_load_list(const char *filename);
int skiplist_check_hostname(const char *host_addr, size_t host_len);

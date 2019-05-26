/* Shared object handling */
int plugin_framing_load(char* path);
int plugin_backend_load(char* backend_requested, ws_backend* backend);

/* Framing function registry */
int plugin_register_framing(char* name, ws_framing func);
ws_framing plugin_framing(char* name);

/* Module management */
void plugin_cleanup();

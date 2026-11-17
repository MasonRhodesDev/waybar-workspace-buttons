#include "waybar_cffi_module.h"
#include <gtk-layer-shell/gtk-layer-shell.h>
#include <gdk/gdkwayland.h>
#include <wayland-client.h>
#include "xdg-output-unstable-v1-client-protocol.h"
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <pthread.h>

#define NUM_WORKSPACES 9
#define DEFAULT_TERTIARY_COLOR "#adc8f8"

typedef struct {
    wbcffi_module* waybar_module;
    const wbcffi_init_info* init_info;
    GtkBox* container;
    GtkButton* buttons[NUM_WORKSPACES];
    GtkLabel* labels[NUM_WORKSPACES];
    GtkLabel* dot_labels[NUM_WORKSPACES];

    int all_outputs;
    int show_empty;

    char tertiary_color[16];

    char monitor_name[64];

    int this_monitor_workspace;
    int user_focused_here;
    int detect_retries;
    int workspace_windows[NUM_WORKSPACES];
    int special_windows[NUM_WORKSPACES];
    char workspace_monitor[NUM_WORKSPACES][64];

    pthread_t ipc_thread;
    int thread_started;
    volatile int running;
    int socket_fd;
} WorkspaceModule;

const size_t wbcffi_version = 2;

static void update_button_states(WorkspaceModule* mod);
static void* ipc_monitor_thread(void* arg);
static void on_button_clicked(GtkButton* button, gpointer user_data);
static void fetch_initial_state(WorkspaceModule* mod);
static void load_tertiary_color(WorkspaceModule* mod);
static gboolean detect_monitor_idle(gpointer user_data);
static void handle_event(WorkspaceModule* mod, const char* event);
static void refresh_window_counts(WorkspaceModule* mod);
static void refresh_workspace_monitors(WorkspaceModule* mod);
static void refresh_active_workspace(WorkspaceModule* mod);

static void popen_string(const char* cmd, char* output, size_t output_size) {
    FILE* fp = popen(cmd, "r");
    if (!fp) {
        output[0] = '\0';
        return;
    }
    if (fgets(output, output_size, fp) == NULL) {
        output[0] = '\0';
    }
    size_t len = strlen(output);
    if (len > 0 && output[len-1] == '\n') {
        output[len-1] = '\0';
    }
    pclose(fp);
}

static int popen_int(const char* cmd) {
    FILE* fp = popen(cmd, "r");
    if (!fp) return 0;

    int result = 0;
    if (fscanf(fp, "%d", &result) != 1) {
        result = 0;
    }
    pclose(fp);
    return result;
}

static void load_tertiary_color(WorkspaceModule* mod) {
    strncpy(mod->tertiary_color, DEFAULT_TERTIARY_COLOR, sizeof(mod->tertiary_color));

    FILE* fp = popen("/usr/bin/lmtt tokens --key tertiary", "r");
    if (!fp) {
        return;
    }

    char hex[16];
    if (fgets(hex, sizeof(hex), fp)) {
        size_t n = strcspn(hex, "\r\n");
        hex[n] = '\0';
        if (hex[0] == '#' && n == 7) {
            snprintf(mod->tertiary_color, sizeof(mod->tertiary_color), "%s", hex);
        }
    }
    pclose(fp);
}

static void start_ipc_thread(WorkspaceModule* mod) {
    if (mod->thread_started) return;
    if (pthread_create(&mod->ipc_thread, NULL, ipc_monitor_thread, mod) == 0) {
        mod->thread_started = 1;
    } else {
        fprintf(stderr, "workspace_buttons: Failed to start IPC thread\n");
    }
}

static void finish_detection(WorkspaceModule* mod) {
    fetch_initial_state(mod);
    update_button_states(mod);
    start_ipc_thread(mod);
}

typedef struct {
    struct zxdg_output_manager_v1* manager;
    char name[64];
} OutputNameProbe;

static void probe_registry_global(void* data, struct wl_registry* registry, uint32_t id,
                                  const char* interface, uint32_t version) {
    OutputNameProbe* probe = data;
    if (strcmp(interface, zxdg_output_manager_v1_interface.name) == 0 && version >= 2) {
        uint32_t bind_version = version < 3 ? version : 3;
        probe->manager = wl_registry_bind(registry, id, &zxdg_output_manager_v1_interface,
                                          bind_version);
    }
}

static void probe_registry_global_remove(void* data, struct wl_registry* registry, uint32_t id) {
    (void)data; (void)registry; (void)id;
}

static const struct wl_registry_listener probe_registry_listener = {
    .global = probe_registry_global,
    .global_remove = probe_registry_global_remove,
};

static void probe_xdg_output_logical_position(void* data, struct zxdg_output_v1* output,
                                              int32_t x, int32_t y) {
    (void)data; (void)output; (void)x; (void)y;
}

static void probe_xdg_output_logical_size(void* data, struct zxdg_output_v1* output,
                                          int32_t width, int32_t height) {
    (void)data; (void)output; (void)width; (void)height;
}

static void probe_xdg_output_done(void* data, struct zxdg_output_v1* output) {
    (void)data; (void)output;
}

static void probe_xdg_output_description(void* data, struct zxdg_output_v1* output,
                                         const char* description) {
    (void)data; (void)output; (void)description;
}

static void probe_xdg_output_name(void* data, struct zxdg_output_v1* output, const char* name) {
    (void)output;
    OutputNameProbe* probe = data;
    size_t n = 0;
    for (const char* r = name; *r && n < sizeof(probe->name) - 1; r++) {
        if (isalnum((unsigned char)*r) || *r == '-' || *r == '_') {
            probe->name[n++] = *r;
        }
    }
    probe->name[n] = '\0';
}

static const struct zxdg_output_v1_listener probe_xdg_output_listener = {
    .logical_position = probe_xdg_output_logical_position,
    .logical_size = probe_xdg_output_logical_size,
    .done = probe_xdg_output_done,
    .name = probe_xdg_output_name,
    .description = probe_xdg_output_description,
};

static void resolve_output_name(GdkMonitor* monitor, char* out, size_t out_size) {
    out[0] = '\0';

    GdkDisplay* gdk_display = gdk_monitor_get_display(monitor);
    if (!GDK_IS_WAYLAND_DISPLAY(gdk_display) || !GDK_IS_WAYLAND_MONITOR(monitor)) return;
    struct wl_display* display = gdk_wayland_display_get_wl_display(gdk_display);
    struct wl_output* output = gdk_wayland_monitor_get_wl_output(monitor);
    if (!display || !output) return;

    struct wl_event_queue* queue = wl_display_create_queue(display);
    if (!queue) return;

    struct wl_display* display_wrapper = wl_proxy_create_wrapper(display);
    if (!display_wrapper) {
        wl_event_queue_destroy(queue);
        return;
    }
    wl_proxy_set_queue((struct wl_proxy*)display_wrapper, queue);
    struct wl_registry* registry = wl_display_get_registry(display_wrapper);
    wl_proxy_wrapper_destroy(display_wrapper);
    if (!registry) {
        wl_event_queue_destroy(queue);
        return;
    }

    OutputNameProbe probe = {0};
    wl_registry_add_listener(registry, &probe_registry_listener, &probe);
    if (wl_display_roundtrip_queue(display, queue) >= 0 && probe.manager) {
        struct zxdg_output_v1* xdg_output =
            zxdg_output_manager_v1_get_xdg_output(probe.manager, output);
        if (xdg_output) {
            zxdg_output_v1_add_listener(xdg_output, &probe_xdg_output_listener, &probe);
            wl_display_roundtrip_queue(display, queue);
            zxdg_output_v1_destroy(xdg_output);
        }
    }
    if (probe.manager) zxdg_output_manager_v1_destroy(probe.manager);
    wl_registry_destroy(registry);
    wl_event_queue_destroy(queue);

    snprintf(out, out_size, "%s", probe.name);
}

static gboolean detect_monitor_idle(gpointer user_data) {
    WorkspaceModule* mod = (WorkspaceModule*)user_data;

    if (mod->thread_started) {
        return G_SOURCE_REMOVE;
    }

    if (mod->monitor_name[0] != '\0') {
        fprintf(stderr, "workspace_buttons: event=wsb.detect source=config monitor=%s\n",
                mod->monitor_name);
        finish_detection(mod);
        return G_SOURCE_REMOVE;
    }

    GtkWidget* toplevel = gtk_widget_get_toplevel(GTK_WIDGET(mod->container));
    const char* source = NULL;
    const char* reason = NULL;
    const char* model = "";
    char xdg_name[64] = "";
    GdkRectangle geom = {0, 0, 0, 0};

    if (!GTK_IS_WINDOW(toplevel) || !gtk_layer_is_layer_window(GTK_WINDOW(toplevel))) {
        reason = "no-layer-window";
    } else {
        GdkMonitor* monitor = gtk_layer_get_monitor(GTK_WINDOW(toplevel));
        if (!monitor) {
            if (mod->detect_retries < 5) {
                mod->detect_retries++;
                fprintf(stderr, "workspace_buttons: event=wsb.detect.retry reason=no-gdk-monitor attempt=%d/5\n",
                        mod->detect_retries);
                g_timeout_add(400, detect_monitor_idle, mod);
                return G_SOURCE_REMOVE;
            }
            reason = "no-gdk-monitor";
        } else {
            gdk_monitor_get_geometry(monitor, &geom);
            const char* m = gdk_monitor_get_model(monitor);
            if (m) model = m;

            char cmd[256];

            resolve_output_name(monitor, xdg_name, sizeof(xdg_name));
            if (xdg_name[0] != '\0') {
                snprintf(cmd, sizeof(cmd),
                         "hyprctl monitors -j | jq -r '.[] | select(.name == \"%s\") | .name' 2>/dev/null",
                         xdg_name);
                popen_string(cmd, mod->monitor_name, sizeof(mod->monitor_name));
                if (mod->monitor_name[0] != '\0') {
                    source = "xdg-output";
                } else {
                    reason = "xdg-output-name-unknown";
                }
            } else {
                reason = "no-xdg-output-name";
            }

            if (!source) {
                snprintf(cmd, sizeof(cmd),
                         "hyprctl monitors -j | jq -r '.[] | select(.x == %d and .y == %d) | .name' 2>/dev/null",
                         geom.x, geom.y);
                popen_string(cmd, mod->monitor_name, sizeof(mod->monitor_name));
                if (mod->monitor_name[0] != '\0') {
                    source = "layer-shell-geometry";
                } else {
                    reason = "no-monitor-at";
                }
            }
        }
    }

    if (!source) {
        popen_string("hyprctl monitors -j | jq -r '.[] | select(.focused == true) | .name' 2>/dev/null",
                     mod->monitor_name, sizeof(mod->monitor_name));
        fprintf(stderr, "workspace_buttons: event=wsb.detect source=fallback-focused reason=%s xdg_name=\"%s\" gdk_x=%d gdk_y=%d monitor=%s\n",
                reason, xdg_name, geom.x, geom.y, mod->monitor_name);
    } else if (reason) {
        fprintf(stderr, "workspace_buttons: event=wsb.detect source=%s reason=%s xdg_name=\"%s\" gdk_x=%d gdk_y=%d gdk_model=\"%s\" monitor=%s\n",
                source, reason, xdg_name, geom.x, geom.y, model, mod->monitor_name);
    } else {
        fprintf(stderr, "workspace_buttons: event=wsb.detect source=%s xdg_name=\"%s\" gdk_x=%d gdk_y=%d gdk_model=\"%s\" monitor=%s\n",
                source, xdg_name, geom.x, geom.y, model, mod->monitor_name);
    }

    finish_detection(mod);
    return G_SOURCE_REMOVE;
}

static void on_widget_map(GtkWidget* widget, gpointer user_data) {
    g_idle_add(detect_monitor_idle, user_data);
}

static void refresh_active_workspace(WorkspaceModule* mod) {
    char cmd[256];

    if (mod->monitor_name[0] == '\0') return;

    snprintf(cmd, sizeof(cmd),
             "hyprctl monitors -j | jq -r '.[] | select(.name == \"%s\") | \"\\(.activeWorkspace.id) \\(.focused)\"' 2>/dev/null",
             mod->monitor_name);
    char line[64];
    popen_string(cmd, line, sizeof(line));
    if (line[0] == '\0') return;

    int ws;
    char focused[8];
    if (sscanf(line, "%d %7s", &ws, focused) != 2) return;

    mod->this_monitor_workspace = ws;
    mod->user_focused_here = (strcmp(focused, "true") == 0);
}

static void fetch_initial_state(WorkspaceModule* mod) {
    char cmd[256];

    if (mod->monitor_name[0] != '\0') {
        refresh_active_workspace(mod);
    } else {
        mod->this_monitor_workspace = popen_int("hyprctl activeworkspace -j | jq -r '.id' 2>/dev/null");
        mod->user_focused_here = 1;
    }

    memset(mod->workspace_windows, 0, sizeof(mod->workspace_windows));
    memset(mod->special_windows, 0, sizeof(mod->special_windows));
    memset(mod->workspace_monitor, 0, sizeof(mod->workspace_monitor));

    for (int i = 1; i <= NUM_WORKSPACES; i++) {
        snprintf(cmd, sizeof(cmd),
                 "hyprctl workspaces -j | jq -r '.[] | select(.id == %d) | .monitor' 2>/dev/null", i);
        popen_string(cmd, mod->workspace_monitor[i-1], sizeof(mod->workspace_monitor[i-1]));
    }

    for (int i = 1; i <= NUM_WORKSPACES; i++) {
        snprintf(cmd, sizeof(cmd),
                 "hyprctl clients -j | jq '[.[] | select(.workspace.id == %d)] | length' 2>/dev/null", i);
        mod->workspace_windows[i - 1] = popen_int(cmd);
    }

    for (int i = 1; i <= NUM_WORKSPACES; i++) {
        snprintf(cmd, sizeof(cmd),
                 "hyprctl clients -j | jq '[.[] | select(.workspace.name == \"special:%d\")] | length' 2>/dev/null", i);
        mod->special_windows[i - 1] = popen_int(cmd);
    }
}

static void refresh_window_counts(WorkspaceModule* mod) {
    memset(mod->workspace_windows, 0, sizeof(mod->workspace_windows));
    memset(mod->special_windows, 0, sizeof(mod->special_windows));

    FILE* fp = popen("hyprctl clients -j 2>/dev/null", "r");
    if (!fp) return;

    char buffer[32768];
    size_t total = 0;
    size_t bytes;
    while ((bytes = fread(buffer + total, 1, sizeof(buffer) - total - 1, fp)) > 0) {
        total += bytes;
        if (total >= sizeof(buffer) - 1) break;
    }
    buffer[total] = '\0';
    pclose(fp);

    const char* ptr = buffer;
    while ((ptr = strstr(ptr, "\"workspace\"")) != NULL) {
        const char* id_ptr = strstr(ptr, "\"id\":");
        const char* name_ptr = strstr(ptr, "\"name\":");

        if (id_ptr && (name_ptr == NULL || id_ptr < name_ptr)) {
            id_ptr += 5;
            while (*id_ptr == ' ') id_ptr++;
            int ws_id = atoi(id_ptr);

            if (ws_id >= 1 && ws_id <= NUM_WORKSPACES) {
                mod->workspace_windows[ws_id - 1]++;
            }
        }

        if (name_ptr) {
            name_ptr += 7;
            while (*name_ptr == ' ' || *name_ptr == '"') name_ptr++;
            if (strncmp(name_ptr, "special:", 8) == 0) {
                int special_id = atoi(name_ptr + 8);
                if (special_id >= 1 && special_id <= NUM_WORKSPACES) {
                    mod->special_windows[special_id - 1]++;
                }
            }
        }

        ptr++;
    }
}

static void refresh_workspace_monitors(WorkspaceModule* mod) {
    memset(mod->workspace_monitor, 0, sizeof(mod->workspace_monitor));

    FILE* fp = popen("hyprctl workspaces -j 2>/dev/null", "r");
    if (!fp) return;

    char buffer[8192];
    size_t total = 0;
    size_t bytes;
    while ((bytes = fread(buffer + total, 1, sizeof(buffer) - total - 1, fp)) > 0) {
        total += bytes;
        if (total >= sizeof(buffer) - 1) break;
    }
    buffer[total] = '\0';
    pclose(fp);

    const char* ptr = buffer;
    while ((ptr = strstr(ptr, "\"id\":")) != NULL) {
        ptr += 5;
        int ws_id = atoi(ptr);

        const char* mon_ptr = strstr(ptr, "\"monitor\":");
        const char* next_id = strstr(ptr, "\"id\":");

        if (mon_ptr && (next_id == NULL || mon_ptr < next_id)) {
            mon_ptr += 10;
            while (*mon_ptr == ' ' || *mon_ptr == '"') mon_ptr++;

            if (ws_id >= 1 && ws_id <= NUM_WORKSPACES) {
                char* dest = mod->workspace_monitor[ws_id - 1];
                int i = 0;
                while (*mon_ptr && *mon_ptr != '"' && i < 63) {
                    dest[i++] = *mon_ptr++;
                }
                dest[i] = '\0';
            }
        }
    }
}

static void handle_event(WorkspaceModule* mod, const char* event) {
    if (mod->monitor_name[0] == '\0') {
        return;
    }

    if (strncmp(event, "workspace>>", 11) == 0) {
        int ws = atoi(event + 11);
        if (ws >= 1 && ws <= NUM_WORKSPACES) {
            const char* ws_monitor = mod->workspace_monitor[ws - 1];
            if (ws_monitor[0] != '\0' && strcmp(ws_monitor, mod->monitor_name) == 0) {
                mod->this_monitor_workspace = ws;
            }
        }
        return;
    }

    if (strncmp(event, "focusedmon>>", 12) == 0) {
        char mon[64];
        int ws;
        const char* comma = strchr(event + 12, ',');
        if (comma) {
            size_t mon_len = comma - (event + 12);
            if (mon_len < sizeof(mon)) {
                strncpy(mon, event + 12, mon_len);
                mon[mon_len] = '\0';
                ws = atoi(comma + 1);

                mod->user_focused_here = (strcmp(mon, mod->monitor_name) == 0);

                if (mod->user_focused_here && ws >= 1 && ws <= NUM_WORKSPACES) {
                    mod->this_monitor_workspace = ws;
                }
            }
        }
        return;
    }

    if (strncmp(event, "activespecial>>", 15) == 0) {
        refresh_window_counts(mod);
        return;
    }

    if (strncmp(event, "openwindow>>", 12) == 0 ||
        strncmp(event, "closewindow>>", 13) == 0 ||
        strncmp(event, "movewindow>>", 12) == 0) {
        refresh_window_counts(mod);
        return;
    }

    if (strncmp(event, "createworkspace>>", 17) == 0 ||
        strncmp(event, "destroyworkspace>>", 18) == 0) {
        refresh_workspace_monitors(mod);
        return;
    }

    if (strncmp(event, "moveworkspace>>", 15) == 0) {
        refresh_workspace_monitors(mod);
        refresh_active_workspace(mod);
        return;
    }
}

static int should_show_workspace(WorkspaceModule* mod, int ws_index) {
    int ws_num = ws_index + 1;
    int is_this_monitor_ws = (ws_num == mod->this_monitor_workspace);
    int has_windows = (mod->workspace_windows[ws_index] > 0);
    int has_special = (mod->special_windows[ws_index] > 0);
    int on_this_monitor = (mod->all_outputs ||
                           mod->monitor_name[0] == '\0' ||
                           strcmp(mod->workspace_monitor[ws_index], mod->monitor_name) == 0 ||
                           mod->workspace_monitor[ws_index][0] == '\0');

    if (is_this_monitor_ws) return 1;

    if (!on_this_monitor) return 0;

    if (!mod->show_empty && !has_windows && !has_special) return 0;

    return 1;
}

static gboolean update_ui_callback(gpointer user_data) {
    WorkspaceModule* mod = (WorkspaceModule*)user_data;
    update_button_states(mod);
    return G_SOURCE_REMOVE;
}

static void update_button_states(WorkspaceModule* mod) {
    for (int i = 0; i < NUM_WORKSPACES; i++) {
        GtkStyleContext* ctx = gtk_widget_get_style_context(GTK_WIDGET(mod->buttons[i]));

        int should_show = should_show_workspace(mod, i);
        gtk_widget_set_visible(GTK_WIDGET(mod->buttons[i]), should_show);

        if (!should_show) continue;

        gtk_style_context_remove_class(ctx, "active");
        gtk_style_context_remove_class(ctx, "visible");
        gtk_style_context_remove_class(ctx, "empty");
        gtk_style_context_remove_class(ctx, "has-special");

        if ((i + 1) == mod->this_monitor_workspace) {
            if (mod->user_focused_here) {
                gtk_style_context_add_class(ctx, "active");
            } else {
                gtk_style_context_add_class(ctx, "visible");
            }
        }

        if (mod->workspace_windows[i] == 0 && mod->special_windows[i] == 0) {
            gtk_style_context_add_class(ctx, "empty");
        }

        if (mod->special_windows[i] > 0) {
            gtk_style_context_add_class(ctx, "has-special");
        }

        if (mod->special_windows[i] > 0) {
            gtk_widget_show(GTK_WIDGET(mod->dot_labels[i]));
        } else {
            gtk_widget_hide(GTK_WIDGET(mod->dot_labels[i]));
        }
    }
}

static void on_button_clicked(GtkButton* button, gpointer user_data) {
    int workspace = GPOINTER_TO_INT(user_data);
    char cmd[128];
    snprintf(cmd, sizeof(cmd), "hyprctl dispatch 'hl.dsp.focus({workspace = %d})'", workspace);
    system(cmd);
}

static int connect_hyprland_socket(void) {
    const char* xdg_runtime = getenv("XDG_RUNTIME_DIR");
    const char* hypr_sig = getenv("HYPRLAND_INSTANCE_SIGNATURE");

    if (!xdg_runtime || xdg_runtime[0] != '/' || !hypr_sig) {
        fprintf(stderr, "workspace_buttons: Missing Hyprland environment variables\n");
        return -1;
    }

    char socket_path[256];
    snprintf(socket_path, sizeof(socket_path), "%s/hypr/%s/.socket2.sock", xdg_runtime, hypr_sig);

    int fd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (fd < 0) {
        perror("workspace_buttons: socket");
        return -1;
    }

    struct sockaddr_un addr;
    memset(&addr, 0, sizeof(addr));
    addr.sun_family = AF_UNIX;
    strncpy(addr.sun_path, socket_path, sizeof(addr.sun_path) - 1);

    if (connect(fd, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
        perror("workspace_buttons: connect");
        close(fd);
        return -1;
    }

    return fd;
}

static void* ipc_monitor_thread(void* arg) {
    WorkspaceModule* mod = (WorkspaceModule*)arg;
    char buffer[4096];
    size_t pending = 0;
    int discarding = 0;

    mod->socket_fd = connect_hyprland_socket();
    if (mod->socket_fd < 0) {
        fprintf(stderr, "workspace_buttons: Failed to connect to Hyprland socket\n");
        return NULL;
    }
    if (!mod->running) {
        close(mod->socket_fd);
        return NULL;
    }

    while (mod->running) {
        ssize_t bytes = read(mod->socket_fd, buffer + pending, sizeof(buffer) - pending - 1);
        if (bytes <= 0) {
            if (mod->running) {
                close(mod->socket_fd);
                sleep(1);
                mod->socket_fd = connect_hyprland_socket();
                pending = 0;
                discarding = 0;
            }
            continue;
        }

        pending += (size_t)bytes;
        buffer[pending] = '\0';

        char* line = buffer;
        char* next;
        int needs_update = 0;

        if (discarding) {
            next = strchr(line, '\n');
            if (!next) {
                pending = 0;
                continue;
            }
            line = next + 1;
            discarding = 0;
        }

        while ((next = strchr(line, '\n')) != NULL) {
            *next = '\0';

            if (*line) {
                handle_event(mod, line);
                needs_update = 1;
            }

            line = next + 1;
        }

        pending = strlen(line);
        if (pending >= sizeof(buffer) - 1) {
            pending = 0;
            discarding = 1;
        } else if (pending > 0 && line != buffer) {
            memmove(buffer, line, pending);
        }

        if (needs_update) {
            g_idle_add(update_ui_callback, mod);
        }
    }

    close(mod->socket_fd);
    return NULL;
}

static int parse_bool(const char* value) {
    if (!value) return 0;
    if (strcmp(value, "true") == 0) return 1;
    if (strcmp(value, "false") == 0) return 0;
    return atoi(value) != 0;
}

void* wbcffi_init(const wbcffi_init_info* init_info, const wbcffi_config_entry* config_entries,
                  size_t config_entries_len) {

    WorkspaceModule* mod = calloc(1, sizeof(WorkspaceModule));
    mod->waybar_module = init_info->obj;
    mod->init_info = init_info;
    mod->running = 1;
    mod->this_monitor_workspace = 1;
    mod->user_focused_here = 1;

    mod->all_outputs = 0;
    mod->show_empty = 0;

    for (size_t i = 0; i < config_entries_len; i++) {
        if (strcmp(config_entries[i].key, "all-outputs") == 0) {
            mod->all_outputs = parse_bool(config_entries[i].value);
        } else if (strcmp(config_entries[i].key, "show-empty") == 0) {
            mod->show_empty = parse_bool(config_entries[i].value);
        } else if (strcmp(config_entries[i].key, "output") == 0) {
            const char* val = config_entries[i].value;
            if (val[0] == '"') val++;
            strncpy(mod->monitor_name, val, sizeof(mod->monitor_name) - 1);
            size_t len = strlen(mod->monitor_name);
            if (len > 0 && mod->monitor_name[len-1] == '"') {
                mod->monitor_name[len-1] = '\0';
            }
            char* w = mod->monitor_name;
            for (const char* r = mod->monitor_name; *r; r++) {
                if (isalnum((unsigned char)*r) || *r == '-' || *r == '_') {
                    *w++ = *r;
                }
            }
            *w = '\0';
        }
    }

    fprintf(stderr, "workspace_buttons: Config - all-outputs=%d, show-empty=%d\n",
            mod->all_outputs, mod->show_empty);

    load_tertiary_color(mod);

    GtkContainer* root = init_info->get_root_widget(init_info->obj);

    mod->container = GTK_BOX(gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0));
    gtk_widget_set_name(GTK_WIDGET(mod->container), "workspaces");
    gtk_container_add(root, GTK_WIDGET(mod->container));

    g_signal_connect(mod->container, "map", G_CALLBACK(on_widget_map), mod);

    for (int i = 0; i < NUM_WORKSPACES; i++) {
        char label[8];
        snprintf(label, sizeof(label), "%d", i + 1);

        mod->buttons[i] = GTK_BUTTON(gtk_button_new());
        GtkOverlay* overlay = GTK_OVERLAY(gtk_overlay_new());

        mod->labels[i] = GTK_LABEL(gtk_label_new(label));
        gtk_widget_set_halign(GTK_WIDGET(mod->labels[i]), GTK_ALIGN_CENTER);
        gtk_widget_set_valign(GTK_WIDGET(mod->labels[i]), GTK_ALIGN_CENTER);
        gtk_container_add(GTK_CONTAINER(overlay), GTK_WIDGET(mod->labels[i]));

        char dot_markup[64];
        snprintf(dot_markup, sizeof(dot_markup),
                 "<span font_size='5000' color='%s'>●</span>", mod->tertiary_color);
        mod->dot_labels[i] = GTK_LABEL(gtk_label_new(NULL));
        gtk_label_set_markup(mod->dot_labels[i], dot_markup);
        gtk_widget_set_halign(GTK_WIDGET(mod->dot_labels[i]), GTK_ALIGN_END);
        gtk_widget_set_valign(GTK_WIDGET(mod->dot_labels[i]), GTK_ALIGN_START);
        gtk_widget_set_no_show_all(GTK_WIDGET(mod->dot_labels[i]), TRUE);
        gtk_overlay_add_overlay(overlay, GTK_WIDGET(mod->dot_labels[i]));

        gtk_container_add(GTK_CONTAINER(mod->buttons[i]), GTK_WIDGET(overlay));

        gtk_button_set_relief(mod->buttons[i], GTK_RELIEF_NONE);
        gtk_widget_set_can_focus(GTK_WIDGET(mod->buttons[i]), FALSE);

        g_signal_connect(mod->buttons[i], "clicked",
                         G_CALLBACK(on_button_clicked), GINT_TO_POINTER(i + 1));

        gtk_container_add(GTK_CONTAINER(mod->container), GTK_WIDGET(mod->buttons[i]));
    }

    gtk_widget_show_all(GTK_WIDGET(mod->container));

    fprintf(stderr, "workspace_buttons: Initialized (tertiary=%s)\n", mod->tertiary_color);
    return mod;
}

void wbcffi_deinit(void* instance) {
    WorkspaceModule* mod = (WorkspaceModule*)instance;

    mod->running = 0;
    if (mod->socket_fd > 0) {
        shutdown(mod->socket_fd, SHUT_RDWR);
    }
    if (mod->thread_started) {
        pthread_join(mod->ipc_thread, NULL);
    }

    while (g_source_remove_by_user_data(mod)) { }

    free(mod);
    fprintf(stderr, "workspace_buttons: Deinitialized\n");
}

void wbcffi_update(void* instance) {
}

void wbcffi_refresh(void* instance, int signal) {
    WorkspaceModule* mod = (WorkspaceModule*)instance;
    load_tertiary_color(mod);
    fetch_initial_state(mod);
    update_button_states(mod);
}

void wbcffi_doaction(void* instance, const char* action_name) {
}

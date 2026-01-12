/**
 * Waybar Workspace Buttons - CFFI Module for Hyprland
 *
 * Creates workspace buttons with:
 * - Active workspace highlighting
 * - Empty workspace hiding (configurable)
 * - Per-monitor filtering (configurable)
 * - Special workspace dot indicator (has-special class + visual dot)
 * - Click to switch workspace
 * - Real-time updates via Hyprland IPC socket
 *
 * Config options:
 *   all-outputs: bool (default: false) - Show workspaces from all monitors
 *   show-empty: bool (default: false) - Show empty workspaces
 */

#include "waybar_cffi_module.h"
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
    GtkLabel* dot_labels[NUM_WORKSPACES];  // Separate dot indicators

    // Configuration
    int all_outputs;      // Show workspaces from all monitors
    int show_empty;       // Show empty workspaces

    // Tertiary color for dot indicator
    char tertiary_color[16];

    // Monitor name for this waybar instance
    char monitor_name[64];

    // State
    int this_monitor_workspace;  // Workspace displayed on THIS module's monitor
    int user_focused_here;       // Is user focused on THIS monitor?
    int workspace_windows[NUM_WORKSPACES];    // Window count per workspace
    int special_windows[NUM_WORKSPACES];      // Window count per special:N
    char workspace_monitor[NUM_WORKSPACES][64]; // Monitor name per workspace

    // Thread for IPC monitoring
    pthread_t ipc_thread;
    volatile int running;
    int socket_fd;
} WorkspaceModule;

const size_t wbcffi_version = 2;

// Forward declarations
static void update_button_states(WorkspaceModule* mod);
static void* ipc_monitor_thread(void* arg);
static void on_button_clicked(GtkButton* button, gpointer user_data);
static void fetch_initial_state(WorkspaceModule* mod);
static void load_tertiary_color(WorkspaceModule* mod);
static gboolean detect_monitor_idle(gpointer user_data);
static void handle_event(WorkspaceModule* mod, const char* event);
static void refresh_window_counts(WorkspaceModule* mod);
static void refresh_workspace_monitors(WorkspaceModule* mod);

// Helper to run command and get string output
static void popen_string(const char* cmd, char* output, size_t output_size) {
    FILE* fp = popen(cmd, "r");
    if (!fp) {
        output[0] = '\0';
        return;
    }
    if (fgets(output, output_size, fp) == NULL) {
        output[0] = '\0';
    }
    // Remove trailing newline
    size_t len = strlen(output);
    if (len > 0 && output[len-1] == '\n') {
        output[len-1] = '\0';
    }
    pclose(fp);
}

// Helper to run command and get integer output
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

// Load tertiary color from matugen CSS
static void load_tertiary_color(WorkspaceModule* mod) {
    FILE* fp = popen("grep -oP '@define-color tertiary \\K#[0-9a-fA-F]+' ~/.config/matugen/lmtt-colors.css 2>/dev/null", "r");
    if (fp) {
        if (fscanf(fp, "%15s", mod->tertiary_color) != 1) {
            strncpy(mod->tertiary_color, DEFAULT_TERTIARY_COLOR, sizeof(mod->tertiary_color));
        }
        pclose(fp);
    } else {
        strncpy(mod->tertiary_color, DEFAULT_TERTIARY_COLOR, sizeof(mod->tertiary_color));
    }
}

// Detect which monitor this waybar instance is on (called from idle to ensure positioning is complete)
static gboolean detect_monitor_idle(gpointer user_data) {
    WorkspaceModule* mod = (WorkspaceModule*)user_data;

    // If monitor was set from config, use that
    if (mod->monitor_name[0] != '\0') {
        fprintf(stderr, "workspace_buttons: Using configured monitor: %s\n", mod->monitor_name);
        fetch_initial_state(mod);
        update_button_states(mod);
        return G_SOURCE_REMOVE;
    }

    GtkWidget* widget = GTK_WIDGET(mod->container);
    GtkWidget* toplevel = gtk_widget_get_toplevel(widget);

    // Get the toplevel window's allocated width - this matches the waybar surface width
    GtkAllocation alloc;
    gtk_widget_get_allocation(toplevel, &alloc);

    // Match by width - query Hyprland layers to find which monitor has a waybar with this width
    char cmd[256];
    snprintf(cmd, sizeof(cmd),
             "hyprctl layers -j | jq -r 'to_entries[] | .key as $mon | .value.levels | to_entries[] | .value[] | select(.namespace == \"waybar\" and .w == %d) | $mon' 2>/dev/null | head -1",
             alloc.width);
    popen_string(cmd, mod->monitor_name, sizeof(mod->monitor_name));

    // Fallback: get focused monitor if detection failed
    if (mod->monitor_name[0] == '\0') {
        popen_string("hyprctl monitors -j | jq -r '.[] | select(.focused == true) | .name' 2>/dev/null",
                     mod->monitor_name, sizeof(mod->monitor_name));
    }

    fprintf(stderr, "workspace_buttons: Detected monitor: %s\n", mod->monitor_name);

    // Now update state with correct monitor filtering
    fetch_initial_state(mod);
    update_button_states(mod);

    return G_SOURCE_REMOVE;
}

// Callback to schedule monitor detection after widget is mapped (positioned)
static void on_widget_map(GtkWidget* widget, gpointer user_data) {
    // Use idle callback to ensure window positioning is complete
    g_idle_add(detect_monitor_idle, user_data);
}

// Parse workspace state from hyprctl using jq
static void fetch_initial_state(WorkspaceModule* mod) {
    char cmd[256];

    // Get THIS monitor's active workspace and focus state
    if (mod->monitor_name[0] != '\0') {
        snprintf(cmd, sizeof(cmd),
                 "hyprctl monitors -j | jq -r '.[] | select(.name == \"%s\") | .activeWorkspace.id' 2>/dev/null",
                 mod->monitor_name);
        mod->this_monitor_workspace = popen_int(cmd);

        snprintf(cmd, sizeof(cmd),
                 "hyprctl monitors -j | jq -r '.[] | select(.name == \"%s\") | .focused' 2>/dev/null",
                 mod->monitor_name);
        char focused[8];
        popen_string(cmd, focused, sizeof(focused));
        mod->user_focused_here = (strcmp(focused, "true") == 0);
    } else {
        // Fallback if monitor not yet detected
        mod->this_monitor_workspace = popen_int("hyprctl activeworkspace -j | jq -r '.id' 2>/dev/null");
        mod->user_focused_here = 1;
    }

    // Reset state
    memset(mod->workspace_windows, 0, sizeof(mod->workspace_windows));
    memset(mod->special_windows, 0, sizeof(mod->special_windows));
    memset(mod->workspace_monitor, 0, sizeof(mod->workspace_monitor));

    // Get workspace monitor assignments
    for (int i = 1; i <= NUM_WORKSPACES; i++) {
        snprintf(cmd, sizeof(cmd),
                 "hyprctl workspaces -j | jq -r '.[] | select(.id == %d) | .monitor' 2>/dev/null", i);
        popen_string(cmd, mod->workspace_monitor[i-1], sizeof(mod->workspace_monitor[i-1]));
    }

    // Count windows per regular workspace (1-9)
    for (int i = 1; i <= NUM_WORKSPACES; i++) {
        snprintf(cmd, sizeof(cmd),
                 "hyprctl clients -j | jq '[.[] | select(.workspace.id == %d)] | length' 2>/dev/null", i);
        mod->workspace_windows[i - 1] = popen_int(cmd);
    }

    // Count windows per special workspace (special:1 through special:9)
    for (int i = 1; i <= NUM_WORKSPACES; i++) {
        snprintf(cmd, sizeof(cmd),
                 "hyprctl clients -j | jq '[.[] | select(.workspace.name == \"special:%d\")] | length' 2>/dev/null", i);
        mod->special_windows[i - 1] = popen_int(cmd);
    }
}

// Batched refresh: update all window counts with single hyprctl call
static void refresh_window_counts(WorkspaceModule* mod) {
    // Reset counts
    memset(mod->workspace_windows, 0, sizeof(mod->workspace_windows));
    memset(mod->special_windows, 0, sizeof(mod->special_windows));

    // Single hyprctl call - read all client data at once
    FILE* fp = popen("hyprctl clients -j 2>/dev/null", "r");
    if (!fp) return;

    // Read entire output
    char buffer[32768];
    size_t total = 0;
    size_t bytes;
    while ((bytes = fread(buffer + total, 1, sizeof(buffer) - total - 1, fp)) > 0) {
        total += bytes;
        if (total >= sizeof(buffer) - 1) break;
    }
    buffer[total] = '\0';
    pclose(fp);

    // Parse workspace IDs from JSON - look for "workspace":{"id":N patterns
    // This is a simple parser that counts occurrences
    const char* ptr = buffer;
    while ((ptr = strstr(ptr, "\"workspace\"")) != NULL) {
        // Look for "id": followed by a number
        const char* id_ptr = strstr(ptr, "\"id\":");
        const char* name_ptr = strstr(ptr, "\"name\":");

        if (id_ptr && (name_ptr == NULL || id_ptr < name_ptr)) {
            id_ptr += 5; // skip "id":
            while (*id_ptr == ' ') id_ptr++;
            int ws_id = atoi(id_ptr);

            // Regular workspace (1-9)
            if (ws_id >= 1 && ws_id <= NUM_WORKSPACES) {
                mod->workspace_windows[ws_id - 1]++;
            }
        }

        // Check for special workspace by name
        if (name_ptr) {
            name_ptr += 7; // skip "name":
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

// Batched refresh: update workspace-to-monitor mapping
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

    // Parse workspace entries - look for "id":N and "monitor":"NAME" pairs
    const char* ptr = buffer;
    while ((ptr = strstr(ptr, "\"id\":")) != NULL) {
        ptr += 5;
        int ws_id = atoi(ptr);

        // Find associated monitor
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

// Handle a single event from Hyprland socket (fast, no subprocess spawning)
static void handle_event(WorkspaceModule* mod, const char* event) {
    // workspace>>N - switched to workspace N on focused monitor
    if (strncmp(event, "workspace>>", 11) == 0) {
        int ws = atoi(event + 11);
        if (ws >= 1 && ws <= NUM_WORKSPACES) {
            // Only update if this is our monitor
            // The workspace event is for the focused monitor
            mod->this_monitor_workspace = ws;
            mod->user_focused_here = 1;
        }
        return;
    }

    // focusedmon>>MONITOR,WORKSPACE - focus changed to different monitor
    if (strncmp(event, "focusedmon>>", 12) == 0) {
        char mon[64];
        int ws;
        // Parse "DP-4,2" format
        const char* comma = strchr(event + 12, ',');
        if (comma) {
            size_t mon_len = comma - (event + 12);
            if (mon_len < sizeof(mon)) {
                strncpy(mon, event + 12, mon_len);
                mon[mon_len] = '\0';
                ws = atoi(comma + 1);

                // Update focus state for this module
                int was_focused = mod->user_focused_here;
                mod->user_focused_here = (strcmp(mon, mod->monitor_name) == 0);

                // If focus moved TO this monitor, update active workspace
                if (mod->user_focused_here && ws >= 1 && ws <= NUM_WORKSPACES) {
                    mod->this_monitor_workspace = ws;
                }

                // If focus state changed, need to update UI
                if (was_focused != mod->user_focused_here) {
                    return; // Will trigger UI update
                }
            }
        }
        return;
    }

    // activespecial>>special:N,MONITOR or activespecial>>,MONITOR (closed)
    if (strncmp(event, "activespecial>>", 15) == 0) {
        // Special workspace state changed - refresh window counts to update dots
        refresh_window_counts(mod);
        return;
    }

    // Window events - need to refresh counts
    if (strncmp(event, "openwindow>>", 12) == 0 ||
        strncmp(event, "closewindow>>", 13) == 0 ||
        strncmp(event, "movewindow>>", 12) == 0) {
        refresh_window_counts(mod);
        return;
    }

    // Workspace created/destroyed - refresh monitor assignments
    if (strncmp(event, "createworkspace>>", 17) == 0 ||
        strncmp(event, "destroyworkspace>>", 18) == 0) {
        refresh_workspace_monitors(mod);
        return;
    }

    // Monitor workspace move - refresh assignments
    if (strncmp(event, "moveworkspace>>", 15) == 0) {
        refresh_workspace_monitors(mod);
        return;
    }
}

// Check if a workspace should be visible based on config
static int should_show_workspace(WorkspaceModule* mod, int ws_index) {
    int ws_num = ws_index + 1;
    int is_this_monitor_ws = (ws_num == mod->this_monitor_workspace);
    int has_windows = (mod->workspace_windows[ws_index] > 0);
    int has_special = (mod->special_windows[ws_index] > 0);
    int on_this_monitor = (mod->all_outputs ||
                           mod->monitor_name[0] == '\0' ||
                           strcmp(mod->workspace_monitor[ws_index], mod->monitor_name) == 0 ||
                           mod->workspace_monitor[ws_index][0] == '\0');

    // Always show this monitor's active workspace
    if (is_this_monitor_ws) return 1;

    // Check monitor filter
    if (!on_this_monitor) return 0;

    // Check empty filter - special windows count as "not empty"
    if (!mod->show_empty && !has_windows && !has_special) return 0;

    return 1;
}

// Update CSS classes and button visibility (must be called from GTK main thread)
static gboolean update_ui_callback(gpointer user_data) {
    WorkspaceModule* mod = (WorkspaceModule*)user_data;
    update_button_states(mod);
    return G_SOURCE_REMOVE;
}

static void update_button_states(WorkspaceModule* mod) {
    for (int i = 0; i < NUM_WORKSPACES; i++) {
        GtkStyleContext* ctx = gtk_widget_get_style_context(GTK_WIDGET(mod->buttons[i]));

        // Check visibility
        int should_show = should_show_workspace(mod, i);
        gtk_widget_set_visible(GTK_WIDGET(mod->buttons[i]), should_show);

        if (!should_show) continue;

        // Remove all our classes first
        gtk_style_context_remove_class(ctx, "active");
        gtk_style_context_remove_class(ctx, "visible");
        gtk_style_context_remove_class(ctx, "empty");
        gtk_style_context_remove_class(ctx, "has-special");

        // Apply active/visible classes based on per-monitor state
        if ((i + 1) == mod->this_monitor_workspace) {
            if (mod->user_focused_here) {
                // User is focused on this monitor - full active styling with underline
                gtk_style_context_add_class(ctx, "active");
            } else {
                // Workspace shown on this monitor but user focused elsewhere - highlight only
                gtk_style_context_add_class(ctx, "visible");
            }
        }

        if (mod->workspace_windows[i] == 0 && mod->special_windows[i] == 0) {
            gtk_style_context_add_class(ctx, "empty");
        }

        if (mod->special_windows[i] > 0) {
            gtk_style_context_add_class(ctx, "has-special");
        }

        // Show/hide dot indicator (separate overlay, doesn't affect centering)
        if (mod->special_windows[i] > 0) {
            gtk_widget_show(GTK_WIDGET(mod->dot_labels[i]));
        } else {
            gtk_widget_hide(GTK_WIDGET(mod->dot_labels[i]));
        }
    }
}

// Button click handler - switch to workspace
static void on_button_clicked(GtkButton* button, gpointer user_data) {
    int workspace = GPOINTER_TO_INT(user_data);
    char cmd[64];
    snprintf(cmd, sizeof(cmd), "hyprctl dispatch workspace %d", workspace);
    system(cmd);
}

// Connect to Hyprland's event socket
static int connect_hyprland_socket(void) {
    const char* xdg_runtime = getenv("XDG_RUNTIME_DIR");
    const char* hypr_sig = getenv("HYPRLAND_INSTANCE_SIGNATURE");

    if (!xdg_runtime || !hypr_sig) {
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

// IPC monitoring thread
static void* ipc_monitor_thread(void* arg) {
    WorkspaceModule* mod = (WorkspaceModule*)arg;
    char buffer[2048];

    mod->socket_fd = connect_hyprland_socket();
    if (mod->socket_fd < 0) {
        fprintf(stderr, "workspace_buttons: Failed to connect to Hyprland socket\n");
        return NULL;
    }

    while (mod->running) {
        ssize_t bytes = read(mod->socket_fd, buffer, sizeof(buffer) - 1);
        if (bytes <= 0) {
            if (mod->running) {
                // Try to reconnect
                close(mod->socket_fd);
                sleep(1);
                mod->socket_fd = connect_hyprland_socket();
            }
            continue;
        }

        buffer[bytes] = '\0';

        // Parse individual events (newline-separated)
        // Fast path: parse events directly without spawning processes
        char* line = buffer;
        char* next;
        int needs_update = 0;

        while ((next = strchr(line, '\n')) != NULL || *line) {
            if (next) *next = '\0';

            // Skip empty lines
            if (*line) {
                // Handle event - updates state directly where possible
                handle_event(mod, line);
                needs_update = 1;
            }

            if (!next) break;
            line = next + 1;
        }

        // Queue single UI update for all events in this batch
        if (needs_update) {
            g_idle_add(update_ui_callback, mod);
        }
    }

    close(mod->socket_fd);
    return NULL;
}

// Parse boolean config value from JSON string
static int parse_bool(const char* value) {
    if (!value) return 0;
    // JSON booleans are "true" or "false"
    if (strcmp(value, "true") == 0) return 1;
    if (strcmp(value, "false") == 0) return 0;
    // Also accept "1" / "0"
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

    // Default config values
    mod->all_outputs = 0;  // Only show workspaces on this monitor
    mod->show_empty = 0;   // Hide empty workspaces

    // Parse config entries
    for (size_t i = 0; i < config_entries_len; i++) {
        if (strcmp(config_entries[i].key, "all-outputs") == 0) {
            mod->all_outputs = parse_bool(config_entries[i].value);
        } else if (strcmp(config_entries[i].key, "show-empty") == 0) {
            mod->show_empty = parse_bool(config_entries[i].value);
        } else if (strcmp(config_entries[i].key, "output") == 0) {
            // Allow manual override of output name
            const char* val = config_entries[i].value;
            if (val[0] == '"') val++;
            strncpy(mod->monitor_name, val, sizeof(mod->monitor_name) - 1);
            size_t len = strlen(mod->monitor_name);
            if (len > 0 && mod->monitor_name[len-1] == '"') {
                mod->monitor_name[len-1] = '\0';
            }
        }
    }

    fprintf(stderr, "workspace_buttons: Config - all-outputs=%d, show-empty=%d\n",
            mod->all_outputs, mod->show_empty);

    // Load theme color
    load_tertiary_color(mod);

    GtkContainer* root = init_info->get_root_widget(init_info->obj);

    // Create horizontal box container
    mod->container = GTK_BOX(gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0));
    gtk_widget_set_name(GTK_WIDGET(mod->container), "workspaces");
    gtk_container_add(root, GTK_WIDGET(mod->container));

    // Connect map signal to detect monitor (fires after widget is positioned)
    g_signal_connect(mod->container, "map", G_CALLBACK(on_widget_map), mod);

    // Create 9 workspace buttons
    for (int i = 0; i < NUM_WORKSPACES; i++) {
        char label[8];
        snprintf(label, sizeof(label), "%d", i + 1);

        // Create button with overlay structure for proper dot positioning
        mod->buttons[i] = GTK_BUTTON(gtk_button_new());
        GtkOverlay* overlay = GTK_OVERLAY(gtk_overlay_new());

        // Main label (centered number)
        mod->labels[i] = GTK_LABEL(gtk_label_new(label));
        gtk_widget_set_halign(GTK_WIDGET(mod->labels[i]), GTK_ALIGN_CENTER);
        gtk_widget_set_valign(GTK_WIDGET(mod->labels[i]), GTK_ALIGN_CENTER);
        gtk_container_add(GTK_CONTAINER(overlay), GTK_WIDGET(mod->labels[i]));

        // Dot indicator (positioned top-right, initially hidden)
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

    // Get initial state (monitor detection happens on realize)
    fetch_initial_state(mod);
    update_button_states(mod);

    // Start IPC monitoring thread
    pthread_create(&mod->ipc_thread, NULL, ipc_monitor_thread, mod);

    fprintf(stderr, "workspace_buttons: Initialized (tertiary=%s)\n", mod->tertiary_color);
    return mod;
}

void wbcffi_deinit(void* instance) {
    WorkspaceModule* mod = (WorkspaceModule*)instance;

    mod->running = 0;
    if (mod->socket_fd > 0) {
        shutdown(mod->socket_fd, SHUT_RDWR);
    }
    pthread_join(mod->ipc_thread, NULL);

    free(mod);
    fprintf(stderr, "workspace_buttons: Deinitialized\n");
}

void wbcffi_update(void* instance) {
    // Called from GTK main loop - we handle updates via IPC thread
}

void wbcffi_refresh(void* instance, int signal) {
    WorkspaceModule* mod = (WorkspaceModule*)instance;
    // Reload color on signal (in case theme changed)
    load_tertiary_color(mod);
    fetch_initial_state(mod);
    update_button_states(mod);
}

void wbcffi_doaction(void* instance, const char* action_name) {
    // Could add custom actions here
}

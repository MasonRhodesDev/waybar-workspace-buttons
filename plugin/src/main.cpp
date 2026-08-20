#include <algorithm>
#include <cctype>
#include <format>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>

#include <hyprland/src/Compositor.hpp>
#include <hyprland/src/SharedDefs.hpp>
#include <hyprland/src/config/shared/actions/ConfigActions.hpp>
#include <hyprland/src/config/values/types/BoolValue.hpp>
#include <hyprland/src/debug/log/Logger.hpp>
#include <hyprland/src/desktop/Workspace.hpp>
#include <hyprland/src/desktop/state/FocusState.hpp>
#include <hyprland/src/event/EventBus.hpp>
#include <hyprland/src/helpers/Color.hpp>
#include <hyprland/src/helpers/MiscFunctions.hpp>
#include <hyprland/src/output/Monitor.hpp>
#include <hyprland/src/state/MonitorState.hpp>
#include <hyprland/src/state/MonitorQuery.hpp>
#include <hyprland/src/state/WorkspaceState.hpp>
#include <hyprland/src/managers/eventLoop/EventLoopManager.hpp>
#include <hyprland/src/plugins/PluginAPI.hpp>

// workspace-zones: every numeric workspace N owns a scratch zone, special:N.
//
// Lua config only (Hyprland 0.56+). Entry points, bound from hyprland.lua:
//   hl.plugin.zones.toggle()      toggle the focused workspace's zone
//   hl.plugin.zones.move()        move the focused window to the zone (and follow)
//   hl.plugin.zones.movesilent()  move the focused window to the zone without following
// The classic zones:* string dispatchers are gone: Hyprland's 0.56 keybinds
// refactor removed the dispatcher registry (and stubbed the plugin API for
// it), and we only support the Lua config anyway.
//
// Auto-dismiss: leaving a workspace closes its zone (any switch path — binds,
// bar clicks, hyprctl), so zones never linger over an unrelated workspace.
// Named specials (special:magic etc.) are never touched.

namespace {
    HANDLE                            PHANDLE = nullptr;
    CHyprSignalListener               g_activeListener;
    SP<Config::Values::CBoolValue>    g_autoDismiss;

    namespace CA = Config::Actions;

    constexpr const char* K_AUTO_DISMISS = "plugin:workspace-zones:auto_dismiss";

    bool autoDismissEnabled() {
        return !g_autoDismiss || g_autoDismiss->value(); // default on if registration failed
    }

    // "special:7" -> 7. nullopt for named specials so they are left alone.
    std::optional<WORKSPACEID> zoneOwner(const std::string& specialName) {
        constexpr std::string_view PREFIX = "special:";
        if (!specialName.starts_with(PREFIX))
            return std::nullopt;
        const auto TAIL = std::string_view{specialName}.substr(PREFIX.size());
        if (TAIL.empty() || !std::ranges::all_of(TAIL, [](unsigned char c) { return std::isdigit(c); }))
            return std::nullopt;
        return std::stoll(std::string{TAIL});
    }

    // String dispatchers died with the 0.56 keybinds refactor (the registry is
    // gone and HyprlandAPI::addDispatcherV2 is a stub); typed Config::Actions
    // functions are the replacement. Bridge their std::expected result back to
    // the SDispatchResult our entry points still report.
    SDispatchResult toDispatchResult(const CA::ActionResult& r) {
        if (r)
            return {};
        return {.success = false, .error = r.error().message};
    }

    // Resolve (creating if needed) workspace special:<owner> — same resolution
    // path the built-in Lua dsp bindings use.
    PHLWORKSPACE getOrCreateZone(WORKSPACEID owner) {
        const auto& [WSID, WSNAME, ISAUTO] = getWorkspaceIDNameFromString(std::format("special:{}", owner));
        if (WSID == WORKSPACE_INVALID || !State::workspaceState()->isSpecial(WSID))
            return nullptr;
        auto ws = State::workspaceState()->query().id(WSID).run();
        if (!ws) {
            const auto MON = Desktop::focusState()->monitor();
            if (MON)
                ws = State::workspaceState()->create(WSID, MON->m_id, WSNAME);
        }
        return ws;
    }

    std::optional<WORKSPACEID> focusedWorkspaceID() {
        const auto MON = Desktop::focusState()->monitor();
        if (!MON)
            return std::nullopt;
        const auto WSID = MON->activeWorkspaceID();
        // Named workspaces (negative IDs) don't own zones.
        return WSID >= 1 ? std::optional{WSID} : std::nullopt;
    }

    SDispatchResult zonesToggle(std::string) {
        const auto WSID = focusedWorkspaceID();
        if (!WSID)
            return {.success = false, .error = "no numeric workspace focused"};
        const auto ZONE = getOrCreateZone(*WSID);
        if (!ZONE)
            return {.success = false, .error = "could not resolve zone workspace"};
        // toggleSpecial already closes-if-open and swaps away any other
        // special, so it carries the full toggle semantics for us.
        return toDispatchResult(CA::toggleSpecial(ZONE));
    }

    SDispatchResult zonesMove(std::string) {
        const auto WSID = focusedWorkspaceID();
        if (!WSID)
            return {.success = false, .error = "no numeric workspace focused"};
        const auto ZONE = getOrCreateZone(*WSID);
        if (!ZONE)
            return {.success = false, .error = "could not resolve zone workspace"};
        return toDispatchResult(CA::moveToWorkspace(ZONE, false));
    }

    SDispatchResult zonesMoveSilent(std::string) {
        const auto WSID = focusedWorkspaceID();
        if (!WSID)
            return {.success = false, .error = "no numeric workspace focused"};
        const auto ZONE = getOrCreateZone(*WSID);
        if (!ZONE)
            return {.success = false, .error = "could not resolve zone workspace"};
        return toDispatchResult(CA::moveToWorkspace(ZONE, true));
    }

    // First-party Lua config functions: hl.plugin.zones.{toggle,move,movesilent}().
    // The Lua config can't reach classic string dispatchers (hl.dispatch only
    // accepts hl.dsp.* objects, and `hyprctl dispatch <arg>` evaluates <arg> as a
    // Lua expression), so expose the same actions as callables a hyprland.lua
    // keybind can invoke directly:
    //   hl.bind("SUPER + ALT + S", function() hl.plugin.zones.toggle() end)
    // No arguments, no return values — the Lua stack is never touched, so the
    // forward-declared lua_State suffices and no Lua headers are needed.
    int luaZonesToggle(lua_State*) {
        if (const auto R = zonesToggle(""); !R.success)
            Log::logger->log(Log::DEBUG, "[workspace-zones] zones.toggle: {}", R.error);
        return 0;
    }

    int luaZonesMove(lua_State*) {
        if (const auto R = zonesMove(""); !R.success)
            Log::logger->log(Log::DEBUG, "[workspace-zones] zones.move: {}", R.error);
        return 0;
    }

    int luaZonesMoveSilent(lua_State*) {
        if (const auto R = zonesMoveSilent(""); !R.success)
            Log::logger->log(Log::DEBUG, "[workspace-zones] zones.movesilent: {}", R.error);
        return 0;
    }

    void onWorkspaceActive(PHLWORKSPACE ws) {
        if (!ws)
            return;
        if (ws->m_id < 1) // activating a special/named workspace never dismisses
            return;
        const auto MON = ws->m_monitor.lock();
        if (!MON || !MON->m_activeSpecialWorkspace)
            return;
        // Zones only. Named specials (special:magic, ...) are never touched —
        // for those, Hyprland's own binds:hide_special_on_workspace_change
        // covers workspace-change dismissal (same-monitor scoped) natively.
        const auto NAME  = MON->m_activeSpecialWorkspace->m_name; // "special:7"
        const auto OWNER = zoneOwner(NAME);
        if (!OWNER || *OWNER == ws->m_id) // named special, or arriving at the zone's owner
            return;
        if (!autoDismissEnabled())
            return;

        // The monitor moved off the zone's owner workspace: dismiss the zone.
        // The event fires for the monitor the new workspace is on, so a zone
        // open on a different monitor is never touched.
        // Deferred to the event loop — we're inside the workspace-change
        // event and must not re-enter workspace machinery from here.
        const auto MONID = MON->m_id;
        g_pEventLoopManager->doLater([NAME, MONID] {
            const auto MON = State::monitorState()->query().id(MONID).run();
            if (!MON || !MON->m_activeSpecialWorkspace)
                return;
            if (MON->m_activeSpecialWorkspace->m_name != NAME)
                return;
            if (const auto R = CA::toggleSpecial(MON->m_activeSpecialWorkspace); !R)
                Log::logger->log(Log::DEBUG, "[workspace-zones] auto-dismiss: {}", R.error().message);
        });
    }
}

APICALL EXPORT std::string PLUGIN_API_VERSION() {
    return HYPRLAND_API_VERSION;
}

APICALL EXPORT PLUGIN_DESCRIPTION_INFO PLUGIN_INIT(HANDLE handle) {
    PHANDLE = handle;

    const std::string HASH        = __hyprland_api_get_hash();
    const std::string CLIENT_HASH = __hyprland_api_get_client_hash();
    if (HASH != CLIENT_HASH) {
        HyprlandAPI::addNotification(PHANDLE, "[workspace-zones] Built against a different Hyprland version — rebuild (hyprpm update)", CHyprColor{1.F, 0.2F, 0.2F, 1.F}, 5000);
        throw std::runtime_error("[workspace-zones] version mismatch: running " + HASH + ", built against " + CLIENT_HASH);
    }

    g_autoDismiss = makeShared<Config::Values::CBoolValue>(K_AUTO_DISMISS, "Auto-dismiss a workspace's zone when leaving its owner workspace", true);
    if (!HyprlandAPI::addConfigValueV2(PHANDLE, g_autoDismiss))
        g_autoDismiss.reset(); // autoDismissEnabled() falls back to default-on

    // Reload-safe: the config manager re-registers these into every rebuilt
    // Lua state, and unregisters them automatically on plugin unload.
    HyprlandAPI::addLuaFunction(PHANDLE, "zones", "toggle", luaZonesToggle);
    HyprlandAPI::addLuaFunction(PHANDLE, "zones", "move", luaZonesMove);
    HyprlandAPI::addLuaFunction(PHANDLE, "zones", "movesilent", luaZonesMoveSilent);

    g_activeListener = Event::bus()->m_events.workspace.active.listen(onWorkspaceActive);

    HyprlandAPI::reloadConfig();

    return {"workspace-zones", "Per-workspace special zones: workspace N owns special:N, auto-dismissed on leave", "Mason Rhodes", "0.3.0"};
}

APICALL EXPORT void PLUGIN_EXIT() {
    g_activeListener.reset();
    g_autoDismiss.reset();
}

#include <algorithm>
#include <cctype>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>

#include <hyprland/src/Compositor.hpp>
#include <hyprland/src/SharedDefs.hpp>
#include <hyprland/src/desktop/Workspace.hpp>
#include <hyprland/src/desktop/state/FocusState.hpp>
#include <hyprland/src/event/EventBus.hpp>
#include <hyprland/src/helpers/Color.hpp>
#include <hyprland/src/helpers/Monitor.hpp>
#include <hyprland/src/managers/KeybindManager.hpp>
#include <hyprland/src/managers/eventLoop/EventLoopManager.hpp>
#include <hyprland/src/plugins/PluginAPI.hpp>

// workspace-zones: every numeric workspace N owns a scratch zone, special:N.
//
// Dispatchers:
//   zones:toggle      toggle the focused workspace's zone
//   zones:move        move the focused window to the zone (and follow)
//   zones:movesilent  move the focused window to the zone without following
//
// Auto-dismiss: leaving a workspace closes its zone (any switch path — binds,
// bar clicks, hyprctl), so zones never linger over an unrelated workspace.
// Named specials (special:magic etc.) are never touched.

namespace {
    HANDLE                PHANDLE = nullptr;
    CHyprSignalListener   g_activeListener;

    constexpr const char* K_AUTO_DISMISS = "plugin:workspace-zones:auto_dismiss";

    // The V2 config API (addConfigValueV2) RASSERTs inside commence() on
    // Hyprland <= 0.54: getConfigValue() doesn't resolve the "plugin" special
    // category. The deprecated V1 API routes correctly, so use it until the
    // minimum supported Hyprland moves past that.
    bool autoDismissEnabled() {
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wdeprecated-declarations"
        static Hyprlang::INT* const* P = [] -> Hyprlang::INT* const* {
            const auto* V = HyprlandAPI::getConfigValue(PHANDLE, K_AUTO_DISMISS);
            return V ? (Hyprlang::INT* const*)V->getDataStaticPtr() : nullptr;
        }();
#pragma GCC diagnostic pop
        return !P || **P; // default on if the value can't be resolved (e.g. lua config)
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

    SDispatchResult runDispatcher(const std::string& name, const std::string& arg) {
        if (!g_pKeybindManager)
            return {.success = false, .error = "keybind manager unavailable"};
        const auto IT = g_pKeybindManager->m_dispatchers.find(name);
        if (IT == g_pKeybindManager->m_dispatchers.end())
            return {.success = false, .error = "dispatcher not found: " + name};
        return IT->second(arg);
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
        // togglespecialworkspace already closes-if-open and swaps away any
        // other special, so it carries the full toggle semantics for us.
        return runDispatcher("togglespecialworkspace", std::to_string(*WSID));
    }

    SDispatchResult zonesMove(std::string) {
        const auto WSID = focusedWorkspaceID();
        if (!WSID)
            return {.success = false, .error = "no numeric workspace focused"};
        return runDispatcher("movetoworkspace", "special:" + std::to_string(*WSID));
    }

    SDispatchResult zonesMoveSilent(std::string) {
        const auto WSID = focusedWorkspaceID();
        if (!WSID)
            return {.success = false, .error = "no numeric workspace focused"};
        return runDispatcher("movetoworkspacesilent", "special:" + std::to_string(*WSID));
    }

    void onWorkspaceActive(PHLWORKSPACE ws) {
        if (!ws || !autoDismissEnabled())
            return;
        if (ws->m_id < 1)
            return;
        const auto MON = ws->m_monitor.lock();
        if (!MON || !MON->m_activeSpecialWorkspace)
            return;
        const auto OWNER = zoneOwner(MON->m_activeSpecialWorkspace->m_name);
        if (!OWNER || *OWNER == ws->m_id)
            return;

        // The monitor moved off the zone's owner workspace: dismiss the zone.
        // Deferred to the event loop — we're inside the workspace-change
        // event and must not re-enter workspace machinery from here.
        const auto ZONE  = *OWNER;
        const auto MONID = MON->m_id;
        g_pEventLoopManager->doLater([ZONE, MONID] {
            const auto MON = g_pCompositor->getMonitorFromID(MONID);
            if (!MON || !MON->m_activeSpecialWorkspace)
                return;
            if (zoneOwner(MON->m_activeSpecialWorkspace->m_name) != ZONE)
                return;
            runDispatcher("togglespecialworkspace", std::to_string(ZONE));
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

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wdeprecated-declarations"
    HyprlandAPI::addConfigValue(PHANDLE, K_AUTO_DISMISS, Hyprlang::INT{1});
#pragma GCC diagnostic pop

    HyprlandAPI::addDispatcherV2(PHANDLE, "zones:toggle", zonesToggle);
    HyprlandAPI::addDispatcherV2(PHANDLE, "zones:move", zonesMove);
    HyprlandAPI::addDispatcherV2(PHANDLE, "zones:movesilent", zonesMoveSilent);

    g_activeListener = Event::bus()->m_events.workspace.active.listen(onWorkspaceActive);

    HyprlandAPI::reloadConfig();

    return {"workspace-zones", "Per-workspace special zones: workspace N owns special:N, auto-dismissed on leave", "Mason Rhodes", "0.1.0"};
}

APICALL EXPORT void PLUGIN_EXIT() {
    g_activeListener.reset();
}

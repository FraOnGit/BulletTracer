#include "BulletTracer.h"

#include <Globals.h>
#include <Hooks.h>
#include <IRenderer.h>
#include <Logging.h>

#include <Windows.h>

#include <Glacier/SExternalReferences.h>
#include <Glacier/SGameUpdateEvent.h>
#include <Glacier/ZEntityManager.h>
#include <Glacier/ZGameLoopManager.h>
#include <Glacier/ZHM5CrippleBox.h>
#include <Glacier/ZHitman5.h>
#include <Glacier/ZModule.h>
#include <Glacier/ZResource.h>
#include <Glacier/ZScene.h>

#include <algorithm>
#include <cmath>

namespace {
constexpr const char* kVersion = "1.0.1";

// Addresses as Ghidra saw them; Windows loads the game elsewhere, so they are shifted at runtime.
constexpr uintptr_t kGhidraImageBase = 0x140000000;

// ZHM5Projectile: the interface whose slot 25 sets a new shot up, and the method that is there.
constexpr uintptr_t kProjectileVTable = 0x141e1f6a0;
constexpr int kSetUpSlot = 25;
constexpr uintptr_t kSetUpFunction = 0x1406ebca0;

// Where the projectile is now, measured in flight: +0xf8 of that interface (+0xe4, the translation of
// its starting matrix at +0xc0, stays where the shot left the gun).
constexpr size_t kPositionOffset = 0xf8;

constexpr int kUpdatePriority = 1;

// A point every 2 cm at most, and a shot that has not moved for half a second of game time has landed.
// Game time, not frames: with the game paused (freecam) a dart in the air stands still but has not landed.
constexpr float kMinStep = 0.02f;
constexpr float kStillSecondsToLand = 0.5f;
constexpr float kMaxFlightSeconds = 30.f;

// Darts or bullets: the speed over the first tenth of a second. A tranquilizer dart was measured at about
// 48 m/s; anything that does not travel in that range is not a dart in flight.
constexpr float kClassifySeconds = 0.1f;
constexpr float kMinDartSpeed = 10.f;
constexpr float kMaxDartSpeed = 150.f;

using SetUpFn = void (*)(void*, void*);

BulletTracer* g_Instance = nullptr;
SetUpFn g_Original = nullptr;

// No C++ objects in here: __try does not mix with destructors.
bool ReadPosition(const void* p_Projectile, uintptr_t p_VTable, float p_Out[3]) {
    __try {
        if (*static_cast<const uintptr_t*>(p_Projectile) != p_VTable) {
            return false;
        }

        const auto* s_Pos = reinterpret_cast<const float*>(static_cast<const char*>(p_Projectile) + kPositionOffset);
        p_Out[0] = s_Pos[0];
        p_Out[1] = s_Pos[1];
        p_Out[2] = s_Pos[2];
        return std::isfinite(p_Out[0]) && std::isfinite(p_Out[1]) && std::isfinite(p_Out[2]);
    }
    __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

float Distance(const SVector3& a, const SVector3& b) {
    const float dx = a.x - b.x, dy = a.y - b.y, dz = a.z - b.z;
    return std::sqrt(dx * dx + dy * dy + dz * dz);
}
} // namespace

BulletTracer::~BulletTracer() {
    RemoveHook();

    const ZMemberDelegate<BulletTracer, void(const SGameUpdateEvent&)> s_Delegate(this, &BulletTracer::OnFrameUpdate);
    Globals::GameLoopManager->UnregisterFrameUpdate(s_Delegate, kUpdatePriority, EUpdateMode::eUpdatePlayMode);

    g_Instance = nullptr;
}

void BulletTracer::Init() {
    g_Instance = this;
    Hooks::ZEntitySceneContext_ClearScene->AddDetour(this, &BulletTracer::OnClearScene);
}

void BulletTracer::OnEngineInitialized() {
    m_ModuleDelta = reinterpret_cast<uintptr_t>(GetModuleHandleA(nullptr)) - kGhidraImageBase;

    m_Enabled = GetSettingBool("general", "enabled", true);
    m_AlwaysVisible = GetSettingBool("general", "window_always_visible", false);
    m_UseDepth = GetSettingBool("general", "use_depth", false);
    m_AllShots = GetSettingBool("general", "all_shots", false);

    const ZMemberDelegate<BulletTracer, void(const SGameUpdateEvent&)> s_Delegate(this, &BulletTracer::OnFrameUpdate);
    Globals::GameLoopManager->RegisterFrameUpdate(s_Delegate, kUpdatePriority, EUpdateMode::eUpdatePlayMode);

    InstallHook();
}

// Swaps slot 25 of the projectile's vtable for ours, only if it still holds the method Ghidra saw: after a
// game update the addresses move, and then the mod stays off rather than patch the wrong thing.
void BulletTracer::InstallHook() {
    auto** s_Slot = reinterpret_cast<void**>(kProjectileVTable + m_ModuleDelta) + kSetUpSlot;

    if (reinterpret_cast<uintptr_t>(*s_Slot) != kSetUpFunction + m_ModuleDelta) {
        Logger::Error("[BulletTracer] The projectile's vtable is not what was expected (game updated?). Not hooking.");
        return;
    }

    DWORD s_Old = 0;

    if (!VirtualProtect(s_Slot, sizeof(void*), PAGE_READWRITE, &s_Old)) {
        Logger::Error("[BulletTracer] Could not unprotect the projectile's vtable.");
        return;
    }

    g_Original = reinterpret_cast<SetUpFn>(*s_Slot);
    *s_Slot = reinterpret_cast<void*>(&BulletTracer::ProjectileSetUp);
    VirtualProtect(s_Slot, sizeof(void*), s_Old, &s_Old);

    m_Slot = s_Slot;
    m_HookOk = true;
}

void BulletTracer::RemoveHook() {
    if (!m_Slot || !g_Original) {
        return;
    }

    DWORD s_Old = 0;

    if (VirtualProtect(m_Slot, sizeof(void*), PAGE_READWRITE, &s_Old)) {
        *m_Slot = reinterpret_cast<void*>(g_Original);
        VirtualProtect(m_Slot, sizeof(void*), s_Old, &s_Old);
    }

    m_Slot = nullptr;
    m_HookOk = false;
}

// The game sets the shot up as usual; afterwards its address goes on the list for the frame update.
void BulletTracer::ProjectileSetUp(void* p_This, void* p_Params) {
    g_Original(p_This, p_Params);

    if (g_Instance && g_Instance->m_Enabled) {
        std::scoped_lock s_Lock(g_Instance->m_Mutex);
        g_Instance->m_NewProjectiles.push_back(p_This);
    }
}

void BulletTracer::OnFrameUpdate(const SGameUpdateEvent& p_UpdateEvent) {
    const float s_Delta = static_cast<float>(p_UpdateEvent.m_GameTimeDelta.ToSeconds());

    // Read here, on the game's own update, for the window to show.
    const auto s_Hitman = SDK()->GetLocalPlayer();
    m_HitmanInfiniteAmmo = s_Hitman && s_Hitman.m_pInterfaceRef->IsInfiniteAmmoEnabled();

    if (!m_Enabled) {
        std::scoped_lock s_Lock(m_Mutex);
        m_NewProjectiles.clear();
        return;
    }

    UpdateTraces(s_Delta);
}

// Dart or bullet, from the average speed since the shot. Before kClassifySeconds the shot stays unknown,
// unless it has already ended (p_Final): then what there is decides.
void BulletTracer::Classify(Trace& p_Trace, bool p_Final) {
    if (p_Trace.Kind != EKind::Unknown || (!p_Final && p_Trace.Age < kClassifySeconds)) {
        return;
    }

    bool s_Dart = false;

    if (p_Trace.Age > 0.f && !p_Trace.Points.empty()) {
        const float s_Speed = Distance(p_Trace.Points.front(), p_Trace.Points.back()) / p_Trace.Age;
        s_Dart = s_Speed >= kMinDartSpeed && s_Speed <= kMaxDartSpeed;
    }

    if (s_Dart) {
        p_Trace.Kind = EKind::Dart;
        ++m_DartsSeen;
    }
    else {
        p_Trace.Kind = EKind::Bullet;
        ++m_BulletsSeen;
    }
}

void BulletTracer::UpdateTraces(float p_Delta) {
    std::scoped_lock s_Lock(m_Mutex);
    const uintptr_t s_VTable = kProjectileVTable + m_ModuleDelta;

    // The shots already in the air. With the game paused no game time passes: nothing moves, and nothing
    // may count as landed.
    if (p_Delta > 0.f) {
        for (auto& s_Trace : m_Traces) {
            if (!s_Trace.Flying) {
                continue;
            }

            s_Trace.Age += p_Delta;
            float s_Pos[3];
            bool s_Ended = s_Trace.Age > kMaxFlightSeconds || !ReadPosition(s_Trace.Projectile, s_Trace.VTable, s_Pos);

            if (!s_Ended) {
                const SVector3 s_Point(s_Pos[0], s_Pos[1], s_Pos[2]);

                if (s_Trace.Points.empty() || Distance(s_Trace.Points.back(), s_Point) >= kMinStep) {
                    s_Trace.Points.push_back(s_Point);
                    s_Trace.StillTime = 0.f;
                }
                else {
                    s_Trace.StillTime += p_Delta;
                    s_Ended = s_Trace.StillTime >= kStillSecondsToLand;
                }
            }

            Classify(s_Trace, s_Ended);

            if (s_Ended) {
                s_Trace.Flying = false;
                s_Trace.Projectile = nullptr;
            }
        }
    }

    // The new shots, after the others, so they start with no time of their own on the clock.
    for (void* s_Projectile : m_NewProjectiles) {
        // The game reuses projectiles: a new shot from the same one ends the old line.
        for (auto& s_Trace : m_Traces) {
            if (s_Trace.Flying && s_Trace.Projectile == s_Projectile) {
                Classify(s_Trace, true);
                s_Trace.Flying = false;
                s_Trace.Projectile = nullptr;
            }
        }

        Trace s_Trace;
        s_Trace.Projectile = s_Projectile;
        s_Trace.VTable = s_VTable;

        float s_Pos[3];

        if (ReadPosition(s_Projectile, s_VTable, s_Pos)) {
            s_Trace.Points.emplace_back(s_Pos[0], s_Pos[1], s_Pos[2]);
        }

        m_Traces.push_back(std::move(s_Trace));
    }

    m_NewProjectiles.clear();
}

void BulletTracer::OnDrawMenu() {
    if (ImGui::Button("BULLET TRACER")) {
        m_ShowWindow = !m_ShowWindow;
    }
}

void BulletTracer::OnDrawUI(bool p_HasFocus) {
    if (!m_ShowWindow || (!p_HasFocus && !m_AlwaysVisible)) {
        return;
    }

    ImGui::PushFont(SDK()->GetImGuiBlackFont());
    const auto s_Expanded = ImGui::Begin("Bullet Tracer", &m_ShowWindow);
    ImGui::PushFont(SDK()->GetImGuiRegularFont());

    if (s_Expanded) {
        if (ImGui::Checkbox("Enabled", &m_Enabled)) {
            SetSettingBool("general", "enabled", m_Enabled);
        }

        if (ImGui::Checkbox("Window always visible (also with the mod menu closed)", &m_AlwaysVisible)) {
            SetSettingBool("general", "window_always_visible", m_AlwaysVisible);
        }

        if (ImGui::Checkbox("Use depth rendering", &m_UseDepth)) {
            SetSettingBool("general", "use_depth", m_UseDepth);
        }

        if (ImGui::Checkbox("Trace all shots (off: only darts, as Sieker and tranquilizer)", &m_AllShots)) {
            SetSettingBool("general", "all_shots", m_AllShots);
        }

        // Only on a click, with the level loaded, as the Player mod does it: never saved, and off again on
        // the next level. Making it at load time, as soon as 47 exists, took the game down.
        // The tick shows what the game says too: ammo left infinite by something else can be turned off here.
        bool s_InfiniteAmmo = m_InfiniteAmmo || m_HitmanInfiniteAmmo;

        if (ImGui::Checkbox("Infinite ammo", &s_InfiniteAmmo)) {
            m_InfiniteAmmo = s_InfiniteAmmo;
            SetInfiniteAmmo();

            if (m_InfiniteAmmo && !m_CrippleBoxEntity) {
                m_InfiniteAmmo = false;
            }
        }

        ImGui::ColorEdit4("Line color", m_Color, ImGuiColorEditFlags_NoInputs);

        if (ImGui::Button("Clear the lines")) {
            std::scoped_lock s_Lock(m_Mutex);
            m_Traces.clear();
        }

        ImGui::Separator();

        if (!m_HookOk) {
            ImGui::TextWrapped("Not active: the game's projectile code is not where it was expected (game updated?).");
        }
        else {
            std::scoped_lock s_Lock(m_Mutex);
            ImGui::Text(
                "Darts seen: %d   Other shots: %d   Lines: %d", m_DartsSeen, m_BulletsSeen,
                static_cast<int>(m_Traces.size())
            );
        }

        ImGui::TextDisabled("Bullet Tracer v%s", kVersion);
    }

    ImGui::PopFont();
    ImGui::End();
    ImGui::PopFont();
}

void BulletTracer::OnDraw3D(IRenderer* p_Renderer) {
    if (!m_UseDepth) {
        DrawTraces(p_Renderer);
    }
}

void BulletTracer::OnDepthDraw3D(IRenderer* p_Renderer) {
    if (m_UseDepth) {
        DrawTraces(p_Renderer);
    }
}

// A shot still unknown (its first tenth of a second) is drawn only with "Trace all shots": with darts only,
// its line appears whole as soon as it turns out to be a dart.
void BulletTracer::DrawTraces(IRenderer* p_Renderer) {
    if (!m_Enabled) {
        return;
    }

    const SVector4 s_Color(m_Color[0], m_Color[1], m_Color[2], m_Color[3]);
    std::scoped_lock s_Lock(m_Mutex);

    for (const auto& s_Trace : m_Traces) {
        if (!m_AllShots && s_Trace.Kind != EKind::Dart) {
            continue;
        }

        for (size_t i = 1; i < s_Trace.Points.size(); ++i) {
            p_Renderer->DrawLine3D(s_Trace.Points[i - 1], s_Trace.Points[i], s_Color, s_Color);
        }
    }
}

// Infinite ammo is a cripple box with limited ammo turned off, as the Player mod makes it. The game keeps
// its cripple boxes on a list and recomputes 47's abilities from that list only when a box is activated or
// deactivated (Ghidra: ZHM5CrippleBox slots 6 and 7, recompute at 0x1403e39b0). A box's destructor just
// takes it off the list, so a box deleted without Deactivate first, by hand or by the level ending, leaves
// the ammo infinite until the game is closed: that is why the box is always deactivated here before it goes.
void BulletTracer::SetInfiniteAmmo() {
    auto s_Hitman = SDK()->GetLocalPlayer();

    if (!m_InfiniteAmmo) {
        RemoveCrippleBox();

        // Still infinite, with no box of ours: left so by a box deleted without Deactivate (version 1.0.0
        // on a level restart, or another mod). A box switched on and off makes the game recompute the
        // ammo from the boxes that are left, which brings it back to normal.
        if (s_Hitman && s_Hitman.m_pInterfaceRef->IsInfiniteAmmoEnabled() && CreateCrippleBox()) {
            if (auto* s_CrippleBox = m_CrippleBoxEntity.QueryInterface<ZHM5CrippleBox>()) {
                s_CrippleBox->m_bActivateOnStart = true;
                s_CrippleBox->m_rHitmanCharacter = s_Hitman;
                s_CrippleBox->m_bLimitedAmmo = true;
                s_CrippleBox->Activate(0);
            }

            RemoveCrippleBox();
        }

        return;
    }

    if (!s_Hitman || (!m_CrippleBoxEntity && !CreateCrippleBox())) {
        return;
    }

    auto* s_CrippleBox = m_CrippleBoxEntity.QueryInterface<ZHM5CrippleBox>();

    if (!s_CrippleBox) {
        return;
    }

    s_CrippleBox->m_bActivateOnStart = true;
    s_CrippleBox->m_rHitmanCharacter = s_Hitman;
    s_CrippleBox->m_bLimitedAmmo = false;
    s_CrippleBox->Activate(0);
}

bool BulletTracer::CreateCrippleBox() {
    const auto s_Scene = Globals::Hitman5Module->m_pEntitySceneContext->m_pScene;

    if (!s_Scene) {
        return false;
    }

    constexpr auto s_CrippleBoxId = ResId<"[modules:/zhm5cripplebox.class].pc_entitytype">;

    TResourcePtr<ZTemplateEntityFactory> s_Factory;
    Globals::ResourceManager->GetResourcePtr(s_Factory, s_CrippleBoxId, 0);

    if (!s_Factory) {
        return false;
    }

    SExternalReferences s_ExternalRefs;

    Functions::ZEntityManager_NewEntity->Call(
        Globals::EntityManager, m_CrippleBoxEntity, "", s_Factory, s_Scene.m_entityRef, s_ExternalRefs, -1
    );

    return static_cast<bool>(m_CrippleBoxEntity);
}

// Deactivate first, so that the game recomputes the ammo without this box; then delete it.
void BulletTracer::RemoveCrippleBox() {
    if (!m_CrippleBoxEntity) {
        return;
    }

    if (auto* s_CrippleBox = m_CrippleBoxEntity.QueryInterface<ZHM5CrippleBox>()) {
        s_CrippleBox->Deactivate(0);
    }

    Functions::ZEntityManager_DeleteEntity->Call(Globals::EntityManager, m_CrippleBoxEntity, {});
    m_CrippleBoxEntity = {};
}

// A new scene (another level, or the same one restarted): the old lines belong to a place that is gone.
// The box is switched off here, while 47 and the scene are still there: left to the scene, it would be
// deleted without Deactivate and the ammo would stay infinite.
DEFINE_PLUGIN_DETOUR(BulletTracer, void, OnClearScene, ZEntitySceneContext* th, bool p_FullyUnloadScene) {
    RemoveCrippleBox();
    m_InfiniteAmmo = false;
    m_HitmanInfiniteAmmo = false;

    std::scoped_lock s_Lock(m_Mutex);
    m_Traces.clear();
    m_NewProjectiles.clear();

    return HookResult<void>(HookAction::Continue());
}

DEFINE_ZHM_PLUGIN(BulletTracer);

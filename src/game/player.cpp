// RBS Engine - player movement (Quake style: strafe jumping, bunny hopping), weapons, camera.
#include "core/cvar.h"
#include "core/input.h"
#include "core/log.h"
#include "game/game.h"
#include "render/assets.h"

namespace rbs {

static Cvar* sv_maxspeed = nullptr;
static Cvar* sv_accelerate = nullptr;
static Cvar* sv_airaccelerate = nullptr;
static Cvar* sv_friction = nullptr;
static Cvar* sv_stopspeed = nullptr;
static Cvar* sv_jumpspeed = nullptr;
static Cvar* sv_stepheight = nullptr;
static Cvar* sv_autojump = nullptr;
static Cvar* m_sensitivity = nullptr;
static Cvar* m_invert = nullptr;
static Cvar* cl_fov = nullptr;
static Cvar* cl_bob = nullptr;
static Cvar* cl_noclipspeed = nullptr;

static void RegisterPlayerCvars() {
    if (sv_maxspeed) return;
    sv_maxspeed = cvars::Register("sv_maxspeed", "320", false, "ground speed");
    sv_accelerate = cvars::Register("sv_accelerate", "10", false, "ground acceleration");
    sv_airaccelerate = cvars::Register("sv_airaccelerate", "10", false, "air acceleration (wish speed capped at 30, Quake style)");
    sv_friction = cvars::Register("sv_friction", "6", false, "ground friction");
    sv_stopspeed = cvars::Register("sv_stopspeed", "100", false, "friction minimum speed");
    sv_jumpspeed = cvars::Register("sv_jumpspeed", "270", false, "jump velocity");
    sv_stepheight = cvars::Register("sv_stepheight", "18", false, "max stair step height");
    sv_autojump = cvars::Register("sv_autojump", "1", true, "hold jump to bunny hop");
    m_sensitivity = cvars::Register("sensitivity", "3", true, "mouse sensitivity");
    m_invert = cvars::Register("m_invert", "0", true, "invert mouse Y");
    cl_fov = cvars::Register("fov", "90", true, "field of view (horizontal at 4:3, like Quake)");
    cl_bob = cvars::Register("cl_bob", "1", true, "view and weapon bobbing amount");
    cl_noclipspeed = cvars::Register("cl_noclipspeed", "600", false, "noclip fly speed");
    cvars::Register("hud_objectives", "1", true, "show the mission goals panel (TAB toggles)");
}

Vector3 Game::EyePos() const {
    if (!m_player) return {0, 0, 0};
    return m_player->pos + Vector3{0, m_ps.viewHeight, 0};
}

Vector3 Game::AimDir() const { return AnglesForward(m_ps.pitch, m_ps.yaw); }

static Vector3 ClipVel(Vector3 in, Vector3 n, float overbounce) {
    float backoff = Dot(in, n) * overbounce;
    return in - n * backoff;
}

void Game::PlayerMove(float dt, Vector3 wishdir, float wishspeed, bool jump) {
    Entity* p = m_player;
    Vector3 groundN{0, 1, 0};
    int groundEnt = -1;
    bool onGround = GroundTrace(p, &groundN, &groundEnt) && p->vel.y <= 1.0f;
    float gravity = m_map.settings.gravity;

    if (onGround && jump) {
        p->vel.y = sv_jumpspeed->f;
        onGround = false;
        PlaySound("sounds/jump.wav", p->pos, 0.5f, RandRange(0.95f, 1.05f), false);
    }
    if (onGround) {
        // friction
        float speed = sqrtf(p->vel.x * p->vel.x + p->vel.z * p->vel.z);
        if (speed < 1.0f) {
            p->vel.x = p->vel.z = 0;
        } else {
            float control = speed < sv_stopspeed->f ? sv_stopspeed->f : speed;
            float newspeed = fmaxf(0.0f, speed - dt * control * sv_friction->f);
            p->vel.x *= newspeed / speed;
            p->vel.z *= newspeed / speed;
        }
        // accelerate
        float cur = Dot(p->vel, wishdir);
        float add = wishspeed - cur;
        if (add > 0) {
            float acc = fminf(sv_accelerate->f * dt * wishspeed, add);
            p->vel += wishdir * acc;
        }
        // follow slopes without losing speed
        float spd = Len(p->vel);
        p->vel = ClipVel(p->vel, groundN, 1.001f);
        if (Len(p->vel) > 0.001f) p->vel = Norm(p->vel) * spd;
        if (p->vel.y < 0 && groundN.y > 0.99f) p->vel.y = 0;
    } else {
        // Quake air control: wish speed capped at 30 -> strafe jumping works
        float wishspd = fminf(wishspeed, 30.0f);
        float cur = Dot(p->vel, wishdir);
        float add = wishspd - cur;
        if (add > 0) {
            float acc = fminf(sv_airaccelerate->f * wishspeed * dt, add);
            p->vel += wishdir * acc;
        }
        p->vel.y -= gravity * dt;
    }
    float startY = p->vel.y;
    StepSlideMove(p, dt, sv_stepheight->f);

    bool nowGround = GroundTrace(p, &groundN, &groundEnt) && p->vel.y <= 1.0f;
    if (nowGround && !m_ps.wasOnGround) {
        float fall = -fminf(startY, m_ps.fallSpeed);
        if (fall > 300) {
            m_ps.landDip = -fminf(12.0f, fall / 60.0f);
            PlaySound("sounds/land.wav", p->pos, Clampf(fall / 700.0f, 0.3f, 1.0f), 1.0f, false);
            if (fall > 750 && !m_ps.god) Damage(p, (fall - 700) / 12.0f, nullptr, {0, 0, 0}, p->pos);
        }
    }
    m_ps.fallSpeed = nowGround ? 0 : fminf(m_ps.fallSpeed, p->vel.y);
    m_ps.wasOnGround = nowGround;
    p->onGround = nowGround;
    p->groundEntity = nowGround ? groundEnt : -1;
}

void Game::UpdatePlayer(float dt, bool input) {
    RegisterPlayerCvars();
    Entity* p = m_player;
    if (!p) return;

    if (p->dead) {
        m_ps.deadTime += dt;
        m_ps.viewHeight = fmaxf(-14.0f, m_ps.viewHeight - dt * 80);
        m_ps.punch.z = fminf(25.0f, m_ps.punch.z + dt * 60);
        p->vel.x *= 0.9f;
        p->vel.z *= 0.9f;
        p->vel.y -= m_map.settings.gravity * dt;
        StepSlideMove(p, dt, 0);
        if (input && m_ps.deadTime > 1.0f && (input::Pressed(Action::Fire) || input::Pressed(Action::Jump))) m_restart = true;
        m_ps.damageFlash = fmaxf(0, m_ps.damageFlash - dt);
        return;
    }

    if (input) {
        Vector2 md = GetMouseDelta();
        float sens = m_sensitivity->f * 0.022f;
        m_ps.yaw -= md.x * sens;
        m_ps.pitch -= md.y * sens * (m_invert->i ? -1.0f : 1.0f);
        m_ps.pitch = Clampf(m_ps.pitch, -89.0f, 89.0f);
        m_ps.yaw = fmodf(m_ps.yaw, 360.0f);
        m_ps.swayX = Lerpf(m_ps.swayX, Clampf(md.x * 0.04f, -1.5f, 1.5f), fminf(1.0f, dt * 8));
        m_ps.swayY = Lerpf(m_ps.swayY, Clampf(md.y * 0.04f, -1.5f, 1.5f), fminf(1.0f, dt * 8));
    }
    p->angles = {m_ps.pitch, m_ps.yaw, 0};

    Vector3 fwd = AnglesForward(0, m_ps.yaw);
    Vector3 right = YawRight(m_ps.yaw);
    float fm = 0, sm = 0;
    if (input) {
        if (input::Down(Action::Forward)) fm += 1;
        if (input::Down(Action::Back)) fm -= 1;
        if (input::Down(Action::Right)) sm += 1;
        if (input::Down(Action::Left)) sm -= 1;
    }
    float maxspeed = sv_maxspeed->f * (input && input::Down(Action::Walk) ? 0.5f : 1.0f);

    if (m_ps.noclip) {
        Vector3 look = AimDir();
        Vector3 wish = look * fm + right * sm;
        if (input && input::Down(Action::Jump)) wish.y += 1;
        p->vel = Norm(wish) * cl_noclipspeed->f;
        p->pos += p->vel * dt;
    } else {
        Vector3 wish = fwd * fm + right * sm;
        float wishspeed = Len(wish) > 0 ? maxspeed : 0;
        Vector3 wishdir = Norm(wish);
        bool jumpDown = input && input::Down(Action::Jump);
        bool jump = jumpDown && (sv_autojump->i || !m_ps.wasOnGround || input::Pressed(Action::Jump));
        PlayerMove(dt, wishdir, wishspeed, jump);
    }

    // footsteps
    float hspeed = sqrtf(p->vel.x * p->vel.x + p->vel.z * p->vel.z);
    if (p->onGround && hspeed > 90 && !m_ps.noclip) {
        m_ps.footstep += hspeed * dt;
        if (m_ps.footstep > 110) {
            m_ps.footstep = 0;
            PlaySound(StrFormat("sounds/step%d.wav", GetRandomValue(1, 4)), p->pos, 0.35f, RandRange(0.9f, 1.1f), false);
        }
    }

    // use
    if (input && input::Pressed(Action::Use)) {
        Vector3 eye = EyePos();
        TraceResult tr = Trace(eye, eye + AimDir() * 80.0f, {0, 0, 0}, {0, 0, 0}, p, true);
        if (Entity* hit = Find(tr.entity)) EvUse(hit, p);
        else PlaySound("sounds/use_fail.wav", p->pos, 0.4f, 1.0f, false);
    }
    if (input && input::Pressed(Action::Objectives)) {
        Cvar* cv = cvars::Find("hud_objectives");
        if (cv) cv->Set(cv->i ? "0" : "1");
    }

    // view effects
    m_ps.bobTime += dt * (p->onGround ? hspeed / 320.0f : 0.0f) * 11.0f;
    m_ps.bobAmount = Lerpf(m_ps.bobAmount, p->onGround ? fminf(1.0f, hspeed / 320.0f) : 0.0f, fminf(1.0f, dt * 10));
    m_ps.stepSmooth = Lerpf(m_ps.stepSmooth, 0.0f, fminf(1.0f, dt * 12));
    m_ps.landDip = Lerpf(m_ps.landDip, 0.0f, fminf(1.0f, dt * 8));
    m_ps.punch = m_ps.punch * fmaxf(0.0f, 1.0f - dt * 8);
    m_ps.damageFlash = fmaxf(0, m_ps.damageFlash - dt * 1.5f);
    m_ps.pickupFlash = fmaxf(0, m_ps.pickupFlash - dt * 2.0f);

    UpdateWeapon(dt, input);
}

// ---------------------------------------------------------------------------------
// weapons
// ---------------------------------------------------------------------------------
int Game::Ammo(const std::string& type) const {
    auto it = m_ps.ammo.find(type);
    return it == m_ps.ammo.end() ? 0 : it->second;
}

void Game::GiveAmmo(const std::string& type, int amount) {
    if (type.empty()) return;
    int mx = 999;
    auto it = m_ps.maxAmmo.find(type);
    if (it != m_ps.maxAmmo.end()) mx = it->second;
    m_ps.ammo[type] = std::min(mx, Ammo(type) + amount);
}

void Game::GiveWeapon(const std::string& id, bool select) {
    int idx = m_script.WeaponIndex(id);
    if (idx < 0) {
        LOG_WARN("give_weapon: unknown weapon '%s'", id.c_str());
        return;
    }
    if ((int)m_ps.hasWeapon.size() < (int)m_script.weapons.size()) m_ps.hasWeapon.resize(m_script.weapons.size(), 0);
    bool had = m_ps.hasWeapon[idx];
    m_ps.hasWeapon[idx] = 1;
    const WeaponDef& w = m_script.weapons[idx];
    if (!w.ammoType.empty() && w.startAmmo > 0) GiveAmmo(w.ammoType, w.startAmmo);
    if (select || (!had && (m_ps.weapon < 0 || w.slot > m_script.weapons[m_ps.weapon].slot))) {
        if (m_ps.weapon < 0) m_ps.weapon = idx;
        m_ps.pendingWeapon = idx;
    }
}

static bool HasAmmoFor(const Game* g, const WeaponDef& w) {
    return w.ammoType.empty() || cvars::GetF("g_infinite_ammo") != 0 || g->Ammo(w.ammoType) >= w.ammoPerShot;
}

void Game::SelectBestWeapon() {
    int best = -1;
    for (size_t i = 0; i < m_script.weapons.size() && i < m_ps.hasWeapon.size(); ++i) {
        if (!m_ps.hasWeapon[i] || !HasAmmoFor(this, m_script.weapons[i])) continue;
        if (best < 0 || m_script.weapons[i].slot >= m_script.weapons[best].slot) best = (int)i;
    }
    if (best >= 0) {
        if (m_ps.weapon < 0) m_ps.weapon = best;
        m_ps.pendingWeapon = best;
    }
}

void Game::UpdateWeapon(float dt, bool input) {
    auto& W = m_script.weapons;
    if (W.empty()) return;
    if ((int)m_ps.hasWeapon.size() < (int)W.size()) m_ps.hasWeapon.resize(W.size(), 0);

    if (input) {
        for (int s = 0; s < 9; ++s) {
            if (!input::Pressed((Action)((int)Action::Weapon1 + s))) continue;
            // cycle through owned weapons in this slot
            std::vector<int> inSlot;
            for (size_t i = 0; i < W.size(); ++i)
                if (W[i].slot == s + 1 && m_ps.hasWeapon[i]) inSlot.push_back((int)i);
            if (inSlot.empty()) continue;
            int pick = inSlot[0];
            for (size_t k = 0; k < inSlot.size(); ++k)
                if (inSlot[k] == m_ps.weapon && k + 1 < inSlot.size()) pick = inSlot[k + 1];
            m_ps.pendingWeapon = pick;
        }
        int dir = input::Pressed(Action::NextWeapon) ? 1 : (input::Pressed(Action::PrevWeapon) ? -1 : 0);
        if (dir != 0 && m_ps.weapon >= 0) {
            int n = (int)W.size();
            for (int k = 1; k < n; ++k) {
                int i = ((m_ps.pendingWeapon >= 0 ? m_ps.pendingWeapon : m_ps.weapon) + dir * k + n * 4) % n;
                if (m_ps.hasWeapon[i]) {
                    m_ps.pendingWeapon = i;
                    break;
                }
            }
        }
    }

    // switching animation
    if (m_ps.pendingWeapon >= 0 && m_ps.pendingWeapon != m_ps.weapon) {
        m_ps.switchAnim += dt * 7.0f;
        if (m_ps.switchAnim >= 1.0f) {
            m_ps.switchAnim = 1.0f;
            m_ps.weapon = m_ps.pendingWeapon;
            PlaySound("sounds/weapon_switch.wav", m_player->pos, 0.5f, 1.0f, false);
        }
    } else {
        m_ps.switchAnim = fmaxf(0.0f, m_ps.switchAnim - dt * 7.0f);
    }
    m_ps.kick = fmaxf(0.0f, m_ps.kick - dt * 5.0f);
    m_ps.muzzleFlash = fmaxf(0.0f, m_ps.muzzleFlash - dt);

    if (m_ps.weapon < 0 || m_ps.weapon >= (int)W.size() || !m_ps.hasWeapon[m_ps.weapon]) return;
    const WeaponDef& w = W[m_ps.weapon];
    bool trigger = input && (w.automatic ? input::Down(Action::Fire) : input::Pressed(Action::Fire));
    if (trigger && m_time >= m_ps.nextFire && m_ps.switchAnim < 0.05f && m_ps.pendingWeapon == m_ps.weapon) {
        if (!HasAmmoFor(this, w)) {
            PlaySound("sounds/empty.wav", m_player->pos, 0.5f, 1.0f, false);
            m_ps.nextFire = m_time + 0.4f;
            SelectBestWeapon();
            return;
        }
        if (!w.ammoType.empty() && cvars::GetF("g_infinite_ammo") == 0) m_ps.ammo[w.ammoType] -= w.ammoPerShot;
        m_ps.nextFire = m_time + w.fireRate;
        m_ps.kick = 1.0f;
        m_ps.muzzleFlash = 0.06f;
        m_ps.punch.x += w.recoil * 1.5f;
        if (w.flashSize > 0) m_fx.AddLight(EyePos() + AimDir() * 24.0f, w.flashColor * 1.5f, w.flashSize * 14.0f, 0.08f);
        m_script.FireWeapon(m_ps.weapon, m_player);
    }
}

// ---------------------------------------------------------------------------------
// camera
// ---------------------------------------------------------------------------------
void Game::UpdateCamera(float dt) {
    RegisterPlayerCvars();
    if (!m_player) return;
    float bob = sinf(m_ps.bobTime) * 2.2f * m_ps.bobAmount * cl_bob->f;
    Vector3 eye = m_player->pos + Vector3{0, m_ps.viewHeight + bob + m_ps.stepSmooth + m_ps.landDip, 0};
    float pitch = Clampf(m_ps.pitch + m_ps.punch.x, -89.5f, 89.5f);
    float yaw = m_ps.yaw + m_ps.punch.y;
    Vector3 fwd = AnglesForward(pitch, yaw);
    float roll = (m_ps.punch.z + Dot(m_player->vel, YawRight(m_ps.yaw)) * 0.006f) * DEG2RAD;
    Vector3 right = Norm(Cross(fwd, {0, 1, 0}));
    Vector3 up = Norm(Cross(right, fwd));
    up = Norm(up * cosf(roll) + right * sinf(roll));
    m_camera.position = eye;
    m_camera.target = eye + fwd;
    m_camera.up = up;
    float hfov = Clampf(cl_fov->f, 50, 140);
    m_fovY = 2.0f * atanf(tanf(hfov * 0.5f * DEG2RAD) * 0.75f) * RAD2DEG;
    m_camera.fovy = m_fovY;
    m_camera.projection = CAMERA_PERSPECTIVE;
    m_listenerPos = eye;
    m_listenerRight = right;

    // a script camera (dialogue shots) takes over, easing towards its target and back to the player
    ScriptCamera& c = m_scriptCam;
    if (c.active) {
        Vector3 pf = m_camera.position + Norm(m_camera.target - m_camera.position) * 500.0f;
        Vector3 tp = c.releasing ? m_camera.position : c.tpos, tl = c.releasing ? pf : c.tlook;
        float tf = c.releasing ? m_camera.fovy : c.tfov;
        if (!c.init) {
            c.pos = m_camera.position;
            c.look = pf;
            c.fov = m_camera.fovy;
            c.init = true;
        }
        float k = c.rate <= 0 ? 1.0f : 1.0f - expf(-c.rate * dt);
        c.pos = c.pos + (tp - c.pos) * k;
        c.look = c.look + (tl - c.look) * k;
        c.fov = c.fov + (tf - c.fov) * k;
        Vector3 d = c.pos - tp;
        if (c.releasing && d.x * d.x + d.y * d.y + d.z * d.z < 0.25f && fabsf(c.fov - tf) < 0.2f) {
            c.active = false;  // back at the player's eyes
        } else {
            m_camera.position = c.pos;
            m_camera.target = c.look;
            m_camera.up = {0, 1, 0};
            m_camera.fovy = c.fov;
        }
    }
}

void Game::SetScriptCamera(Vector3 pos, Vector3 look, float fov, float rate) {
    ScriptCamera& c = m_scriptCam;
    if (!c.active) c.init = false;  // starting from the player's view; while it is moving, keep its position
    c.active = true;
    c.releasing = false;
    c.tpos = pos;
    c.tlook = look;
    c.tfov = fov;
    c.rate = rate;
}

void Game::ClearScriptCamera(float rate) {
    if (!m_scriptCam.active) return;
    m_scriptCam.releasing = true;
    m_scriptCam.rate = rate;
}

}  // namespace rbs

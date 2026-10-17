// Geräte-Test (Quest, arm64): Bildschirm-Geometrie aus src/quad_renderer.cpp.
// screenPoint, raycastToScreen, worldToScreenLocal und screenModelMatrix müssen
// dieselbe Fläche beschreiben — sonst laufen Klickpunkt, Cursor und Bild auseinander.
//
// Sweep: Drehung 13 × Wölbung 4 × Breite 3 × Seitenverhältnis 3 × Abstand 3 × UV 5×5
// × 2 Strahlursprünge (Kopf, Controller) + Fehlschüsse (rückwärts, daneben, darüber).
// Gebaut und ausgeführt von tests/run-device-tests.sh.

#include "quad_renderer.h"

#include <cmath>
#include <cstdio>

namespace {

int gChecks = 0, gFailures = 0;
void check(bool ok, const char* what, double a = 0, double b = 0) {
    ++gChecks;
    if (!ok && ++gFailures <= 20) std::printf("FEHLER: %s (%.6f / %.6f)\n", what, a, b);
}

// Orientierung, deren −Z-Achse in Richtung d zeigt (Yaw um Y, dann Pitch um X).
XrQuaternionf lookAlong(float dx, float dy, float dz) {
    const float len = std::sqrt(dx * dx + dy * dy + dz * dz);
    dx /= len; dy /= len; dz /= len;
    const float yaw = std::atan2(-dx, -dz);
    const float pitch = std::asin(dy);
    const float cy = std::cos(yaw * 0.5f), sy = std::sin(yaw * 0.5f);
    const float cp = std::cos(pitch * 0.5f), sp = std::sin(pitch * 0.5f);
    // q = qYaw(Y) · qPitch(X)
    return XrQuaternionf{ cy * sp, sy * cp, -sy * sp, cy * cp };
}

// Gleiche Formel wie der Renderer (−Z der Pose) — prüft lookAlong selbst.
void forward(const XrQuaternionf& q, float* d) {
    d[0] = -2 * (q.x * q.z + q.w * q.y);
    d[1] = -2 * (q.y * q.z - q.w * q.x);
    d[2] = -(1 - 2 * (q.x * q.x + q.y * q.y));
}

} // namespace

int main() {
    const float yaws[] = { -3.0f, -2.5f, -2.0f, -1.5f, -1.0f, -0.5f, 0.0f, 0.5f, 1.0f, 1.5f, 2.0f, 2.5f, 3.0f };
    const float curveDeg[] = { 0.0f, 30.0f, 60.0f, 90.0f };
    const float widths[] = { 1.2f, 2.4f, 4.0f };
    const float aspects[] = { 4.0f / 3.0f, 10.0f / 9.0f, 8.0f / 7.0f };
    const float distances[] = { 1.0f, 2.0f, 3.0f };
    const float head[3] = { 0.1f, 1.6f, -0.2f };

    int hits = 0;
    for (float yaw : yaws)
    for (float deg : curveDeg)
    for (float w : widths)
    for (float aspect : aspects)
    for (float dist : distances) {
        ScreenState s;
        s.width = w;
        s.height = w / aspect;
        s.yaw = yaw;
        s.curveRadius = deg > 0.0f ? w / (deg * 3.14159265f / 180.0f) : 0.0f;
        // Bild vor dem Kopf, Normale (sin yaw, 0, cos yaw) zeigt zum Kopf.
        s.posX = head[0] - std::sin(yaw) * dist;
        s.posY = head[1] - 0.1f;
        s.posZ = head[2] - std::cos(yaw) * dist;

        // Modellmatrix bildet die lokale Bildmitte auf pos ab.
        float m[16];
        screenModelMatrix(s, m);
        check(std::fabs(m[12] - s.posX) < 1e-5f && std::fabs(m[14] - s.posZ) < 1e-5f, "Modellmatrix: Position");

        // Bildmitte liegt auf pos (Zylinder: Mitte der Bogenfläche).
        float c[3];
        screenPoint(s, 0.5f, 0.5f, c);
        check(std::fabs(c[0] - s.posX) < 1e-4f && std::fabs(c[1] - s.posY) < 1e-4f &&
              std::fabs(c[2] - s.posZ) < 1e-4f, "Bildmitte = pos");

        for (int iu = 0; iu < 5; ++iu)
        for (int iv = 0; iv < 5; ++iv) {
            const float u = 0.1f + 0.2f * static_cast<float>(iu);
            const float v = 0.1f + 0.2f * static_cast<float>(iv);
            float p[3];
            screenPoint(s, u, v, p);
            // Ursprung 1: Kopf; Ursprung 2: Controller rechts unten vor dem Kopf.
            const float origins[2][3] = { { head[0], head[1], head[2] },
                                          { head[0] + 0.25f, head[1] - 0.35f, head[2] - 0.2f } };
            for (const auto& o : origins) {
                XrPosef aim{};
                aim.position = { o[0], o[1], o[2] };
                aim.orientation = lookAlong(p[0] - o[0], p[1] - o[1], p[2] - o[2]);
                float d[3];
                forward(aim.orientation, d);
                const float len = std::sqrt((p[0] - o[0]) * (p[0] - o[0]) + (p[1] - o[1]) * (p[1] - o[1]) +
                                            (p[2] - o[2]) * (p[2] - o[2]));
                check(std::fabs(d[0] * len - (p[0] - o[0])) < 1e-3f, "lookAlong-Selbsttest");
                const RayHit hit = raycastToScreen(s, aim);
                check(hit.hit, "Treffer erwartet", u, v);
                check(std::fabs(hit.u - u) < 2e-4f, "u", hit.u, u);
                check(std::fabs(hit.v - v) < 2e-4f, "v", hit.v, v);
                check(std::fabs(hit.distance - len) < 2e-3f, "Abstand", hit.distance, len);
                if (hit.hit) ++hits;

                // Lokale Koordinaten: der Punkt hat im Bildschirmraum die lokale Geometrie.
                const XrPosef local = worldToScreenLocal(s, aim);
                const RayHit localHit = raycastToScreen(localScreen(s), local);
                check(localHit.hit && std::fabs(localHit.u - u) < 2e-4f && std::fabs(localHit.v - v) < 2e-4f,
                      "worldToScreenLocal konsistent", localHit.u, u);
            }
        }

        // Fehlschüsse: vom Bild weg, knapp daneben (u > 1), darüber (v < 0).
        XrPosef away{};
        away.position = { head[0], head[1], head[2] };
        away.orientation = lookAlong(std::sin(yaw), 0.0f, std::cos(yaw));   // zeigt vom Bild weg
        check(!raycastToScreen(s, away).hit, "rückwärts: kein Treffer");
        float edge[3], top[3];
        screenPoint(s, 1.0f, 0.5f, edge);
        screenPoint(s, 0.5f, 0.0f, top);
        const float side[3] = { std::cos(yaw) * 0.3f, 0.0f, -std::sin(yaw) * 0.3f };   // lokal +X
        XrPosef beside{};
        beside.position = away.position;
        beside.orientation = lookAlong(edge[0] + side[0] - head[0], edge[1] - head[1], edge[2] + side[2] - head[2]);
        check(!raycastToScreen(s, beside).hit, "daneben: kein Treffer");
        XrPosef above{};
        above.position = away.position;
        above.orientation = lookAlong(top[0] - head[0], top[1] + 0.3f - head[1], top[2] - head[2]);
        check(!raycastToScreen(s, above).hit, "darüber: kein Treffer");
    }

    std::printf("test_geometry: %d Prüfungen, %d Treffer, %d Fehler\n", gChecks, hits, gFailures);
    return gFailures == 0 ? 0 : 1;
}

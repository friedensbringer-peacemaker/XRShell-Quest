// Host-Test für src/cursor_lock.h („Zeiger lernen“: Spielzeiger im Speicher finden und an den
// Laser regeln). Simuliert DOS-Spiele, die Mickeys im eigenen Takt abholen und mit eigenem
// Maßstab und Randanschlag in ihre Zeigerposition umrechnen — dazu Störer im Speicher:
// Bildzähler, verrauschter Puffer, nachlaufende Kopie des Zeigers, ungeklemmter Mickey-Zähler.
// Sweep: Maßstab × Lesetakt × Bereich × 16/32-Bit × gerade/ungerade Adresse.
// Gebaut von tests/run-host-tests.sh.

#include "cursor_lock.h"

#include <cmath>
#include <cstdio>
#include <random>
#include <string>
#include <vector>

namespace {

int gChecks = 0, gFailures = 0;
void check(bool ok, const std::string& what) {
    ++gChecks;
    if (!ok && ++gFailures <= 30) std::printf("FEHLER: %s\n", what.c_str());
}

struct Game {
    std::vector<uint8_t> conv = std::vector<uint8_t>(640 * 1024);
    std::vector<uint8_t> ext = std::vector<uint8_t>(2 * 1024 * 1024);
    static constexpr uint32_t kExt = 0x200000;
    uint32_t ax = kExt + 0x12340, ay = kExt + 0x12342, copyX = kExt + 0x40000;
    bool wide = false;              // 32-Bit-Werte
    float scaleX = 1.0f, scaleY = 1.0f;   // Spieleinheiten je Mickey
    int maxX = 319, maxY = 199;
    int readEvery = 2;              // Spiel holt Mickeys alle k Polls ab
    int capPerRead = 0;             // > 0: Zeiger bewegt sich höchstens so weit je Abfrage (Worms-Waffenleiste)
    bool reading = true;            // false: anderer Bildschirm, Zeiger wird nicht mehr geführt
    int x = 160, y = 100;
    float remX = 0, remY = 0;
    int mickX = 0, mickY = 0;
    long rawMick = 0;
    int poll = 0;
    std::mt19937 rng{ 7 };

    std::vector<curlock::Region> regs() const {
        return { { conv.data(), 0, static_cast<uint32_t>(conv.size()) },
                 { ext.data(), kExt, static_cast<uint32_t>(ext.size()) } };
    }
    uint8_t* at(uint32_t addr) { return addr >= kExt ? &ext[addr - kExt] : &conv[addr]; }
    void put(uint32_t addr, uint32_t v, int bytes) {
        for (int i = 0; i < bytes; ++i) at(addr)[i] = static_cast<uint8_t>(v >> (8 * i));
    }
    void write() {
        put(ax, static_cast<uint32_t>(x), wide ? 4 : 2);
        put(ay, static_cast<uint32_t>(y), wide ? 4 : 2);
    }
    void step(int dx, int dy) {
        mickX += dx;
        mickY += dy;
        rawMick += dx;
        ++poll;
        // Störer: Bildzähler, Rauschpuffer, ungeklemmter Mickey-Zähler (32 Bit).
        put(0x8000, static_cast<uint32_t>(poll), 2);
        for (int i = 0; i < 256; ++i) conv[0x20000 + (rng() % 4096)] = static_cast<uint8_t>(rng());
        put(kExt + 0x50000, static_cast<uint32_t>(rawMick), 4);
        if (!reading || poll % readEvery != 0) return;
        put(copyX, static_cast<uint32_t>(x), 2);   // Kopie für das Neuzeichnen (alter Wert)
        const float fx = remX + mickX * scaleX, fy = remY + mickY * scaleY;
        const int ix = static_cast<int>(std::floor(fx)), iy = static_cast<int>(std::floor(fy));
        remX = fx - ix;
        remY = fy - iy;
        const int cx = capPerRead > 0 ? std::min(std::max(ix, -capPerRead), capPerRead) : ix;
        const int cy = capPerRead > 0 ? std::min(std::max(iy, -capPerRead), capPerRead) : iy;
        x = std::min(std::max(x + cx, 0), maxX);
        y = std::min(std::max(y + cy, 0), maxY);
        mickX = mickY = 0;
        write();
    }
};

bool learn(Game& g, curlock::Lock& out, std::string& msg) {
    curlock::Learner l;
    l.start(320, 200);
    for (int i = 0; i < 400 && l.running(); ++i) {
        int dx = 0, dy = 0;
        l.poll(g.regs(), dx, dy);
        g.step(dx, dy);
    }
    msg = l.message();
    if (l.state() != curlock::Learner::State::Ok) return false;
    out = l.lock();
    return true;
}

void testLearnAndFollow() {
    int cases = 0;
    for (float scale : { 0.5f, 1.0f, 2.0f })
        for (int readEvery : { 1, 2, 4 })
            for (int range : { 1, 2, 3 })   // Zeigerbereich = Bild, doppelt (640er-Raster), Bild ohne Leiste unten
                for (bool wide : { false, true })
                    for (bool odd : { false, true }) {
                        Game g;
                        g.scaleX = scale;
                        g.scaleY = scale * 0.5f;   // Y oft halb so schnell (Mickey-Verhältnis 2:1)
                        g.readEvery = readEvery;
                        g.maxX = range == 2 ? 639 : 317;
                        g.maxY = range == 2 ? 399 : range == 3 ? 167 : 199;   // 3: wie Worms (Leiste ab Zeile 168)
                        g.wide = wide;
                        g.ax = Game::kExt + 0x12340 + (odd ? 1 : 0);
                        g.ay = g.ax + (wide ? 4 : 2);
                        g.write();
                        char tag[96];
                        std::snprintf(tag, sizeof(tag), "Maßstab %.1f, Takt %d, Bereich %d×, %s, %s", scale,
                                      readEvery, range, wide ? "32 Bit" : "16 Bit", odd ? "ungerade" : "gerade");
                        curlock::Lock lock;
                        std::string msg;
                        const bool ok = learn(g, lock, msg);
                        check(ok, std::string("gelernt: ") + tag + " — " + msg);
                        if (!ok) continue;
                        check(lock.x.addr == g.ax && lock.y.addr == g.ay, std::string("Adressen: ") + tag + " — " + msg);
                        check(lock.x.min == 0 && lock.x.max == g.maxX && lock.y.min == 0 && lock.y.max == g.maxY,
                              std::string("Bereich: ") + tag);
                        check(std::fabs(lock.x.perDelta - g.scaleX) < 0.15f * g.scaleX + 0.05f &&
                                  std::fabs(lock.y.perDelta - g.scaleY) < 0.15f * g.scaleY + 0.05f,
                              std::string("Maßstab erkannt: ") + tag);
                        // Folgen: feste Laserpunkte, Zeiger muss auf ≤ 1 Einheit (bzw. Maßstab) heran.
                        curlock::Follower f;
                        f.set(lock);
                        std::mt19937 rng(11);
                        for (int t = 0; t < 12; ++t) {
                            const float u = (rng() % 1001) / 1000.0f, v = (rng() % 1001) / 1000.0f;
                            for (int i = 0; i < 40 + 12 * readEvery; ++i) {
                                int dx = 0, dy = 0;
                                f.poll(g.regs(), u, v, true, dx, dy);
                                g.step(dx, dy);
                            }
                            // Ziel in Spieleinheiten: Pixel × Raster, am Anschlag geklemmt.
                            const float s2 = range == 2 ? 2.0f : 1.0f;
                            const float tx = std::min(u * 320.0f * s2, static_cast<float>(g.maxX));
                            const float ty = std::min(v * 200.0f * s2, static_cast<float>(g.maxY));
                            check(std::fabs(g.x - tx) <= std::max(1.0f, g.scaleX) + 0.01f &&
                                      std::fabs(g.y - ty) <= std::max(1.0f, g.scaleY) + 0.01f,
                                  std::string("folgt: ") + tag + " Ziel " + std::to_string(tx) + "/" +
                                      std::to_string(ty) + " ist " + std::to_string(g.x) + "/" + std::to_string(g.y));
                        }
                        check(f.active() && !f.lost(), std::string("bleibt gekoppelt: ") + tag);
                        // Kein Überschwingen bei langsamem Lesetakt: Laser fährt quer, dann still.
                        int maxOver = 0;
                        for (int i = 0; i < 120; ++i) {
                            int dx = 0, dy = 0;
                            f.poll(g.regs(), 0.9f, 0.5f, true, dx, dy);
                            g.step(dx, dy);
                            maxOver = std::max(maxOver, g.x - static_cast<int>(std::lround(0.9f * 320.0f * (range == 2 ? 2 : 1))));
                        }
                        check(maxOver <= std::max(1, static_cast<int>(std::ceil(g.scaleX))), std::string("kein Überschwingen: ") + tag);
                        // Bildschirm ohne Zeiger: Spiel liest nicht mehr → Kopplung verloren.
                        g.reading = false;
                        for (int i = 0; i < 200 && f.active(); ++i) {
                            int dx = 0, dy = 0;
                            f.poll(g.regs(), 0.1f, 0.1f, true, dx, dy);
                            g.step(dx, dy);
                        }
                        check(!f.active() && f.lost(), std::string("verloren erkannt: ") + tag);
                        // Spiel liest wieder (Überblendung vorbei): relative Laser-Deltas → wieder gekoppelt.
                        g.reading = true;
                        bool resumed = false;
                        for (int i = 0; i < 300 && !resumed; ++i) {
                            const int rdx = (i / 20) % 2 ? -3 : 3, rdy = (i / 30) % 2 ? -2 : 2;   // Laser wandert
                            resumed = f.observe(g.regs(), rdx, rdy);
                            g.step(rdx, rdy);
                        }
                        check(resumed && f.active() && !f.lost(), std::string("wieder gekoppelt: ") + tag);
                        ++cases;
                    }
    check(cases == 3 * 3 * 3 * 2 * 2, "Sweep vollständig");
}

void testCappedSpeed() {
    // Zeiger bewegt sich höchstens 12 Einheiten je Abfrage (Rest verfällt): Bereich muss trotzdem
    // voll erkannt werden, sonst klemmt der Pfeil vor dem Rand (Worms-Waffenleiste: 122…250).
    for (int readEvery : { 1, 2 }) {
        Game g;
        g.capPerRead = 12;
        g.readEvery = readEvery;
        g.write();
        curlock::Lock lock;
        std::string msg;
        check(learn(g, lock, msg), "gebremst: gelernt — " + msg);
        check(lock.x.min == 0 && lock.x.max == g.maxX && lock.y.min == 0 && lock.y.max == g.maxY,
              "gebremst: voller Bereich (" + std::to_string(lock.x.min) + "…" + std::to_string(lock.x.max) + ")");
        curlock::Follower f;
        f.set(lock);
        for (float u : { 0.05f, 0.95f, 0.5f }) {
            for (int i = 0; i < 150; ++i) {
                int dx = 0, dy = 0;
                f.poll(g.regs(), u, 0.5f, true, dx, dy);
                g.step(dx, dy);
            }
            const int want = std::min(static_cast<int>(u * 320), g.maxX);
            check(std::abs(g.x - want) <= 1, "gebremst: folgt bis " + std::to_string(u) + " ist " + std::to_string(g.x) +
                                                 " soll " + std::to_string(want) + " aktiv " + std::to_string(f.active()));
        }
        check(f.active(), "gebremst: bleibt gekoppelt");
    }
}

void testWarp() {
    // Bildschirmwechsel: Spiel setzt den Zeiger selbst (z. B. auf „EXIT“) und verwirft die Mickeys,
    // die gerade unterwegs sind. Der Pfeil muss ohne Laserbewegung zurück an den Laser.
    Game g;
    g.write();
    curlock::Lock lock;
    std::string msg;
    check(learn(g, lock, msg), "Sprung: gelernt");
    curlock::Follower f;
    f.set(lock);
    auto runFor = [&](int n, float u, float v) {
        for (int i = 0; i < n; ++i) {
            int dx = 0, dy = 0;
            f.poll(g.regs(), u, v, true, dx, dy);
            g.step(dx, dy);
        }
    };
    runFor(60, 0.45f, 0.5f);
    // Sprung + 40 Polls Überblendung, in der das Spiel Mickeys verwirft
    g.x = 280;
    g.y = 156;
    g.write();
    for (int i = 0; i < 40; ++i) {
        int dx = 0, dy = 0;
        f.poll(g.regs(), 0.45f, 0.5f, true, dx, dy);
        g.mickX = g.mickY = 0;   // verworfen
        ++g.poll;
    }
    runFor(60, 0.45f, 0.5f);
    check(std::abs(g.x - 144) <= 1 && std::abs(g.y - 100) <= 1 && f.active(),
          "Sprung: zurück am Laser ohne Pause (" + std::to_string(g.x) + "/" + std::to_string(g.y) + ")");
}

void testProbe() {
    // Gespeicherte Kopplung, Laser steht still: Probebewegung koppelt an, ohne Laserbewegung.
    Game g;
    g.write();
    curlock::Lock lock;
    std::string msg;
    check(learn(g, lock, msg), "Probe: gelernt");
    curlock::Follower f;
    f.setAll({ lock });
    bool on = false;
    for (int i = 0; i < 200 && !on; ++i) {
        int pdx = 0, pdy = 0;
        on = f.observe(g.regs(), 0.8f, 0.2f, true, 0, 0, pdx, pdy);
        g.step(pdx, pdy);
    }
    check(on && f.active(), "Probe: angekoppelt bei ruhigem Laser");
    // Falsche Kopplung (Speicher steht still): Probe darf nicht ankoppeln.
    curlock::Lock wrong = lock;
    wrong.x.addr = Game::kExt + 0x70000;
    wrong.y.addr = Game::kExt + 0x70002;
    curlock::Follower f2;
    f2.setAll({ wrong });
    on = false;
    for (int i = 0; i < 300 && !on; ++i) {
        int pdx = 0, pdy = 0;
        on = f2.observe(g.regs(), 0.8f, 0.2f, true, 0, 0, pdx, pdy);
        g.step(pdx, pdy);
    }
    check(!on && f2.lost(), "Probe: tote Kopplung bleibt pausiert");
}

void testNoCursor() {
    // Spiel ohne Mauszeiger (liest nie): Lernen schlägt mit Meldung fehl.
    Game g;
    g.reading = false;
    curlock::Lock lock;
    std::string msg;
    check(!learn(g, lock, msg), "ohne Zeiger kein Ergebnis");
    check(!msg.empty(), "Fehlermeldung vorhanden");
}

void testLaserOff() {
    Game g;
    g.write();
    curlock::Lock lock;
    std::string msg;
    check(learn(g, lock, msg), "Lernen für Laser-aus-Test");
    curlock::Follower f;
    f.set(lock);
    const int x0 = g.x, y0 = g.y;
    for (int i = 0; i < 100; ++i) {
        int dx = 0, dy = 0;
        f.poll(g.regs(), 0.5f, 0.5f, false, dx, dy);
        check(dx == 0 && dy == 0, "Laser weg: keine Bewegung");
        g.step(dx, dy);
    }
    check(g.x == x0 && g.y == y0 && f.active(), "Laser weg: Zeiger bleibt, Kopplung bleibt");
}

void testTwoPrograms() {
    // Menü und Spielrunde als eigene Programme: Zeiger an verschiedenen Adressen. Beide gelernt →
    // die Kopplung wechselt beim Programmwechsel von selbst auf die passende.
    Game menu, round;
    round.ax = Game::kExt + 0x30010;
    round.ay = Game::kExt + 0x30012;
    round.maxY = 167;
    menu.write();
    round.write();
    curlock::Lock lm, lr;
    std::string msg;
    check(learn(menu, lm, msg), "Menü gelernt");
    check(learn(round, lr, msg), "Runde gelernt");
    std::vector<curlock::Lock> all = curlock::addLock(curlock::addLock({}, lm), lr);
    check(all.size() == 2 && all[0].x.addr == lr.x.addr, "neue Kopplung vorn, alte bleibt");
    check(curlock::addLock(all, lr).size() == 2, "gleiche Adressen ersetzen statt verdoppeln");
    std::vector<curlock::Lock> back = curlock::decodeList(curlock::encodeList(all));
    check(back.size() == 2 && back[1].x.addr == lm.x.addr, "Liste: Rundweg");
    // Gespeicherte Liste laden → pausiert; im Menü bewegt die relative Maus → Menü-Kopplung aktiv.
    curlock::Follower f;
    f.setAll(back);
    check(!f.active() && f.lost(), "geladen: erst beobachten");
    // Die Runde läuft nicht: ihr Speicher steht still. Beide Spiele teilen sich hier keinen
    // Speicher, daher getrennt beobachten — erst Menü.
    bool on = false;
    for (int i = 0; i < 200 && !on; ++i) {
        const int d = (i / 15) % 2 ? -4 : 4;
        on = f.observe(menu.regs(), d, d);
        menu.step(d, d);
    }
    check(on && f.active() && f.lock().x.addr == lm.x.addr, "Menü-Kopplung gewählt");
    for (int i = 0; i < 80; ++i) {
        int dx = 0, dy = 0;
        f.poll(menu.regs(), 0.25f, 0.5f, true, dx, dy);
        menu.step(dx, dy);
    }
    check(std::abs(menu.x - 80) <= 1 && std::abs(menu.y - 100) <= 1, "Menü folgt dem Laser");
    // Programmwechsel: Menü-Wert steht → pausiert; Runde reagiert → Runden-Kopplung.
    for (int i = 0; i < 200 && f.active(); ++i) {
        int dx = 0, dy = 0;
        f.poll(round.regs(), 0.9f, 0.9f, true, dx, dy);   // Menüadresse liegt im Rundenspeicher still
        round.step(dx, dy);
    }
    check(f.lost(), "Programmwechsel: pausiert");
    on = false;
    for (int i = 0; i < 300 && !on; ++i) {
        const int d = (i / 15) % 2 ? -4 : 4;
        on = f.observe(round.regs(), d, d);
        round.step(d, d);
    }
    check(on && f.lock().x.addr == lr.x.addr, "Runden-Kopplung gewählt");
    for (int i = 0; i < 80; ++i) {
        int dx = 0, dy = 0;
        f.poll(round.regs(), 0.5f, 0.95f, true, dx, dy);
        round.step(dx, dy);
    }
    check(std::abs(round.x - 160) <= 1 && round.y == 167, "Runde folgt, unten am Anschlag 167");
}

void testEncode() {
    curlock::Lock l;
    l.x = { 0x2123A1, 0, 639, 0.5f };
    l.y = { 0x2123A3, 0, 199, 0.25f };
    curlock::Lock back;
    l.x.frame = 320;
    l.y.frame = 200;
    check(curlock::Lock::decode(l.encode(), back), "Rundweg dekodierbar: " + l.encode());
    check(back.x.frame == 320 && back.y.frame == 200 && back.x.unitsPerPixel() == 2 && back.y.unitsPerPixel() == 1,
          "Bildgröße und Raster im Rundweg");
    curlock::Lock old;
    check(curlock::Lock::decode("x=391CC4,0,317,1.0000;y=391CC8,0,167,0.5000", old) && old.x.frame == 0 &&
              std::fabs(old.y.target(1.0f) - 167.0f) < 0.01f,
          "ältere Einträge ohne Bildgröße bleiben lesbar");
    curlock::Axis worms{ 0x391CC8, 0, 167, 0.5f, 200 };
    check(std::fabs(worms.target(0.5f) - 100.0f) < 0.01f && std::fabs(worms.target(0.95f) - 167.0f) < 0.01f,
          "Worms-Y: Mitte = Zeile 100, unten am Anschlag 167");
    check(back.x.addr == l.x.addr && back.y.addr == l.y.addr && back.x.max == 639 && back.y.max == 199 &&
              std::fabs(back.x.perDelta - 0.5f) < 1e-4f && std::fabs(back.y.perDelta - 0.25f) < 1e-4f,
          "Rundweg Werte");
    check(!curlock::Lock::decode("", back) && !curlock::Lock::decode("x=1,5,5,1;y=2,0,9,1", back) &&
              !curlock::Lock::decode("x=1,0,9,0;y=2,0,9,1", back) && !curlock::Lock::decode("Unsinn", back),
          "Ungültiges wird abgelehnt");
    // Speicher außerhalb aller Bereiche: Folgen verliert sofort, statt Unsinn zu senden.
    std::vector<uint8_t> mem(64);
    std::vector<curlock::Region> regs{ { mem.data(), 0, 64 } };
    curlock::Follower f;
    f.set(l);
    int dx = 0, dy = 0;
    f.poll(regs, 0.5f, 0.5f, true, dx, dy);
    check(dx == 0 && dy == 0 && f.lost(), "Adresse fehlt → verloren");
    check(!f.observe(regs, 5, 5) && f.lost(), "Adresse fehlt → bleibt pausiert");
}

} // namespace

int main() {
    testEncode();
    testTwoPrograms();
    testLaserOff();
    testCappedSpeed();
    testWarp();
    testProbe();
    testNoCursor();
    testLearnAndFollow();
    std::printf("test_cursor_lock: %d Prüfungen, %d Fehler\n", gChecks, gFailures);
    return gFailures == 0 ? 0 : 1;
}

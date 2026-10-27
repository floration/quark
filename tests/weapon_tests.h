// Weapon sets (.qweapons, src/game/weaponset.h, tools/maptool/weapon/DESIGN.md): the standard set is today's game bit
// for bit, the grammar (canonical text, errors with line:col), the interpreter, the rules' network / disk forms and
// a set changing damage, headshots, falloff and logic slots through the real Server. Plus the editor's quark_tests
// tools: exportweapons / checkweapons / weaponsast / printweapons.
// Included by tests.cpp (it shares CHECK, pump, idle, TP and rulesStart).

// Every field of the calling thread's tuned weapons (weaponDef / weaponExtra / weaponAlt), as values.
static WeaponValues wsActiveValues() {
    WeaponValues v = standardWeaponValues();
    for (int w = 0; w < WPN_COUNT; w++) { v.def[w] = weaponDef(w); v.ex[w] = weaponExtra(w); }
    v.alt = weaponAlt();
    return v;
}
// The fields that differ (bit for bit: the floats compared as their bits), "" when none.
static std::string wsDiff(const WeaponValues& a, const WeaponValues& b) {
    std::string d;
    for (int i = 0; i < weaponFieldCount(); i++) {
        double x = getWeaponField(a, i), y = getWeaponField(b, i);
        float fx = (float)x, fy = (float)y;
        if (memcmp(&fx, &fy, sizeof fx) != 0 || x != y) d += " " + weaponField(i).key;
    }
    for (int w = 0; w < WPN_COUNT; w++)
        if (a.ex[w].falloff != b.ex[w].falloff || a.ex[w].curveKind != b.ex[w].curveKind) d += std::string(" ") + weaponKey(w) + ".curve";
    return d;
}

static void testWeaponSetDefault() {
    printf("weapon sets: the standard set is today's game\n");
    const std::string t = standardWeaponSetText();
    WsDiag d;
    CHECK(printWeaponSetText(t, &d) == t);   // canonical: print(parse(t)) == t
    auto s = compileWeaponSet(t, &d);
    CHECK(s && d.errors.empty() && d.warnings.empty());
    if (!s) { printf("%s\n", weaponSetDiagJson(d).c_str()); return; }
    CHECK(!weaponSetHasLogic(*s));
    std::string diff = wsDiff(weaponSetValues(*s), standardWeaponValues());
    if (!diff.empty()) printf("  differs:%s\n", diff.c_str());
    CHECK(diff.empty());
    // the standard values are the gamedefs.h constants
    const WeaponValues& sv = standardWeaponValues();
    CHECK(sv.ex[WPN_SNIPER].headDamage == kSniperHeadDamage && sv.ex[WPN_CROSSBOW].headDamage == kCrossbowHeadDamage);
    CHECK(sv.ex[WPN_RIFLE].headDamage == kRifleHeadDamage && sv.ex[WPN_SHOTGUN].headDamage == kPelletHeadDamage && sv.ex[WPN_ROCKET].headDamage == 0);
    CHECK(sv.ex[WPN_ROCKET].directDamage == kRocketDirectDamage && sv.alt.lobGravity == kRocketLobGravity && sv.alt.stickyMax == kMaxStickies);
    for (int w = 0; w < WPN_COUNT; w++) CHECK(!sv.ex[w].falloff && sv.ex[w].wallbang == kWallbangDamage);
    // the tuned table: no rules, rules without a set, the same rules with the standard set's text: identical
    setActiveRules(CustomRules());
    CHECK(wsDiff(wsActiveValues(), standardWeaponValues()).empty());
    CHECK(headDamage(WPN_SNIPER) == kSniperHeadDamage && activeWeaponLogic() == nullptr);
    CustomRules r;
    r.name = "multipliers";
    r.damage[WPN_SNIPER] = 1.37f; r.damage[WPN_ROCKET] = 0.61f; r.damage[WPN_SYRINGE] = 2.3f; r.fireRate[WPN_RIFLE] = 1.9f; r.projSpeed[WPN_BUBBLE] = 1.3f;
    setActiveRules(r);
    WeaponValues plain = wsActiveValues();
    CHECK(headDamage(WPN_SNIPER) == 1.37f * kSniperHeadDamage);                  // (what tunedDamage(w) * k gave)
    CHECK(weaponExtra(WPN_ROCKET).directDamage == kRocketDirectDamage * 0.61f);
    CHECK(weaponAlt().syringeHeal == kSyringeHeal * 2.3f);
    r.weaponSetName = "standard"; r.weaponSetText = t;
    setActiveRules(r);
    diff = wsDiff(wsActiveValues(), plain);
    if (!diff.empty()) printf("  with the standard set, differs:%s\n", diff.c_str());
    CHECK(diff.empty() && activeWeaponLogic() == nullptr);
    for (int w = 0; w < WPN_COUNT; w++) CHECK(std::string(weaponDef(w).name) == baseWeaponDef(w).name);
    setActiveRules(CustomRules());
}

static void testWeaponSetGrammar() {
    printf("weapon sets: grammar, canonical text, errors with line:col\n");
    // canonical printing (spacing, literals, inline arms) and idempotence
    std::string messy = "version 1\nweapons \"x\"\nlabel rocket \"HOT ROCKET\"\nparam rocket.damage f32 = 40.0\nparam bonus f32 = 1.50\n"
                        "curve rocket.falloff(dist f32) = points 0: 1, 20:0.6\n# the head bonus\nslot hit:\n    if head and dist>30:   damage=damage*bonus   # far\n"
                        "    elif (airborne):\n        damage = damage * 2.0f\n        heal = 1\n";
    WsDiag d;
    std::string canon = printWeaponSetText(messy, &d);
    CHECK(d.errors.empty());
    CHECK(canon == "version 1\nweapons \"x\"\nlabel rocket \"HOT ROCKET\"\nparam rocket.damage f32 = 40\nparam bonus f32 = 1.5\n"
                   "curve rocket.falloff(dist f32) = points 0: 1, 20: 0.6\n# the head bonus\nslot hit:\n  if head and dist > 30: damage = damage * bonus # far\n"
                   "  elif airborne:\n    damage = damage * 2.0f\n    heal = 1\n");
    CHECK(printWeaponSetText(canon, &d) == canon);
    auto s = compileWeaponSet(canon, &d);
    if (!s) printf("  %s\n", weaponSetDiagJson(d).c_str());
    CHECK(s && weaponSetHasLogic(*s) && weaponSetLabel(*s, WPN_ROCKET) == "HOT ROCKET");
    if (s) {
        CHECK(weaponSetValues(*s).def[WPN_ROCKET].damage == 40 && weaponSetWrites(*s, weaponFieldIndex("rocket.damage")) && !weaponSetWrites(*s, weaponFieldIndex("rocket.refire")));
        const WeaponExtra& e = weaponSetValues(*s).ex[WPN_ROCKET];
        CHECK(e.falloff && std::fabs(weaponFalloff(e, 10) - 0.8f) < 1e-6f && weaponFalloff(e, 50) == 0.6f);
    }
    std::string ast = weaponSetAstJson(canon, &d);
    CHECK(ast.find("\"format\":\"qweapons-ast\"") != std::string::npos && ast.find("\"kind\":\"label\"") != std::string::npos && ast.find("\"ref\":\"sensor\"") != std::string::npos);
    // errors: line:col
    struct Bad { const char* body; int line, col; const char* has; };
    const Bad bad[] = {
        {"param rocket.dmg f32 = 1\n", 3, 1, "not a field of rocket"},
        {"param shotgun.pellets f32 = 16\n", 3, 1, "is int"},
        {"param rocket.projectile bool = no\n", 3, 1, "kind"},
        {"param rocket.damage f32 = 1 2 3 4\n", 3, 29, "one value"},
        {"label rocket \"hot\"\n", 3, 14, "A-Z"},
        {"slot hit:\n  damage = dammage * 2\n", 4, 12, "unknown name 'dammage'"},
        {"slot hit:\n  damage = 1.5\n", 4, 3, "f32, not f64"},
        {"slot hit:\n  x = 1 / 2\n", 4, 7, "int / int"},
        {"slot hit:\n  if head: y = 1.0f\n  damage = y\n", 5, 12, "may not be assigned"},
        {"slot kill:\n  damage = 1\n", 4, 3, "not an output of slot kill"},
        {"slot kill:\n  heal = splash\n", 4, 10, "not a sensor of slot kill"},
        {"slot hit:\n  pass\nslot hit:\n  pass\n", 5, 1, "a second slot hit"},
        {"slot hit rail:\n  pass\n", 3, 10, "not a weapon"},
        {"slot hit:\n  if dist: pass\n", 4, 6, "bool"},
        {"slot hit:\n\tpass\n", 4, 1, "tab"},
        {"param weapon f32 = 1\n", 3, 1, "reserved"},
    };
    for (const Bad& b : bad) {
        WsDiag e;
        std::string text = std::string("version 1\nweapons \"bad\"\n") + b.body;
        bool compiled = compileWeaponSet(text, &e) != nullptr;
        bool ok = !compiled && !e.errors.empty() && e.errors[0].line == b.line && e.errors[0].col == b.col && e.errors[0].msg.find(b.has) != std::string::npos;
        if (!ok) printf("  %s -> %s\n", b.body, weaponSetDiagJson(e).c_str());
        CHECK(ok);
    }
    // a value out of range: clamped, with a warning (not an error)
    WsDiag w;
    auto c = compileWeaponSet("version 1\nweapons \"w\"\nparam rocket.damage f32 = 5000\nparam shotgun.pellets int = 0\n", &w);
    CHECK(c && w.errors.empty() && w.warnings.size() == 2 && w.warnings[0].line == 3 && w.warnings[0].col == 27);
    if (c) CHECK(weaponSetValues(*c).def[WPN_ROCKET].damage == 1000 && weaponSetValues(*c).def[WPN_SHOTGUN].pellets == 1);
    // a newer version: an error
    WsDiag v;
    CHECK(!compileWeaponSet("version 9\nweapons \"v\"\n", &v) && !v.errors.empty() && v.errors[0].line == 1);
    // the catalog
    std::string cat = exportWeaponsJson();
    CHECK(cat.find("\"key\":\"rocket.damage\"") != std::string::npos && cat.find("\"key\":\"bubble.streamMax\"") != std::string::npos);
    CHECK(cat.find("\"readOnly\":true") != std::string::npos && cat.find("\"defaultText\":") != std::string::npos && cat.find("\"name\":\"streak\"") != std::string::npos);
    int depth = 0, minDepth = 0;
    bool inStr = false;
    for (size_t i = 0; i < cat.size(); i++) {
        char ch = cat[i];
        if (inStr) { if (ch == '\\') i++; else if (ch == '"') inStr = false; continue; }
        if (ch == '"') inStr = true; else if (ch == '{' || ch == '[') depth++; else if (ch == '}' || ch == ']') minDepth = std::min(minDepth, --depth);
    }
    CHECK(depth == 0 && minDepth == 0 && !inStr);   // (balanced)
    auto st = computeWeaponStats(standardWeaponValues());
    bool sniper = false;
    for (auto& x : st)
        if (x.weapon == WPN_SNIPER && !x.alt) { sniper = true; CHECK(x.dmgBody == 40 && x.dmgHead == 50 && x.shotsBody == 3 && x.shotsHead == 2 && std::fabs(x.ttkHead - 1.5f) < 1e-5f); }
    CHECK(sniper);
}

static void testWeaponSetLogic() {
    printf("weapon sets: the interpreter\n");
    const char* text =
        "version 1\nweapons \"logic\"\nparam bonus f32 = 1.5\nparam far f64 = 30\n"
        "curve ramp(d f32) = clamp(d / 10.0f, 0.0f, 2.0f)\ncurve steps1(d f32) = steps 1, 10: 2, 20: 3\n"
        "slot hit:\n"
        "  if head and f64(dist) > far:\n    damage = damage * bonus\n"
        "  elif airborne:\n    damage = damage * 2\n"
        "  else:\n    k = ramp(dist)\n    knock = k\n"
        "  if streak >= 3: heal = f32(streak)\n"
        "slot hit sniper:\n  return damage + steps1(dist), 0.5f\n"
        "slot kill:\n  heal = 10\n  refund = kills >= 2 and weapon == rocket\n";
    WsDiag d;
    auto s = compileWeaponSet(text, &d);
    CHECK(s != nullptr);
    if (!s) { printf("%s\n", weaponSetDiagJson(d).c_str()); return; }
    CHECK(weaponSetHasSlot(*s, WS_HIT, WPN_RIFLE) && weaponSetHasSlot(*s, WS_HIT, WPN_SNIPER) && weaponSetHasSlot(*s, WS_KILL, WPN_LASER));
    auto hit = [&](int w, float dmg, bool head, bool air, float dist, int streak) {
        WsCall c;
        c.in[WSS_WEAPON] = w; c.in[WSS_HEAD] = head; c.in[WSS_AIRBORNE] = air; c.in[WSS_DIST] = dist; c.in[WSS_STREAK] = streak;
        c.out[WSO_DAMAGE] = dmg; c.out[WSO_KNOCK] = 1; c.out[WSO_HEAL] = 0;
        runWeaponSlot(*s, WS_HIT, w, c);
        return c;
    };
    WsCall c = hit(WPN_RIFLE, 10, true, false, 40, 1);
    CHECK(c.out[WSO_DAMAGE] == 15 && c.out[WSO_KNOCK] == 1 && c.out[WSO_HEAL] == 0);
    c = hit(WPN_RIFLE, 10, true, false, 20, 1);    // too close for the bonus, on the ground: the knock ramp
    CHECK(c.out[WSO_DAMAGE] == 10 && c.out[WSO_KNOCK] == 2);
    c = hit(WPN_RIFLE, 10, false, true, 5, 4);     // airborne: x2; a streak of 4 heals 4
    CHECK(c.out[WSO_DAMAGE] == 20 && c.out[WSO_HEAL] == 4);
    c = hit(WPN_RIFLE, 10, false, false, 5, 1);
    CHECK(std::fabs(c.out[WSO_KNOCK] - 0.5) < 1e-6);
    c = hit(WPN_SNIPER, 40, true, true, 15, 1);    // the sniper's own slot instead of the general one
    CHECK(c.out[WSO_DAMAGE] == 42 && c.out[WSO_KNOCK] == 0.5);
    WsCall k;
    k.in[WSS_WEAPON] = WPN_ROCKET; k.in[WSS_KILLS] = 2;
    runWeaponSlot(*s, WS_KILL, WPN_ROCKET, k);
    CHECK(k.out[WSO_HEAL] == 10 && k.out[WSO_REFUND] == 1);
    k = WsCall(); k.in[WSS_WEAPON] = WPN_LASER; k.in[WSS_KILLS] = 5;
    runWeaponSlot(*s, WS_KILL, WPN_LASER, k);
    CHECK(k.out[WSO_HEAL] == 10 && k.out[WSO_REFUND] == 0);
    // f32 arithmetic rounds like C++ floats
    auto f = compileWeaponSet("version 1\nweapons \"f\"\nslot hit:\n  damage = damage * 0.1f + 0.2f\n", &d);
    CHECK(f != nullptr);
    if (f) {
        WsCall x; x.out[WSO_DAMAGE] = 3;
        runWeaponSlot(*f, WS_HIT, WPN_RIFLE, x);
        CHECK((float)x.out[WSO_DAMAGE] == 3.0f * 0.1f + 0.2f && x.out[WSO_DAMAGE] == (double)(3.0f * 0.1f + 0.2f));
    }
    // the cost of a call (the laser hits up to 1000 times a second per player)
    double t0 = timeNow(), sink = 0;
    const int n = 200000;
    for (int i = 0; i < n; i++) { WsCall x = hit(WPN_RIFLE, 10, (i & 1) != 0, (i & 2) != 0, (float)(i % 50), i % 5); sink += x.out[WSO_DAMAGE]; }
    double ns = (timeNow() - t0) / n * 1e9;
    printf("  hit slot: %.0f ns a call (%.0f)\n", ns, sink);
    CHECK(ns < 5000);
}

static void testWeaponSetRules() {
    printf("weapon sets: the custom mode's network and file forms\n");
    const std::string set = "version 1\nweapons \"net\"\nparam rocket.damage f32 = 40 # hot\n\nslot hit:\n  damage = damage * 2\n";
    CustomRules r;
    r.name = "wsrules"; r.weaponSetName = "netset"; r.weaponSetText = set;
    std::string net = r.toText();
    CHECK(net.find("\nweaponset=netset\n") != std::string::npos && net.find("\nwsline=param rocket.damage f32 = 40 # hot\nwsline=\nwsline=slot hit:\n") != std::string::npos);
    CustomRules back;
    CHECK(back.fromText(net) && back.weaponSetName == "netset" && back.weaponSetText == set && back.toText() == net);
    // a trailing \r on a line (fromText strips one): the round trip still gives the same text
    CustomRules cr;
    CHECK(cr.fromText("custommode=cr\nwsline=abc\r\r\n") && cr.weaponSetText == "abc\n");
    CustomRules cr2;
    CHECK(cr2.fromText(cr.toText()) && cr2.toText() == cr.toText());
    // lines from a gamemode logic block or anything else unknown don't disturb it
    CustomRules lg;
    CHECK(lg.fromText(net + "logic 1\nslot onKill:\n  x = 1\n") && lg.weaponSetText == set);
    // the file: only the name; loading reads custom_weapons/<name>.qweapons
    CHECK(saveWeaponSetFile("netset", set));
    CHECK(saveCustomMode(r));
    std::string file;
    { std::ifstream f(utf8Path(customModesDir() + "/wsrules.qmode")); std::stringstream ss; ss << f.rdbuf(); file = ss.str(); }
    CHECK(file.find("weaponset=netset") != std::string::npos && file.find("wsline") == std::string::npos);
    CustomRules loaded;
    CHECK(loadCustomMode("wsrules", loaded) && loaded.weaponSetName == "netset" && loaded.weaponSetText == set);
    auto names = listWeaponSets();
    CHECK(std::find(names.begin(), names.end(), "netset") != names.end());
    // a set that's missing or doesn't compile: the standard weapons (the name kept, so the editor saves it back)
    r.name = "wsmissing"; r.weaponSetName = "nosuchset"; r.weaponSetText.clear();
    CHECK(saveCustomMode(r));
    CHECK(loadCustomMode("wsmissing", loaded) && loaded.weaponSetName == "nosuchset" && loaded.weaponSetText.empty());
    CHECK(saveWeaponSetFile("broken", "version 1\nweapons \"b\"\nparam rocket.dmg f32 = 1\n"));
    r.name = "wsbroken"; r.weaponSetName = "broken"; r.weaponSetText.clear();
    CHECK(saveCustomMode(r));
    std::string why;
    CustomRules br;
    CHECK(br.fromText(r.toText()) && !resolveWeaponSet(br, &why) && why.find("3:1") != std::string::npos && br.weaponSetText.empty());
    // too big to send (EV_RULES: 255 chunks of 240 bytes): refused with a clear reason
    std::string big = "version 1\nweapons \"big\"\n";
    while (big.size() < kMaxWeaponSetBytes - 8) big += "#\n";
    CHECK(saveWeaponSetFile("big", big));
    CustomRules bg;
    bg.name = "wsbig"; bg.weaponSetName = "big";
    why.clear();
    CHECK(!resolveWeaponSet(bg, &why) && why.find("too big to send") != std::string::npos && bg.weaponSetText.empty());
    CHECK(bg.toText().size() <= kMaxRulesTextBytes);
    // ...while the standard set with every field written goes (a copy of it is where an edited set starts)
    CHECK(saveWeaponSetFile("full", standardWeaponSetText()));
    CustomRules fl;
    fl.name = "wsfull"; fl.weaponSetName = "full";
    why.clear();
    CHECK(resolveWeaponSet(fl, &why) && why.empty() && fl.weaponSetText == standardWeaponSetText());
    printf("  the standard set as rules: %zu bytes, %zu EV_RULES chunks (at most %zu)\n", fl.toText().size(), (fl.toText().size() + 239) / 240, kMaxRulesTextBytes / 240);
    setActiveRules(CustomRules());
}

// Through the real Server: a set changing the sniper's damage and headshots, the rocket's direct hit, the rifle's
// falloff, a hit slot doubling airborne damage and a kill slot healing the killer. The client gets the set with the
// rules (EV_RULES) and compiles the same one.
static void testWeaponSetServer(uint16_t port) {
    printf("weapon sets: in play\n");
    const std::string set =
        "version 1\nweapons \"play\"\nlabel sniper \"LONG GUN\"\n"
        "param sniper.damage f32 = 30\nparam sniper.headDamage f32 = 90\nparam rocket.directDamage f32 = 70\n"
        "param rifle.falloffStart f32 = 5\nparam rifle.falloffEnd f32 = 15\nparam rifle.falloffMin f32 = 0.5\n"
        "slot hit sniper:\n  if airborne: damage = damage * 2\n"
        "slot kill:\n  heal = 25\n";
    Server s;
    ServerConfig cfg;
    cfg.map = "bastion"; cfg.mode = MODE_FFA; cfg.port = port;
    cfg.custom.name = "wsplay"; cfg.custom.baseMode = MODE_FFA; cfg.custom.weaponSetName = "play"; cfg.custom.weaponSetText = set;
    NetClient c[2];
    std::vector<NetClient*> cls;
    if (!rulesStart(s, cfg, c, 2, cls, false)) { s.stop(); setActiveRules(CustomRules()); return; }
    std::vector<std::string> parts;
    auto collect = [&] { for (auto& e : c[0].takeEvents()) if (e.type == EV_RULES && e.b > 0 && e.a < e.b) { parts.resize(e.b); parts[e.a] = e.text; } };
    pump(s, cls, 2.0, idle);
    collect();
    CHECK(weaponDef(WPN_SNIPER).damage == 30 && headDamage(WPN_SNIPER) == 90 && std::string(weaponDef(WPN_SNIPER).name) == "LONG GUN" && activeWeaponLogic() != nullptr);
    {   // what a client gets: the same set
        std::string all;
        for (auto& p : parts) all += p;
        CustomRules got;
        CHECK(!parts.empty() && got.fromText(all) && got.weaponSetText == set && got.weaponSetName == "play");
        auto a = compileWeaponSet(got.weaponSetText), b = compileWeaponSet(set);
        CHECK(a && b && wsDiff(weaponSetValues(*a), weaponSetValues(*b)).empty());
    }
    auto& sa = s.players_[c[0].clientId()];
    auto& sb = s.players_[c[1].clientId()];
    struct Shot { const char* what; int weapon; float height; bool air; float hp; float expect; };
    const Shot shots[] = {
        {"sniper body", WPN_SNIPER, 1.0f, false, 100, 30},
        {"sniper head", WPN_SNIPER, 1.62f, false, 100, 90},
        {"sniper body, airborne (hit slot x2)", WPN_SNIPER, 1.0f, true, 100, 60},
        {"rocket direct hit", WPN_ROCKET, 1.0f, false, 100, 70},
    };
    for (const Shot& sh : shots) {
        Vec3 tpos(-10, sh.air ? 3.0f : 0.0f, 19);
        sa.move.pos = {10, 0, 19}; sb.move.pos = tpos; sa.move.vel = sb.move.vel = {};
        auto aim = [&](int i, uint16_t b) {
            UserCmd u; if (i != 0) return u;
            Vec3 eye = sa.move.pos + Vec3(0, kEyeHeight, 0), tgt = tpos + Vec3(0, sh.height, 0);
            Vec3 dd = normalize(tgt - eye);
            u.pitch = std::asin(dd.y); u.yaw = std::atan2(-dd.x, -dd.z); u.weapon = (uint8_t)sh.weapon; u.buttons = b;
            return u;
        };
        auto hold = [&](int i, uint16_t b) { sb.move.pos = tpos; sb.move.vel = {}; sa.move.pos = {10, 0, 19}; sa.move.vel = {}; return aim(i, b); };
        pump(s, cls, 1.6, [&](int i) { return hold(i, 0); });   // (switched, the last shot's cooldown over)
        sb.health = sh.hp;
        int d0 = sb.deaths;
        pump(s, cls, 0.25, [&](int i) { return hold(i, BTN_FIRE); });
        pump(s, cls, 0.8, [&](int i) { return hold(i, 0); });   // (a rocket flies)
        float dealt = sb.deaths > d0 ? sh.hp : sh.hp - sb.health;
        printf("  %s: %.2f damage (expected %.0f)\n", sh.what, dealt, sh.expect);
        CHECK(std::fabs(dealt - sh.expect) < 0.01f);
    }
    {   // the rifle at 20 m with falloff 5..15 m to x0.5: 4.95 x 0.5 a bullet
        sa.move.pos = {10, 0, 19}; sb.move.pos = {-10, 0, 19};
        Vec3 tpos(-10, 0, 19);
        auto hold = [&](int i, uint16_t b) {
            sb.move.pos = tpos; sb.move.vel = {}; sa.move.pos = {10, 0, 19}; sa.move.vel = {};
            UserCmd u; if (i != 0) return u;
            Vec3 eye = sa.move.pos + Vec3(0, kEyeHeight, 0), tgt = tpos + Vec3(0, 1.0f, 0);
            Vec3 dd = normalize(tgt - eye);
            u.pitch = std::asin(dd.y); u.yaw = std::atan2(-dd.x, -dd.z); u.weapon = WPN_RIFLE; u.buttons = b;
            return u;
        };
        pump(s, cls, 0.5, [&](int i) { return hold(i, 0); });
        sb.health = 100;
        float hp0 = sb.health;
        pump(s, cls, 0.25, [&](int i) { return hold(i, BTN_FIRE); });
        float dealt = hp0 - sb.health, per = baseWeaponDef(WPN_RIFLE).damage * 0.5f, n = dealt / per;
        printf("  rifle at 20 m with falloff: %.3f damage (%.2f bullets of %.3f)\n", dealt, n, per);
        CHECK(dealt > 0 && std::fabs(n - std::round(n)) < 0.02f);
    }
    {   // the kill slot: the killer gets 25 hp
        Vec3 tpos(-10, 0, 19);
        auto hold = [&](int i, uint16_t b) {
            sb.move.pos = tpos; sb.move.vel = {}; sa.move.pos = {10, 0, 19}; sa.move.vel = {};
            UserCmd u; if (i != 0) return u;
            Vec3 eye = sa.move.pos + Vec3(0, kEyeHeight, 0), tgt = tpos + Vec3(0, 1.0f, 0);
            Vec3 dd = normalize(tgt - eye);
            u.pitch = std::asin(dd.y); u.yaw = std::atan2(-dd.x, -dd.z); u.weapon = WPN_SNIPER; u.buttons = b;
            return u;
        };
        pump(s, cls, 1.6, [&](int i) { return hold(i, 0); });
        sb.health = 20; sa.health = 50;
        int d0 = sb.deaths;
        pump(s, cls, 0.25, [&](int i) { return hold(i, BTN_FIRE); });
        printf("  kill slot: the killer has %.0f hp (50 + 25)\n", sa.health);
        CHECK(sb.deaths == d0 + 1 && std::fabs(sa.health - 75) < 0.01f);
    }
    s.stop();
    setActiveRules(CustomRules());
}

static void testWeaponSets(uint16_t port) {
    testWeaponSetDefault();
    testWeaponSetGrammar();
    testWeaponSetLogic();
    testWeaponSetRules();
    testWeaponSetServer(port);
}

// quark_tests exportweapons [out.json] / checkweapons <file> / weaponsast <file> / printweapons <file>
static int weaponTool(const std::string& cmd, const char* arg) {
    if (cmd == "exportweapons") {
        std::string j = exportWeaponsJson();
        if (!arg) { fputs(j.c_str(), stdout); fputs("\n", stdout); return 0; }
        std::ofstream o(arg, std::ios::binary);
        o << j;
        printf("wrote %s (%zu bytes)\n", arg, j.size());
        return o ? 0 : 1;
    }
    std::string text;
    {
        std::ifstream f(arg ? arg : "", std::ios::binary);
        if (!arg || !f) { printf("{\"ok\":false,\"errors\":[{\"line\":0,\"col\":0,\"msg\":\"can't read the file\"}],\"warnings\":[]}\n"); return 1; }
        std::stringstream ss; ss << f.rdbuf(); text = ss.str();
    }
    WsDiag d;
    if (cmd == "checkweapons") {
        auto s = compileWeaponSet(text, &d);
        printf("%s\n", weaponSetDiagJson(d).c_str());
        return s ? 0 : 1;
    }
    std::string out = cmd == "weaponsast" ? weaponSetAstJson(text, &d) : printWeaponSetText(text, &d);
    if (out.empty()) { printf("%s\n", weaponSetDiagJson(d).c_str()); return 1; }
    fputs(out.c_str(), stdout);
    if (cmd == "weaponsast") fputs("\n", stdout);
    return 0;
}

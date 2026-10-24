// The MOCK's test room and presets: the page's stand-ins for `quark_maptool physroom` / `physpresets` (INTERFACE.md),
// used only when the helper isn't there. The room is .qmap text (the game's format, read with qmap.js) so the real
// room and this one are drawn and simulated the same way. Lanes side by side along z, everything faced along +x
// (yaw -90), like the helper's room. Parts (each preset says which: "room:<part>"):
//   floor   z -60..60: a 400 m floor (the open lane: start -120 0 0)
//   wall    z 90..110: a wall at x 20 (dash into it)
//   ladder  z -110..-90: a 10 m tower at x 20 with a ladder on its -x face
//   gap     z 140..146: a 1 m runway, then gaps of 2, 3, 4, 5, 6 and 7 m
//   stairs  z -146..-140: ten 0.5 m steps up to a 5 m platform
//   ceiling z 160..190: a grapple ceiling 14 m up
//   tower   z -180..-170: a 20 m tower (falls)

export const ROOM_PARTS = ["floor", "wall", "ladder", "gap", "stairs", "ceiling", "tower"];

export function mockRoomText() {
    const L = ["version 2", "title Physics test room (page MOCK)", "for ffa", "handmade"];
    const f = (v) => +v.toFixed(2);
    const box = (x0, y0, z0, x1, y1, z1, mat = "box") => L.push(`${mat} ${f(x0)} ${f(y0)} ${f(z0)} ${f(x1)} ${f(y1)} ${f(z1)}`);
    box(-160, -1, -280, 300, 0, 280);                         // floor
    box(20, 0, 90, 22, 8, 110, "accent");                     // wall
    box(20, 0, -104, 24, 10, -96);                            // ladder tower
    L.push("ladder 19.6 0 -101.2 20 10 -98.8");
    box(-20, 0, 140, 0, 1, 146, "metal");                     // gap runway, then platforms after gaps 2..7
    let x = 0;
    for (const g of [2, 3, 4, 5, 6, 7]) { x += g; box(x, 0, 140, x + 3, 1, 146, "metal"); x += 3; }
    for (let k = 0; k < 10; k++) box(10 + 0.6 * k, 0, -146, 10 + 0.6 * (k + 1), 0.5 * (k + 1), -140);   // stairs
    box(16, 0, -146, 26, 5, -140);
    box(-20, 14, 160, 40, 15, 190, "accent");                 // grapple ceiling
    box(60, 0, -180, 70, 20, -170);                           // tower
    L.push("spawn ffa -120 0 0 -90");
    return L.join("\n") + "\n";
}

// A scenario (INTERFACE.md): {name, start:{pos, yaw, pitch, vel?, onGround?, dashCharges?}, durationMs, steps:[{atMs,
// forward, side, yaw?, pitch?, yawRate?, jump?, crouch?, dash?, hook?, strafeBot?}]}. Angles in degrees; yaw 0 looks
// down -z, 90 looks down -x, -90 down +x, 180 down +z (the game's: forward = (-sin yaw, 0, -cos yaw)); pitch > 0 up.
const S = (name, start, durationMs, steps) => ({ name, start: { onGround: true, pitch: 0, ...start }, durationMs, steps });
const LANE = { pos: [-120, 0, 0], yaw: -90 };

export function mockPresets() {
    const hop = [{ atMs: 0, forward: 127, side: 0 }, { atMs: 400, forward: 0, side: 127, jump: true, strafeBot: true, strafeSwitch: true },
        { atMs: 400 + 6700, forward: 0, side: 0 }];
    return [
        { id: "standing_jump", label: "Standing jump", desc: "jump from a standstill: height and air time", map: "room:floor",
          scenario: S("Standing jump", LANE, 1200, [{ atMs: 0, forward: 0, side: 0, jump: true }, { atMs: 30, forward: 0, side: 0 }]) },
        { id: "running_jump", label: "Running jump", desc: "run 0.6 s, jump: distance, landing speed", map: "room:floor",
          scenario: S("Running jump", LANE, 1800, [{ atMs: 0, forward: 127, side: 0 }, { atMs: 600, forward: 127, side: 0, jump: true }, { atMs: 630, forward: 127, side: 0 }]) },
        { id: "run_up", label: "Run-up to top speed", desc: "hold W: time to top speed", map: "room:floor",
          scenario: S("Run-up", LANE, 1500, [{ atMs: 0, forward: 127, side: 0 }]) },
        { id: "stop", label: "Stop from top speed", desc: "run 1 s, let go: stop distance and time", map: "room:floor",
          scenario: S("Stop", LANE, 2500, [{ atMs: 0, forward: 127, side: 0 }, { atMs: 1000, forward: 0, side: 0 }]) },
        { id: "bhop10", label: "Bhop chain x10 (strafe bot)", desc: "run-up, then ten hops strafing A / D at the best angle", map: "room:floor",
          scenario: S("Bhop x10", LANE, 8000, hop) },
        { id: "dash", label: "Dash", desc: "one dash from a standstill", map: "room:floor",
          scenario: S("Dash", LANE, 1200, [{ atMs: 0, forward: 0, side: 0 }, { atMs: 100, forward: 0, side: 0, dash: true }, { atMs: 130, forward: 0, side: 0 }]) },
        { id: "dash_jump", label: "Dash jump", desc: "dash on the floor, jump during it", map: "room:floor",
          scenario: S("Dash jump", LANE, 1600, [{ atMs: 0, forward: 127, side: 0 }, { atMs: 300, forward: 127, side: 0, dash: true }, { atMs: 340, forward: 127, side: 0, jump: true }, { atMs: 370, forward: 127, side: 0 }]) },
        { id: "super_dash", label: "Super dash", desc: "run, dash aimed 40 deg down into the floor with jump", map: "room:floor",
          scenario: S("Super dash", LANE, 1800, [{ atMs: 0, forward: 127, side: 0 }, { atMs: 400, forward: 127, side: 0, pitch: -40, dash: true, jump: true }, { atMs: 440, forward: 127, side: 0, pitch: 0 }]) },
        { id: "dash_wall", label: "Dash into wall (bounce)", desc: "dash head-on into the wall", map: "room:wall",
          scenario: S("Dash into wall", { pos: [16, 0, 100], yaw: -90 }, 1500, [{ atMs: 0, forward: 127, side: 0 }, { atMs: 200, forward: 127, side: 0, dash: true }, { atMs: 230, forward: 127, side: 0 }]) },
        { id: "grapple", label: "Grapple swing", desc: "run, jump, hook the ceiling 50 deg up, swing, let go", map: "room:ceiling",
          scenario: S("Grapple swing", { pos: [-24, 0, 175], yaw: -90 }, 3200, [{ atMs: 0, forward: 127, side: 0 }, { atMs: 200, forward: 127, side: 0, jump: true, pitch: 50 }, { atMs: 260, forward: 127, side: 0, hook: true }, { atMs: 1800, forward: 127, side: 0, pitch: 0 }]) },
        { id: "ladder", label: "Ladder climb", desc: "walk into the ladder and climb 10 m", map: "room:ladder",
          scenario: S("Ladder", { pos: [16, 0, -100], yaw: -90 }, 2600, [{ atMs: 0, forward: 127, side: 0 }]) },
        { id: "stairs", label: "Step-up stairs", desc: "walk up ten 0.5 m steps", map: "room:stairs",
          scenario: S("Stairs", { pos: [2, 0, -143], yaw: -90 }, 3000, [{ atMs: 0, forward: 127, side: 0 }]) },
        { id: "crouch", label: "Crouch walk", desc: "hold W and crouch (the game ignores crouch since 1.17)", map: "room:floor",
          scenario: S("Crouch walk", LANE, 1500, [{ atMs: 0, forward: 127, side: 0, crouch: true }]) },
        { id: "max_gap", label: "Max gap", desc: "a running jump off the 1 m runway's edge", map: "room:gap",
          scenario: S("Max gap", { pos: [-3.5, 1, 143], yaw: -90 }, 1800, [{ atMs: 0, forward: 127, side: 0 }, { atMs: 380, forward: 127, side: 0, jump: true }, { atMs: 410, forward: 127, side: 0 }]) },
        { id: "fall", label: "Fall (20 m)", desc: "walk off the 20 m tower", map: "room:tower",
          scenario: S("Fall", { pos: [65, 20, -175], yaw: -90 }, 3000, [{ atMs: 0, forward: 127, side: 0 }, { atMs: 500, forward: 0, side: 0 }]) },
    ];
}

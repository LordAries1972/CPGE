// ================================================================================================
//  VectorShapes.h  -  reusable 3D mesh types, shape builders and procedural-world helpers
//
//  Everything here is renderer-agnostic: a Mesh is just a vertex list plus convex, flat-shaded faces with outward normals.  A project
//  builds its meshes ONCE at start-up with the Build...Mesh functions and then draws them with whatever transform / rasteriser it
//  has (the Vectors demo in main.cpp uses SubmitMesh).  Nothing in this file touches the renderer, the window or any game state.
//
//  Dependencies: Vectors.h (MyVector), Color.h (MyColor), MathPrecalculation.h (FAST_UNIT_CIRCLE, used by the .cpp only).
//
//  Layout
//    Types         Face, Mesh, Creature / CreatureSet, PlanetPalette, FleetId, Shade()
//    Builders      grouped below: tools, colour/noise helpers, primitives, spacecraft, characters, planets
//  Usage
//        using namespace MyShapes;            // or qualify with MyShapes::
//        Mesh cube = BuildCubeMesh();         // unit cube
//        Mesh rock = BuildRockMesh(1, 0xA11CE5u, 0.34f);
//        Mesh fleet[kFlCount];  BuildFleet(fleet);
//  Every builder returns a finished mesh (call FinalizeMesh yourself only for meshes you assemble by hand with AddFaceAuto & friends).
// ================================================================================================
#pragma once

#include "Includes.h"                    // global platform / renderer definitions (must come first)
#include "Vectors.h"
#include "Color.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <initializer_list>
#include <string>
#include <vector>

namespace MyShapes
{
    constexpr int kArrowSegments = 8;                     // sides of the arrow shaft / head (a precomputed unit circle)

    // ---- meshes: vertex list + convex faces with outward normals ------------------------------------------------------------------
    struct Face
    {
        std::vector<int> idx;                         // vertex indices of a convex polygon
        MyVector normal;                              // outward unit normal
        float    tone = 1.0f;                         // multiplies the instance colour: lets one mesh have light and dark panels
        bool     fixedColor = false;                  // true: paint with `color` instead of the instance colour (glass, metal, flames)
        MyColor  color;
        bool     glow = false;                        // self-lit: ignores the light direction (engine flames, nozzles)
        float    depthBias = 0.0f;                    // metres: the face sorts this much NEARER than its own centre says - for decals (windows, lamps, plates) lying on a bigger face, so the painter's sort draws them after it
    };
    struct Mesh { std::vector<MyVector> verts; std::vector<Face> faces; float radius = 0.0f; };   // radius: bounding sphere about the local origin

    // Scales a colour's brightness by k (clamped to 0..255 per channel); alpha is kept.  Inline: it is called per face / per HUD element.
    inline MyColor Shade(const MyColor& c, float k)
    {
        auto clamp255 = [](float v) { return static_cast<uint8_t>(v < 0.0f ? 0.0f : (v > 255.0f ? 255.0f : v)); };
        return MyColor(clamp255(c.r * k), clamp255(c.g * k), clamp255(c.b * k), c.a);
    }

    // ---- the spacecraft built by BuildFleet (indices into the fleet array) -----------------------------------------------------------
    // The last twelve are the "specialist" attackers (each has its own weapon: laser beam, shock-wave, mines, homing mines, missiles, micro-missile swarm, railgun,
    // flak, burst plasma, torpedo), the player's detailed Spartan fighter and the proximity mine.
    enum FleetId { kFlDart = 0, kFlManta, kFlLancer, kFlScarab, kFlWasp, kFlCruiser, kFlCarrier, kFlDread, kFlDrone, kFlFalcon, kFlPhantom, kFlSaber, kFlHornet, kFlRaven, kFlStarhawk,
                   kFlLasher, kFlPulsar, kFlMiner, kFlSeeder, kFlHunter, kFlSwarmer, kFlRailer, kFlFlakker, kFlSpitter, kFlTorpedo, kFlSpartan, kFlMine,
                   // the second fleet (one per new firing technique; the last four carry several): see BuildFleet
                   kFlVortex, kFlNova, kFlGatling, kFlSerpent, kFlSower, kFlReaper, kFlTrident, kFlCrossfire, kFlAegis, kFlTempest, kFlWarden, kFlSpectre,
                   // the third fleet (ten new firing techniques; the last six carry several)
                   kFlBlossom, kFlHelix, kFlWarper, kFlLongbow, kFlMauler, kFlMonsoon, kFlMatrix, kFlSonar, kFlStarburst, kFlHailer, kFlMarauder, kFlOracle, kFlLeviathan, kFlPhoenix, kFlColossus, kFlOverlord,
                   // the fourth fleet (twelve more firing patterns) and six BOSS craft
                   kFlBanshee, kFlRipper, kFlWidow, kFlComet, kFlLancet, kFlGorgon, kFlMantis, kFlCinder, kFlGlaive, kFlEclipse, kFlRevenant, kFlTalon,
                   kFlBehemoth, kFlHydra, kFlJuggernaut, kFlSeraph, kFlKraken, kFlAnnihilator, kFlCount };

    // ---- articulated creatures: a body mesh plus separately animated limbs that hinge at their own origin -----------------------------
    struct Creature
    {
        Mesh body, armL, armR, legL, legR, wingL, wingR;
        float armY = 0.8f, armZ = 0.3f, legY = 0.4f, legZ = 0.1f, wingX = -0.08f, wingY = 0.85f, wingZ = 0.1f;   // hinge positions (model units)
        float armSwing = 30.0f, legSwing = 30.0f, armRest = 0.0f, rate = 9.0f, bob = 0.04f, flap = 0.0f, hover = 0.0f;   // animation: swing degrees, rest angle, walk rate, bob height, wing flap degrees, hover height
    };
    // The first seven are the original dungeon monsters; the second batch (troll ... cyclops) are the extra Gauntlet / Dungeon kinds.
    struct CreatureSet { Creature grunt, ghost, demon, death, skel, spider, sorc,
                         troll, bat, archer, slime, slimelet, golem, werewolf, necro, imp, cyclops; };       // filled by BuildCreatures

    // ---- procedural planets ----------------------------------------------------------------------------------------------------------
    struct PlanetPalette { MyColor deep, shallow, beach, low, forest, desert, rock, snow, ice, halo, cloud; const wchar_t* name; };
    extern const PlanetPalette kPlanetPal[7];                                    // earth, mars, ice, jungle, volcanic, golden, ocean
    // Per-face terrain numbers of one procedural planet (height, moisture, |latitude|, fine detail), kept so the planet can be repainted with another palette (PaintPlanet).
    // Each BuildPlanetMesh call fills the PlanetTerrain it is given, so several detail levels of the planet can coexist.
    struct PlanetTerrain { std::vector<float> h, m, lat, d; };

    // ----------------------------------------------------------------------------------------------------
    //  MESH-BUILDING TOOLS
    // ----------------------------------------------------------------------------------------------------
    // Computes and stores the bounding-sphere radius (about the local origin) that the renderer uses for culling and level of detail. Call once after
    // a mesh is complete.
    void FinalizeMesh(Mesh& m);

    // Adds one convex face. The normal comes from the first three vertices and is flipped to point away from `interior`, so builders never need
    // hand-written normals. tone darkens/lightens the face; fixedColor paints it with `color` instead of the instance colour; glow makes it self-lit.
    void AddFaceAuto(Mesh& m, std::initializer_list<int> idx, const MyVector& interior, float tone = 1.0f, bool fixedColor = false, const MyColor& color = MyColor(), bool glow = false);

    // Same as AddFaceAuto but takes the vertex indices as a std::vector (for faces whose size is only known at run time).
    void AddFaceVec(Mesh& m, const std::vector<int>& idx, const MyVector& interior, float tone = 1.0f, bool fixedColor = false, const MyColor& color = MyColor(), bool glow = false);

    // Adds an axis-aligned box centred on c with half-sizes h (8 new vertices, 6 faces). Used for the hand-built characters and voxel-like props.
    void AddBoxFaces(Mesh& m, const MyVector& c, const MyVector& h, float tone, bool fixedColor, const MyColor& color);

    // Marks the last `count` faces of a mesh as self-lit paint (engine glow, lamps, eyes) with the given colour.
    void MarkGlow(Mesh& m, size_t count, const MyColor& col);

    // Shared icosphere generator: an icosahedron, optionally subdivided (each triangle becomes four; 0 = 12 vertices, 1 = 42, 2 = 162, 3 = 642, 4 =
    // 2562). Output is unit radius vertices plus triangle index triples.
    void BuildIcoSphere(int subdiv, std::vector<MyVector>& v, std::vector<std::array<int, 3>>& tris);

    // Adds a square-section bar from point a to point b with radius ra at one end and rb at the other (limbs, horns, claws, tails, blades - any
    // diagonal). Faces are painted `col`.
    void AddBar(Mesh& m, const MyVector& a, const MyVector& b, float ra, float rb, const MyColor& col, float tone = 1.0f);

    // Adds a box that narrows or widens along Y: the bottom rectangle at c0 (half sizes hx0, hz0) to the top rectangle at c1 (hx1, hz1). Torsos,
    // hips, necks, sleeves.
    void AddTaper(Mesh& m, const MyVector& c0, float hx0, float hz0, const MyVector& c1, float hx1, float hz1, const MyColor& col, float tone = 1.0f);

    // Adds a thin polygon visible from both sides (wing membrane): the same outline twice with opposite normals.
    void AddMembrane(Mesh& m, std::initializer_list<MyVector> pts, const MyColor& col, float tone);

    // Like AddBar but each face may take the instance colour (fixed = false) or its own paint (fixed = true). Used for the spacecraft hulls.
    void FleetBar(Mesh& m, const MyVector& a, const MyVector& b, float ra, float rb, const MyColor& col, bool fixed, float tone = 1.0f);

    // A flat double-sided wing / fin panel with a convex outline of 3-5 points; fixed selects own paint or instance colour.
    void FleetPanel(Mesh& m, std::initializer_list<MyVector> pts, const MyColor& col, bool fixed, float tone = 1.0f);

    // A tapering slab along Y (see AddTaper) with a choice of own paint or instance colour.
    void FleetSlab(Mesh& m, const MyVector& c0, float hx0, float hz0, const MyVector& c1, float hx1, float hz1, const MyColor& col, bool fixed, float tone = 1.0f);


    // ----------------------------------------------------------------------------------------------------
    //  COLOUR AND NOISE HELPERS
    // ----------------------------------------------------------------------------------------------------
    // Integer lattice hash returning 0..1 (the basis of the value noise).
    float Hash3(int x, int y, int z);

    // Smooth 3D lattice value noise (0..1), trilinear with smoothstep weights.
    float ValueNoise3(const MyVector& p);

    // Four octaves of value noise summed (fractal Brownian motion), roughly 0..0.94. Terrain heights, moisture and cloud masks.
    float Fbm3(const MyVector& p);

    // Blends colour a toward b by t (clamped to 0..1); the result is opaque.
    MyColor LerpColor(const MyColor& a, const MyColor& b, float t);

    // The colour of one patch of a procedural world from palette P: h = height (sea level 0.48), mo = moisture, lat = |latitude|, d = fine detail
    // noise. Every transition is a smooth blend; the result is quantised so faces of one colour batch together.
    MyColor PlanetPaint(const PlanetPalette& P, float h, float mo, float lat, float d);


    // ----------------------------------------------------------------------------------------------------
    //  PRIMITIVES (cube, octahedron, disc, ball, rock, arrow, voxels)
    // ----------------------------------------------------------------------------------------------------
    // Unit arrow along +X: tail cap, shaft, a step ring and a cone head with the tip at x = 1 (ring points come from the precalculated unit circle).
    Mesh BuildArrowMesh();

    // Unit cube centred on the origin (8 vertices, 6 quads).
    Mesh BuildCubeMesh();

    // Octahedron with 6 vertices on the axes at +/-0.5 and 8 triangular faces.
    Mesh BuildOctahedronMesh();

    // A weathered, lumpy rock: icosphere vertices pushed in/out at random (seeded, so repeatable), squashed per axis, every face with its own tone.
    // Nominal radius about 1.
    Mesh BuildRockMesh(int subdiv, unsigned seed, float lump);

    // A realistic cratered asteroid (Planet Assault): elongated and flattened, fractal-noise lumps, bowl-and-rim impact craters, face tones that follow the ground height.
    // subdiv 2 = 320 faces, 1 = 80, 0 = 20.  Nominal radius about 1.
    Mesh BuildAsteroidMesh(int subdiv, unsigned seed);

    // Voxel mesh from an ASCII picture. rows[0] is the top row; '.' empty, '#' instance colour, a/r/y/w/k/s/b = fixed colours. `depth` layers are
    // extruded in Z. Only faces that touch empty space are generated.
    Mesh BuildVoxelMesh(const std::vector<std::string>& rows, int depth);

    // Unit floor disc in the XZ plane (a 32-gon), for round pads and shadows.
    Mesh BuildDiscMesh();

    // A plain 80-face self-lit sphere for fireballs, smoke and flak puffs.
    Mesh BuildPuffMesh();

    // A smooth ball (162 vertices, 320 faces); the instance colour paints it.
    Mesh BuildBallMesh();


    // ----------------------------------------------------------------------------------------------------
    //  SPACECRAFT, VEHICLES AND HARDWARE
    // ----------------------------------------------------------------------------------------------------
    // A proper little fighter, nose along +X, wings along +/-Z, up = +Y, about 1 unit long: tapered 6-sided fuselage, glass canopy, swept wings, twin
    // tail fins and two engine nozzles with glowing exhaust. Hull panels take the instance (team) colour; glass, metal and glow are fixed.
    Mesh BuildShipMesh();

    // Exhaust flame: a self-lit four-sided spike with its base at x = 0 and the tip trailing along -X.
    Mesh BuildFlameMesh();

    // Ground-launched rocket, nose along +X, about 1 unit long: octagonal body with a dark band, white nose cone, four swept tail fins and an engine
    // nozzle.
    Mesh BuildRocketMesh();

    // Flying saucer (Defender lander, Scramble UFO), up = +Y, about 1 unit across: layered hull, ring of rim lights, glass dome and three landing
    // legs.
    Mesh BuildSaucerMesh();

    // Hostile raider: forward-swept angular wings, a long dart fuselage, a glowing red cockpit eye and twin glowing engines. Nose along +X, about 1
    // unit long.
    Mesh BuildRaiderMesh();

    // Heavy warship / command ship: armoured block hull, pointed prow, raised bridge, two side pods and three big engines. Nose along +X, about 1
    // unit long.
    Mesh BuildWarshipMesh();

    // Aerodynamic bomb, nose along +X, about 1 unit long: rounded body, yellow band, pointed nose and four tail fins.
    Mesh BuildBombMesh();

    // A classic round bomb for Jumpman: a ten-sided ball (about 1 unit across, centred on the origin) with a steel collar, a bent fuse and a glint of light.  The body takes the instance
    // colour; the game draws a flickering spark at the fuse tip (about (0.34, 0.9, 0)).
    Mesh BuildRoundBombMesh();
    Mesh BuildRocketLauncherMesh();                  // ground heat-seeker launcher (Project X): tracked chassis + 3-tube rack

    // Fuel tank: a vertical octagonal drum with a white band, domed top and steel base ring. y from -0.5 to +0.5.
    Mesh BuildFuelTankMesh();

    // Anti-air turret: wide octagonal base, neck, domed head and a long gun barrel along +X (y from -0.5 to +0.5). withGun = false omits the barrel.
    Mesh BuildTurretMesh(bool withGun = true);

    // TIE-style fighter: a spherical cockpit with a window facing +X and two long hexagonal solar panels joined by a strut (panels are vertical
    // plates at +/-Z).
    Mesh BuildTieMesh();

    // Builds the hand-modelled spacecraft of the shoot-'em-ups into fleet[0 .. kFlCount-1] (indices are FleetId). All point along +X, about
    // one unit long (the Railer is longer); hull panels take the instance colour.  Includes the specialist attackers, the player's Spartan and the mine.
    void BuildFleet(Mesh* fleet);
    // Where a fleet ship's engine nozzles are (local space, nose along +X): the nozzle's exit position, its radius and the length of the static glowing exhaust bar built into the mesh.
    // Recorded by BuildFleet; a renderer draws an animated, flickering flame at each so every thruster of every ship is alive (see SubmitFleetEngines in the demo).
    struct FleetEngine { MyVector pos; float r = 0.05f, len = 0.2f; };
    const std::vector<FleetEngine>& FleetEngines(int fleetId);                        // empty for ships without engines (the mine) or an out-of-range id
    // Mesh tools and a procedural ship generator (see VectorShapes.cpp): a rounded-edge twin of any mesh, the wing-tip / fin positions of a mesh, and 40 ship shapes (10 designs x 4 variants).
    Mesh BevelMesh(const Mesh& src, float inset = 0.10f, float lift = 0.010f);
    void FindStrobePoints(const Mesh& m, MyVector& port, MyVector& starboard, MyVector& top);
    void BuildProceduralShip(int design, int variant, unsigned seed, Mesh& out, std::vector<FleetEngine>& engines);

    // The hardware that builds up on the player's ship in Morpheous, one mesh per build stage (each holds BOTH sides).  Local space of the player's ship drawn at scale 1.4:
    // nose along +X, wings along +/-Z, up +Y; dimensions are in world units, so the meshes are drawn at scale 1 with the ship's rotation.
    enum MorphAddonId { kMaWingPods = 0, kMaTwinGuns, kMaNacelles, kMaSideGuns, kMaRearGuns, kMaMissiles, kMaNoseArmor, kMaLaser, kMaOuterWings, kMaDorsal, kMaDrone, kMaCount };
    void BuildMorphAddons(Mesh* addons);

    // A space station that turns about its vertical (Y) axis: armoured core drum with a mast and dish, a windowed habitat ring on four spokes, docking pylons with
    // beacons, two solar arrays and an engineering pod.  About 1 unit in radius; the hull takes the instance colour, the rest is fixed paint.
    Mesh BuildStarbaseMesh();
    // Saturn's rings alone (planet radii = 1): C, B and A rings with the Cassini division and the Encke gap, tilted; merge it with a Saturn globe for a picture of the planet.
    Mesh BuildSaturnRingsMesh();


    // ----------------------------------------------------------------------------------------------------
    //  CHARACTERS, CREATURES AND PROPS
    // ----------------------------------------------------------------------------------------------------
    // Mutant: a spiky alien urchin - a small core with six long spikes (made to tumble on every axis).
    Mesh BuildMutantMesh();
    // Mutant UFO (Defender's mutants): an aggressive, armoured, spiked and fanged saucer (radius about 0.8), drawn with BuildMutantUfoRingMesh - a ring of hooked teeth - spinning
    // the other way round it.  Not the same shape as BuildSaucerMesh (the friendly-looking lander).
    Mesh BuildMutantUfoMesh();
    Mesh BuildMutantUfoRingMesh();
    // Gyrospace drone: a small reconnaissance satellite.  A faceted probe body (lathed round the Z axis) with a glowing amber sensor lens on its -Z face (the face the player sees), a dark
    // equatorial band, a mast with a dish antenna on top, and two solar wings along +/-X, each a boom, a metal frame and three blue photovoltaic cells.  Wing tips at x = +/-1.24,
    // so it is about 2.5 across; scale it to taste.  Hull panels take the instance colour (steel for the outer drones, gold for the centre one); lens, wings and trim are fixed paint.
    Mesh BuildDroneMesh();

    // Humanoid (Defender's people): head, torso, legs and arms in one mesh. Origin at the centre, 1 unit tall, facing +X.
    Mesh BuildHumanMesh();

    // Pac-Man ghost: a rounded dome head, straight flanks, a wavy skirt with four scallops and big eyes (white with blue pupils) looking along +X. Feet at y = 0, 1 unit tall.
    // `frame` 0 / 1 are the two skirt positions (the scallops swap places: swap between them several times a second and the skirt wiggles like the arcade ghost);
    // `scared` gives the frightened face instead: two small pale dots and a zig-zag mouth (colour the body blue).
    Mesh BuildGhostMesh(int frame = 0, bool scared = false);

    // Just the eyes of a ghost (white with blue pupils, double-sided, facing +X): what is left of a ghost that has been eaten and is hurrying home. About 0.4 wide, centred at y = 0.68.
    Mesh BuildGhostEyesMesh();

    // Pac-Man 3D: one HALF of the sphere (top or bottom) with a dark flat inside; the game hinges the two halves apart to chomp.
    Mesh BuildPacHalfMesh(bool top);

    // Articulated person, part 1: head + torso (the torso takes the instance colour = shirt), 1 unit tall in total with the limbs, facing +X, left /
    // right = +/-Z.
    Mesh BuildHumBodyMesh();

    // Articulated person, part 2: one arm with its ORIGIN AT THE SHOULDER, hanging along -Y so the game can swing it about Z.
    Mesh BuildHumArmMesh();

    // Articulated person, part 3: one leg with its ORIGIN AT THE HIP, hanging along -Y so the game can swing it about Z.
    Mesh BuildHumLegMesh();

    // A gun barrel that pivots at its own origin (the game turns it about Z to track a target), about 0.9 units long.
    Mesh BuildGunMesh();

    // A glass marble: an 80-face icosphere painted in two alternating swirl bands (colours a and b) so its rolling is visible.
    Mesh BuildMarbleMesh(const MyColor& a, const MyColor& b);

    // The red spike roller: a ball with twelve spikes (the icosahedron corners pulled out).
    Mesh BuildSpikeBallMesh();

    // A patrol robot: treads, red torso, grey head with a glowing visor, antenna and arms. Faces +X, feet at y = 0.
    Mesh BuildRobotMesh();

    // A winged demon: red body, horns, bat wings and glowing eyes. Faces +X.
    Mesh BuildDemonMesh();

    // A monster generator: a heap of bones around a smouldering core.
    Mesh BuildGeneratorMesh();

    // A treasure chest (the Gauntlet gold pickup).
    Mesh BuildChestMesh();

    // DEATH: a tall dark human in a hooded robe with an empty black face, two glowing red eyes and a scythe. Faces +X, feet at y = 0.
    Mesh BuildDeathMesh();

    // Builds the seven articulated dungeon monsters (body + separately animated limbs) into `set`. Call once at start-up.
    void BuildCreatures(CreatureSet& set);


    // ----------------------------------------------------------------------------------------------------
    //  PLANETS
    // ----------------------------------------------------------------------------------------------------
    // Fbm3 with a chosen octave count, scaled to the same 0 .. 0.94 range (dense planets use 5-6 octaves).
    float Fbm3N(const MyVector& p, int octaves);

    // A procedural planet (radius 1): continents from low-frequency noise plus ridged mountain relief. subdiv 4 = 2562 vertices / 5120 faces, subdiv 5 = 10242 / 20480 (more
    // octaves and deeper relief). Fills `t` so the planet can be repainted with another palette (PaintPlanet).
    Mesh BuildPlanetMesh(int subdiv, PlanetTerrain& t);
    void PaintPlanet(Mesh& m, const PlanetTerrain& t, const PlanetPalette& P);

    // The real Earth: each face of a dense icosphere (subdiv 4 or 5) takes the colour sampled from the equirectangular RGB24 map file `mapFile` (512 x 256). Returns
    // false (and `m` is left empty) if the file is missing.
    bool BuildEarthMappedMesh(Mesh& m, const std::filesystem::path& mapFile, int subdiv);

    // Cloud layer: only the faces where a domain-warped noise field is high, translucent white, hovering at `radius` above the planet surface (radius 1).
    Mesh BuildCloudMesh(int subdiv = 4, float radius = 1.035f, float coverage = 0.58f);

    // Atmosphere: a faint shell at `radius`; its rim shows as a soft glow round the limb (default: faint blue).
    Mesh BuildHaloMesh(int subdiv = 4, float radius = 1.10f, const MyColor& col = MyColor(110, 165, 255, 34));

    // One planet from an equirectangular RGB24 map (128x64, 256x128 or 512x256; see the .cpp): kind 0 Pluto, 1 Neptune, 2 Uranus, 3 Saturn, 4 Jupiter, 5 Mars, 6 Earth;
    // subdiv 4 / 5 = 5120 / 20480 faces. Gas giants get turbulent bands and an oblate shape, rocky worlds relief and craters. Returns false if the map is missing.
    bool BuildMappedPlanetMesh(Mesh& m, const std::filesystem::path& mapFile, int subdiv, int kind);
}   // namespace MyShapes

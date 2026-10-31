// Project tuning only. The engine knows no launcher, specimen or test-scene constants.
export const range_config = {
    rocketSpeed: 30,
    rocketRadius: 0.12,
    rocketLifetime: 5,
    // Covers the maximum requested rate for the whole lifetime, even when shots miss.
    rocketPool: 64,
    rates: [2, 6, 12] as const,
    barrageShots: 60,
    damageRadius: 5.5,
    terrainRadius: 3.8,
    damage: 14,
    impulse: 45000,
    minSoilSamples: 8,
    maxSoilFragments: 96,
    soilFragmentCells: 2,
    soilDensity: 250,
    stressElastic: 1e8,
    stressFatal: 2e8,
    stressIterations: 500,
    stressInterval: 0.5,
    targets: [
        {x: -1.8, y: 4, z: 0.6}, {x: 2, y: 9, z: 0.6},
        {x: -11, y: 4, z: 1.1}, {x: 3, y: 0, z: 5},
        {x: -18, y: 1.7, z: 7}, {x: 11, y: 3, z: 1.6},
        {x: -4, y: 0, z: 4}, {x: -1, y: 14, z: 0.6},
    ],
};

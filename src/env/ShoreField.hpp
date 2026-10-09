// The shore pass of env/Fields (UpdateShore) on copied values, so it can run on a worker thread
// (setting ShoreWorker). Nothing here touches the game: a snapshot holds plain values copied from
// the grid on the main thread, the result holds plain values the main thread applies.
//
// Same calculation as Fields.cpp's UpdateShore, bit for bit (tests/shore checks it against the
// function itself): distance to the nearest submerged cell and that water's surface by a two-pass
// chamfer transform, then the same for hot ground. The sweeps depend on each other in order, so the
// whole pass runs on ONE thread; splitting rows between threads would change the answer.
#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

namespace wxl_livingazeroth::fields::shore
{
    /// Which grid a snapshot was copied from. A result applies only to the same grid.
    struct GridKey
    {
        uint64_t worldEpoch = 0;          // Fields bumps it on every reset (map change, teardown, reload)
        int      mapId = -1;
        int      firstI = 0, firstJ = 0;  // world cell at the grid's logical (0, 0)
        int      size = 0;                // cells per side
    };
    inline bool SameGrid(const GridKey& a, const GridKey& b)
    {
        return a.worldEpoch == b.worldEpoch && a.mapId == b.mapId && a.firstI == b.firstI && a.firstJ == b.firstJ &&
               a.size == b.size;
    }

    /// The model numbers UpdateShore uses, handed over by Fields so both paths share one definition.
    struct Params
    {
        float cell = 2.0f;
        float shoreNear = 2.0f, shoreFar = 6.0f, shoreLow = 0.5f, shoreHigh = 2.0f;
        float heatNear = 1.0f, heatFar = 12.0f, heatLow = 0.5f, heatHigh = 4.0f;
        float hotGroundShare = 0.5f;
    };

    /// The calling thread's floating-point modes (rounding, precision, denormals). The worker runs
    /// each job under the modes of the thread that copied it, so results match the game thread's.
    struct FpControl
    {
        unsigned x87 = 0, sse = 0;
        bool     valid = false;
    };
    FpControl CaptureFpControl();

    /// One logical cell's inputs (row-major from the grid's (0, 0); NOT the toroidal storage slot).
    struct Input
    {
        uint32_t stamp = 0;               // the cell's fill (Fields: Cell::fillStamp); 0 = no live cell here
        bool     submerged = false, liquidHot = false;
        float    z = 0.0f, waterZ = 0.0f;
        float    liquidMoisture = 1.0f, liquidTemp = 0.0f;
        float    hotShare = 0.0f, hotTemp = 0.0f;
        uint32_t liquid = 0;
    };

    /// One logical cell's result: what UpdateShore would have written there.
    struct Output
    {
        uint32_t stamp = 0;               // copied from the input: applies only to that very fill
        bool     water = false;           // a liquid in range (else only the "none" fields are written)
        bool     hot = false;             // hot ground in range (same)
        float    waterDist = -1.0f, aboveWater = 0.0f, shore = 0.0f, heat = 0.0f;
        uint32_t nearLiquid = 0;
        float    nearMoisture = 0.0f, nearTemp = 0.0f;
        bool     nearHot = false;
        float    groundHeat = 0.0f, groundHeatTemp = 0.0f, groundHeatDist = -1.0f;
    };

    struct Snapshot
    {
        GridKey            key;
        uint64_t           revision = 0;  // Fields' input revision when copied
        Params             params;
        FpControl          fp;
        std::vector<Input> cells;         // key.size * key.size
        std::vector<Output> spare;        // a result buffer to reuse (may be empty)
    };

    struct Result
    {
        GridKey             key;
        uint64_t            revision = 0;
        std::vector<Output> cells;
        unsigned            water = 0, hotGround = 0;
        double              computeMs = 0.0; // on the worker
        std::vector<Input>  inputs;          // the snapshot's buffer, back for the next copy (RunJob)
    };

    /// The calculation. Throws std::invalid_argument on a malformed snapshot or non-finite inputs
    /// (the caller then falls back to the original routine). Pure: touches only its arguments.
    Result Compute(const Snapshot& in);
    /// The same into `out`, reusing out.cells' memory. Of a cell without a liquid (hot ground) in
    /// range only the fields Apply then writes are set; the rest keeps whatever the buffer held.
    void ComputeInto(const Snapshot& in, Result& out);

    /// One worker job, on the worker thread: the calculation (or the test override) under the
    /// snapshot's floating-point modes, timed, into the snapshot's spare buffer; the snapshot's own
    /// buffer goes back with the result. The buffers travel between the threads with the job, so no
    /// pass allocates (fresh megabyte blocks cost page faults on the game thread).
    using ComputeFn = Result (*)(const Snapshot&);
    Result RunJob(Snapshot& in, ComputeFn compute);

    /// Tests only: what RunJob computes with when started by Fields (nullptr = Compute). Read when the
    /// worker starts, on the main thread.
    void      SetComputeForTests(ComputeFn compute);
    ComputeFn ComputeForTests();

    /// Keeps this module mapped for the rest of the process, so code a worker thread runs can never
    /// be unloaded under it. Called (once) before the first worker starts. True if pinned.
    bool PinModule();

    /// Writes a result into one cell exactly as UpdateShore does: the fields it writes in each case,
    /// and nothing else (what it leaves alone when no liquid / hot ground is in range stays as is).
    template <class Cell> void Apply(Cell& c, const Output& o)
    {
        c.waterDist = o.waterDist; c.shore = o.shore; c.heat = o.heat; c.nearLiquid = o.nearLiquid; c.nearHot = o.nearHot;
        if (o.water) { c.aboveWater = o.aboveWater; c.nearMoisture = o.nearMoisture; c.nearTemp = o.nearTemp; }
        c.groundHeat = o.groundHeat; c.groundHeatDist = o.groundHeatDist;
        if (o.hot) c.groundHeatTemp = o.groundHeatTemp;
    }
}

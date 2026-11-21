/*
 * ecosim_abi.h — the only contract between the native engine and its consumers.
 *
 * Rules (docs/superpowers/specs §10 and appendix A.1):
 *   - pure C: extern "C", __cdecl, no C++ types, no STL, no exceptions, no bool,
 *     no wide characters cross this line.
 *   - every struct starts with int32_t struct_size; the callee validates it and
 *     only reads fields it recognises, so the ABI may grow by appending.
 *   - existing field order and existing signatures never change. New exports and
 *     new trailing fields are normal evolution ("只增不改").
 *   - return codes: 0 == OK, negative == error, details via ecosim_last_error().
 *   - pointers returned by *_view are BORROWED: valid until the next mutating
 *     call. The managed side serialises every call behind one lock.
 */
#ifndef ECOSIM_ABI_H
#define ECOSIM_ABI_H

#include <stdint.h>

#if defined(_WIN32)
#  if defined(ECOSIM_IMPLEMENTING)
#    define ECOSIM_API __declspec(dllexport)
#  elif defined(ECOSIM_LINK_STATIC)
#    define ECOSIM_API  /* the same objects, linked in rather than loaded */
#  else
#    define ECOSIM_API __declspec(dllimport)
#  endif
#  define ECOSIM_CALL __cdecl
#else
#  define ECOSIM_API __attribute__((visibility("default")))
#  define ECOSIM_CALL
#endif

#ifdef __cplusplus
extern "C" {
#endif

#define ECOSIM_ABI_VERSION 1

/* Hard limits, mirrored on the managed side. */
#define ECOSIM_MAX_DIM 8192
#define ECOSIM_MAX_SPECIES 16

/* Return codes. */
enum {
  ECOSIM_OK = 0,
  ECOSIM_ERR_INVALID = -1,  /* bad argument / dimensions / range            */
  ECOSIM_ERR_SIZE = -2,      /* struct_size too small for this build         */
  ECOSIM_ERR_STATE = -3,     /* legal call, not available in this state/build */
  ECOSIM_ERR_RANGE = -4      /* operation would touch cells outside the grid  */
};

typedef enum EcosimMode {
  ECOSIM_MODE_BIRSURV = 0,   /* Conway-style: birth/survive masks only        */
  ECOSIM_MODE_ECOSYSTEM = 1  /* multi-species pipeline (milestone M2)         */
} EcosimMode;

typedef enum EcosimTopology {
  ECOSIM_TOPOLOGY_TORUS = 0,
  ECOSIM_TOPOLOGY_BOUNDED = 1
} EcosimTopology;

/* Ids for ecosim_abi_struct_size(), used to prove managed/native layout parity. */
typedef enum EcosimStructId {
  ECOSIM_STRUCT_CONFIG = 1,
  ECOSIM_STRUCT_GRID_VIEW = 2,
  ECOSIM_STRUCT_PALETTE = 3,
  ECOSIM_STRUCT_PAINT_OP = 4,
  ECOSIM_STRUCT_STATS = 5,
  ECOSIM_STRUCT_SEED = 6,
  ECOSIM_STRUCT_PARAMS = 7,
  ECOSIM_STRUCT_PARAM_DESC = 8
} EcosimStructId;

#pragma pack(push, 4)

typedef struct EcosimConfig {
  int32_t struct_size;
  int32_t w;
  int32_t h;
  int32_t mode;        /* EcosimMode     */
  int32_t topology;    /* EcosimTopology */
  int32_t threads;     /* <= 0 means "pick automatically" */
  uint8_t birth_mask[9];    /* index = live neighbour count */
  uint8_t survive_mask[9];
  uint64_t rng_seed;
} EcosimConfig;

typedef struct EcosimGridView {
  int32_t struct_size;
  int32_t w;
  int32_t h;
  int32_t row_pitch_cells;      /* == w in this build; kept for future padding */
  const uint8_t* species;       /* 0 = empty, else species table index         */
  const uint8_t* resource;      /* ambient nutrient / remains                  */
  const uint16_t* energy;
  const uint16_t* age;
} EcosimGridView;

typedef struct EcosimPalette {
  int32_t struct_size;
  uint32_t cell_bgra[ECOSIM_MAX_SPECIES];  /* indexed by species */
} EcosimPalette;

typedef struct EcosimPaintOp {
  int32_t struct_size;
  int32_t x0;   /* half-open: [x0, x1) */
  int32_t y0;
  int32_t x1;
  int32_t y1;
  int32_t species;
} EcosimPaintOp;

typedef struct EcosimStats {
  int32_t struct_size;
  uint64_t generation;
  int32_t alive_count;
  int32_t w;
  int32_t h;
  int32_t species_count[ECOSIM_MAX_SPECIES];
} EcosimStats;

typedef struct EcosimSeedConfig {
  int32_t struct_size;
  int32_t species;
  uint32_t density_permille;   /* 0..1000 */
  uint64_t seed;               /* 0 = fall back to EcosimConfig.rng_seed */
} EcosimSeedConfig;

/* Tunables for the ecosystem pipeline. Indexed by species id (slot 0 unused).
 * Scales are permille multipliers over the species table's base numbers, so
 * the table stays the single source of truth for what a species *is*. */
typedef struct EcosimParams {
  int32_t struct_size;
  int32_t light_permille;       /* chance an eligible empty cell sprouts a producer */
  int32_t fertility_permille;   /* chance a cell gains +1 resource this generation */
  int32_t metabolism_scale_permille[ECOSIM_MAX_SPECIES];
  int32_t repro_scale_permille[ECOSIM_MAX_SPECIES];
} EcosimParams;

/* One row of the parameter description table: everything a slider needs,
 * including the UTF-8 label the UI shows. Index order is part of the contract,
 * so append only. */
typedef struct EcosimParamDesc {
  int32_t struct_size;
  int32_t index;
  int32_t group;        /* 0 = global, 1 = per species */
  int32_t species;      /* 0 when global */
  int32_t kind;         /* 0 = permille 0..1000, 1 = scale 0..10000 */
  int32_t min_value;
  int32_t max_value;
  int32_t default_value;
  const char* name;     /* static UTF-8 */
  const char* label;    /* static UTF-8 */
} EcosimParamDesc;

#pragma pack(pop)

typedef struct EcosimWorld EcosimWorld;

/* --- meta ------------------------------------------------------------- */
ECOSIM_API int32_t ECOSIM_CALL ecosim_abi_version(void);
ECOSIM_API int32_t ECOSIM_CALL ecosim_abi_struct_size(int32_t which);
ECOSIM_API const char* ECOSIM_CALL ecosim_last_error(void);

/* --- lifecycle -------------------------------------------------------- */
ECOSIM_API EcosimWorld* ECOSIM_CALL ecosim_world_create(const EcosimConfig* cfg);
ECOSIM_API void ECOSIM_CALL ecosim_world_destroy(EcosimWorld* world);

/* --- advance and read ------------------------------------------------- */
ECOSIM_API int32_t ECOSIM_CALL ecosim_world_step(EcosimWorld* world, int32_t n_steps);
ECOSIM_API int32_t ECOSIM_CALL ecosim_set_topology(EcosimWorld* world, int32_t topology);
ECOSIM_API int32_t ECOSIM_CALL ecosim_world_grid_view(const EcosimWorld* world,
                                                      EcosimGridView* out);
/* Temporary export, superseded by EcosimStats in milestone M1 Task 10. */
ECOSIM_API int64_t ECOSIM_CALL ecosim_generation(const EcosimWorld* world);

/* --- render ----------------------------------------------------------- */
ECOSIM_API int32_t ECOSIM_CALL ecosim_render_frame(const EcosimWorld* world, uint8_t* bgra,
                                                   int32_t row_pitch,
                                                   const EcosimPalette* palette);

/* Renders a bare species plane, no world involved. The managed side uses this
 * to paint from an immutable snapshot so a repaint never waits on the engine
 * lock (see docs/architecture.md, "渲染"). */
ECOSIM_API int32_t ECOSIM_CALL ecosim_render_species(const uint8_t* species, int32_t w,
                                                     int32_t h, uint8_t* bgra, int32_t row_pitch,
                                                     const EcosimPalette* palette);

/* --- input ------------------------------------------------------------ */
ECOSIM_API int32_t ECOSIM_CALL ecosim_paint(EcosimWorld* world, const EcosimPaintOp* ops,
                                            int32_t count);
ECOSIM_API int32_t ECOSIM_CALL ecosim_clear(EcosimWorld* world, int32_t fill_species);
ECOSIM_API int32_t ECOSIM_CALL ecosim_randomize(EcosimWorld* world,
                                                const EcosimSeedConfig* seed_cfg);

/* --- parameters (hot, effective from the next step) ------------------- */
ECOSIM_API int32_t ECOSIM_CALL ecosim_param_count(void);
ECOSIM_API int32_t ECOSIM_CALL ecosim_param_desc(int32_t index, EcosimParamDesc* out);
ECOSIM_API int32_t ECOSIM_CALL ecosim_params_get(const EcosimWorld* world, EcosimParams* out);
/* Index-addressed access, matching ecosim_param_desc: the managed side never
 * has to know which struct field an index maps to. */
ECOSIM_API int32_t ECOSIM_CALL ecosim_param_get(const EcosimWorld* world, int32_t index,
                                                int32_t* out_value);
ECOSIM_API int32_t ECOSIM_CALL ecosim_param_set(EcosimWorld* world, int32_t index,
                                                int32_t value);
ECOSIM_API int32_t ECOSIM_CALL ecosim_params_set(EcosimWorld* world, const EcosimParams* params);

/* --- analysis and persistence ---------------------------------------- */
ECOSIM_API int32_t ECOSIM_CALL ecosim_stats(const EcosimWorld* world, EcosimStats* out);
ECOSIM_API uint64_t ECOSIM_CALL ecosim_hash(const EcosimWorld* world);
ECOSIM_API int32_t ECOSIM_CALL ecosim_rle_size(const EcosimWorld* world, int32_t* out_needed);
ECOSIM_API int32_t ECOSIM_CALL ecosim_to_rle(const EcosimWorld* world, char* buf, int32_t cap,
                                             int32_t* out_written);
ECOSIM_API int32_t ECOSIM_CALL ecosim_from_rle(EcosimWorld* world, const char* rle);
/* Same, with the pattern's top-left corner placed at (origin_x, origin_y).
 * Added in M1 so "drop this pattern at the cursor" needs no world rebuild. */
ECOSIM_API int32_t ECOSIM_CALL ecosim_from_rle_at(EcosimWorld* world, const char* rle,
                                                  int32_t origin_x, int32_t origin_y);

#ifdef __cplusplus
}  /* extern "C" */
#endif

#endif  /* ECOSIM_ABI_H */

/* Rain on Glass — OBS native port of the WebGL "rain-on-glass-v4" prototype.
 *
 * A CPU stick-slip droplet simulation (video_tick) feeds two dynamic vertex
 * buffers (drop splats + trail ribbons) that are rasterised into half-float
 * height targets and refracted over the parent scene (video_render). See
 * docs/OBS-port-brief.md and docs/rain-on-glass-v4.html for the source spec.
 *
 * libobs has neither a MAX blend equation nor instancing, so:
 *   - splats and ribbons go into two separate additive height targets and are
 *     combined with max() in the composite shader (Method A from the brief);
 *   - the per-drop quads (VS_SPLAT logic) are expanded on the CPU into a plain
 *     triangle list instead of instanced draws.
 */

#include "../sfx-common.h"
#include "../sfx-render.h"

#include <obs-module.h>
#include <graphics/graphics.h>
#include <graphics/vec3.h>
#include <graphics/vec4.h>
#include <util/bmem.h>
#include <plugin-support.h>

#include <math.h>
#include <string.h>

#define MAXDROPS 4000
#define MAXRV 60000 /* max ribbon vertices */
#define MAX_ORPHANS 200
#define PATH_CAP 256 /* ring capacity (> JS path cap of 220) */
#define PATH_TRIM 220
#define CELL 42
#define TRAIL_FADE 0.7f

/* Fixed constants that the prototype keeps off the UI. */
#define P_GROW 1.0f
#define P_EVAP 0.22f
#define GSC 0.02f
#define PIN 0.13f

#define SPLAT_VERTS (MAXDROPS * 6) /* two triangles per drop */

/* --- droplet path: ring buffer of points (push back, shift front) ---------- */
struct pt {
	float x, y, w, t;
};
struct path {
	struct pt *pts; /* NULL until first point */
	int head;
	int count;
};

static void path_free(struct path *p)
{
	if (p->pts)
		bfree(p->pts);
	p->pts = NULL;
	p->head = 0;
	p->count = 0;
}

static inline struct pt *path_at(struct path *p, int i)
{
	return &p->pts[(p->head + i) % PATH_CAP];
}

static void path_push(struct path *p, struct pt v)
{
	if (!p->pts) {
		p->pts = bzalloc(sizeof(struct pt) * PATH_CAP);
		p->head = 0;
		p->count = 0;
	}
	int tail = (p->head + p->count) % PATH_CAP;
	p->pts[tail] = v;
	if (p->count < PATH_CAP)
		p->count++;
	else
		p->head = (p->head + 1) % PATH_CAP;
	/* Match the JS cap (path.length > 220 -> shift). */
	while (p->count > PATH_TRIM) {
		p->head = (p->head + 1) % PATH_CAP;
		p->count--;
	}
}

static void path_shift(struct path *p)
{
	if (p->count > 0) {
		p->head = (p->head + 1) % PATH_CAP;
		p->count--;
	}
}

/* --- droplet ---------------------------------------------------------------- */
struct drop {
	float x, y, r;
	float vx, vy;
	float dirx, diry;
	float seed, thick, phase, stretch;
	float segLen, segTravel, stuckTimer;
	float trailAcc, lineAcc, r0;
	bool sliding, braking, collect, stopGo, trail, pinned, dead;
	int id;
	struct path path;
};

struct orphan {
	struct path path;
	float tDeath;
};

/* --- tunables (internal values, exposed directly per brief §6) ------------- */
struct rain_params {
	float spawn, spawnTrail, spawnStatic;
	float dropSize, dropVar, mergeGain;
	float refr, fog, grav, meander;
	float stopGoProb, sgFreq, sgDecel;
	float maxStretch, wobble, trailLife, trailWidth;
	bool trailBeads, trailLine, debug;
};

struct rain_filter {
	obs_source_t *context;
	gs_effect_t *fx;

	gs_texrender_t *bg;     /* parent scene capture (GS_RGBA) */
	gs_texrender_t *hdrop;  /* splat height (GS_RGBA16F) */
	gs_texrender_t *htrail; /* ribbon height (GS_RGBA16F) */

	gs_vertbuffer_t *splatVB;
	struct gs_vb_data *splatVBD;
	gs_vertbuffer_t *ribVB;
	struct gs_vb_data *ribVBD;

	/* simulation state */
	struct drop drops[MAXDROPS];
	int ndrops;
	struct orphan orphans[MAX_ORPHANS];
	int norphans;
	int next_id;
	uint32_t W, H;
	bool seeded;
	double simTime;
	float spawnAcc, spawnAccT, spawnAccS;
	uint32_t rng;

	/* merge acceleration grid */
	int *grid_head;
	int grid_cells;
	int grid_w, grid_h;
	int next[MAXDROPS];

	struct rain_params P;
};

/* --- PRNG (xorshift32) ------------------------------------------------------ */
static inline float rnd(struct rain_filter *f)
{
	uint32_t x = f->rng;
	x ^= x << 13;
	x ^= x >> 17;
	x ^= x << 5;
	f->rng = x ? x : 1u;
	return (float)(f->rng & 0xFFFFFFu) / (float)0x1000000;
}

/* --- value noise (matches the JS hash2 / vnoise) --------------------------- */
static inline float hash2(int ix, int iy)
{
	int32_t h = (int32_t)(ix * 374761393 + iy * 668265263);
	h = (h ^ (h >> 13)) * 1274126177;
	return (float)((uint32_t)(h ^ (h >> 16))) / 4294967295.0f;
}

static inline float vnoise(float x, float y)
{
	const float s = 0.010f;
	x *= s;
	y *= s;
	float fxr = floorf(x), fyr = floorf(y);
	int ix = (int)fxr, iy = (int)fyr;
	float fx = x - fxr, fy = y - fyr;
	float u = fx * fx * (3.0f - 2.0f * fx);
	float v = fy * fy * (3.0f - 2.0f * fy);
	float a = hash2(ix, iy), b = hash2(ix + 1, iy);
	float c = hash2(ix, iy + 1), d = hash2(ix + 1, iy + 1);
	return a * (1 - u) * (1 - v) + b * u * (1 - v) + c * (1 - u) * v +
	       d * u * v;
}

static inline float clampf(float v, float lo, float hi)
{
	return v < lo ? lo : (v > hi ? hi : v);
}

static float drop_radius(struct rain_filter *f)
{
	float base = f->P.dropSize, vv = f->P.dropVar;
	float r = base * (1 - 0.7f * vv) + base * 1.4f * vv * rnd(f);
	if (rnd(f) < 0.05f)
		r *= 1.6f;
	return r > 0.2f ? r : 0.2f;
}

static float peak_for(const struct drop *d)
{
	float p = (0.30f + d->r * 0.055f) * d->thick;
	return p < 1.6f ? p : 1.6f;
}

static struct drop *make_drop(struct rain_filter *f, float x, float y, float r,
			      bool collect)
{
	if (f->ndrops >= MAXDROPS)
		return NULL;
	struct drop *d = &f->drops[f->ndrops++];
	memset(d, 0, sizeof(*d));
	d->x = x;
	d->y = y;
	d->r = r;
	d->dirx = 0;
	d->diry = 1;
	d->collect = collect;
	d->seed = rnd(f);
	d->thick = 0.7f + rnd(f) * 0.7f;
	d->phase = rnd(f) * 6.2831f;
	d->stretch = 1;
	d->stopGo = rnd(f) < f->P.stopGoProb;
	d->segLen = 8 + rnd(f) * 38;
	d->r0 = r;
	d->id = f->next_id++;
	return d;
}

static void spawn(struct rain_filter *f, float x, float y, float r)
{
	struct drop *d = make_drop(f, x, y, r, true);
	if (d)
		d->trail = false;
}
static void spawn_trail(struct rain_filter *f, float x, float y, float r)
{
	struct drop *d = make_drop(f, x, y, r, true);
	if (d)
		d->trail = true;
}
static void spawn_static(struct rain_filter *f, float x, float y, float r)
{
	struct drop *d = make_drop(f, x, y, r, true);
	if (d)
		d->pinned = true;
}
static void deposit_bead(struct rain_filter *f, float x, float y, float r,
			 bool is_line)
{
	struct drop *d = make_drop(f, x, y, r, false);
	if (d && is_line)
		d->thick = 0.5f;
}

static void seed_field(struct rain_filter *f)
{
	for (int i = 0; i < 90; i++)
		spawn(f, rnd(f) * f->W, rnd(f) * f->H, drop_radius(f));
}

/* --- merge grid ------------------------------------------------------------- */
static void grid_rebuild(struct rain_filter *f)
{
	int gw = (int)(f->W / CELL) + 1;
	int gh = (int)(f->H / CELL) + 1;
	int cells = gw * gh;
	if (cells > f->grid_cells) {
		f->grid_head = brealloc(f->grid_head, sizeof(int) * cells);
		f->grid_cells = cells;
	}
	f->grid_w = gw;
	f->grid_h = gh;
	for (int i = 0; i < cells; i++)
		f->grid_head[i] = -1;
	for (int i = 0; i < f->ndrops; i++) {
		struct drop *d = &f->drops[i];
		int cx = (int)(d->x / CELL);
		int cy = (int)(d->y / CELL);
		cx = cx < 0 ? 0 : (cx >= gw ? gw - 1 : cx);
		cy = cy < 0 ? 0 : (cy >= gh ? gh - 1 : cy);
		int idx = cy * gw + cx;
		f->next[i] = f->grid_head[idx];
		f->grid_head[idx] = i;
	}
}

static void push_orphan(struct rain_filter *f, struct path *src, float tDeath)
{
	if (f->norphans >= MAX_ORPHANS) {
		/* JS shifts the oldest off when over the cap. */
		path_free(&f->orphans[0].path);
		memmove(&f->orphans[0], &f->orphans[1],
			sizeof(struct orphan) * (MAX_ORPHANS - 1));
		f->norphans--;
	}
	f->orphans[f->norphans].path = *src; /* transfer ownership */
	f->orphans[f->norphans].tDeath = tDeath;
	f->norphans++;
	src->pts = NULL;
	src->head = 0;
	src->count = 0;
}

/* --- one simulation step (JS step()) --------------------------------------- */
static void rain_step(struct rain_filter *f, float dt)
{
	struct rain_params *P = &f->P;
	float W = (float)f->W, H = (float)f->H;
	f->simTime += dt;

	f->spawnAcc += P->spawn * dt;
	while (f->spawnAcc >= 1) {
		f->spawnAcc -= 1;
		spawn(f, rnd(f) * W, rnd(f) * H, drop_radius(f));
	}
	f->spawnAccT += P->spawnTrail * dt;
	while (f->spawnAccT >= 1) {
		f->spawnAccT -= 1;
		spawn_trail(f, rnd(f) * W, rnd(f) * H, drop_radius(f));
	}
	f->spawnAccS += P->spawnStatic * dt;
	while (f->spawnAccS >= 1) {
		f->spawnAccS -= 1;
		spawn_static(f, rnd(f) * W, rnd(f) * H, drop_radius(f));
	}

	grid_rebuild(f);

	int n = f->ndrops;
	for (int i = 0; i < n; i++) {
		struct drop *d = &f->drops[i];
		if (d->dead)
			continue;

		if (d->collect)
			d->r -= dt * P_EVAP * (d->r < 3 ? 1.7f : 0.5f);
		else
			d->r -= dt * (d->r0 /
				      (P->trailLife > 0.3f ? P->trailLife : 0.3f));
		d->phase += dt * (0.5f + 0.9f * d->seed);
		if (d->r < 0.15f) {
			d->dead = true;
			continue;
		}

		float w = vnoise(d->x, d->y);
		float drive = d->r * d->r * d->r * GSC;
		float pinF = d->r * d->r * (0.7f + 1.1f * w) * PIN;

		if (!d->sliding) {
			d->stretch += (1 - d->stretch) *
				      (3.0f * dt < 1 ? 3.0f * dt : 1);
			if (d->collect && !d->pinned)
				d->r += dt * P_GROW * (0.3f + 0.7f * d->seed);
			if (drive > pinF && d->r > 4 && !d->pinned) {
				d->sliding = true;
				d->vy = 6 + rnd(f) * 8;
				d->vx = (rnd(f) - 0.5f) * 6;
				d->segTravel = 0;
				d->segLen = 8 + rnd(f) * 30;
			}
			continue;
		}

		/* sliding ------------------------------------------------------ */
		if (d->stuckTimer > 0) {
			d->stuckTimer -= dt;
			if (d->collect)
				d->r += dt * P_GROW * 0.5f;
			d->vx *= 0.5f;
			d->vy *= 0.5f;
			d->x += d->vx * dt;
			d->y += d->vy * dt;
			d->stretch += (1 - d->stretch) *
				      (3.0f * dt < 1 ? 3.0f * dt : 1);
			if (d->stuckTimer <= 0) {
				d->vy = 10 + rnd(f) * 14;
				d->vx += (rnd(f) < 0.5f ? -1 : 1) * P->meander *
					 (0.3f + 0.6f * rnd(f));
				d->segTravel = 0;
				d->segLen = 8 + rnd(f) * 34;
			}
			continue;
		}

		float dx, dy;
		if (d->braking) {
			float k = 1 - P->sgDecel * dt;
			d->vx *= k > 0 ? k : 0;
			d->vy *= k > 0 ? k : 0;
			dx = d->vx * dt;
			dy = d->vy * dt;
			d->x += dx;
			d->y += dy;
			if (hypotf(d->vx, d->vy) < 6) {
				d->braking = false;
				d->stuckTimer = 0.25f + rnd(f) * 2.0f;
				d->vx = 0;
				d->vy = 0;
			}
		} else {
			float sizeF = clampf((d->r - 3) / 8, 0.10f, 1.5f);
			d->vy += P->grav * dt * sizeF;
			d->vy *= (1 - 2.6f * dt);
			d->vx *= (1 - 1.0f * dt);
			dx = d->vx * dt;
			dy = d->vy * dt;
			d->x += dx;
			d->y += dy;
			d->segTravel += hypotf(dx, dy);
			if (d->segTravel >= d->segLen) {
				d->segTravel = 0;
				d->segLen = 8 + rnd(f) * 38;
				if (d->stopGo && rnd(f) < P->sgFreq) {
					d->braking = true;
				} else {
					float dir = rnd(f) < 0.5f ? -1.0f : 1.0f;
					d->vx += dir * P->meander *
						 (0.4f + rnd(f)) * (0.6f + w);
					if (d->vy < 10)
						d->vy = 10;
				}
			}
			float sp0 = hypotf(d->vx, d->vy);
			if (sp0 < 12 && drive < pinF * 1.05f) {
				d->sliding = false;
				d->braking = false;
				d->vx = 0;
				d->vy = 0;
				continue;
			}
		}

		float moved = hypotf(dx, dy);
		float sp = hypotf(d->vx, d->vy);
		if (sp > 2) {
			d->dirx = d->vx / sp;
			d->diry = d->vy / sp;
		}
		float target = 1 + clampf((sp - 20) / 120, 0, 1) *
				       (P->maxStretch - 1);
		d->stretch += (target - d->stretch) *
			      (3.0f * dt < 1 ? 3.0f * dt : 1);

		/* trails: connected line and/or discrete beads (coexist) */
		float life = P->trailLife > 0.3f ? P->trailLife : 0.3f;
		if (P->trailLine && d->collect && d->trail) {
			float hw = d->r * P->trailWidth;
			if (hw < 0.8f)
				hw = 0.8f;
			d->lineAcc += moved;
			float thresh = hw * 0.5f < 2.0f ? 2.0f : hw * 0.5f;
			if (d->lineAcc >= thresh || d->path.count == 0) {
				d->lineAcc = 0;
				struct pt p = {d->x, d->y - d->r * 0.25f, hw,
					       (float)f->simTime};
				path_push(&d->path, p);
			}
			while (d->path.count &&
			       f->simTime - path_at(&d->path, 0)->t > life)
				path_shift(&d->path);
		} else if (d->path.count) {
			while (d->path.count &&
			       f->simTime - path_at(&d->path, 0)->t > life)
				path_shift(&d->path);
		}
		if (P->trailBeads && d->trail) {
			d->trailAcc += moved;
			float rr = d->r * 0.22f;
			if (rr < 1.1f)
				rr = 1.1f;
			float spacing = rr * (sp > 120 ? 1.4f
					      : (sp > 50 ? 2.2f : 3.4f));
			if (d->trailAcc > spacing) {
				d->trailAcc = 0;
				deposit_bead(f,
					     d->x + (rnd(f) - 0.5f) * d->r * 0.3f,
					     d->y - d->r * 0.6f,
					     rr * (0.8f + 0.5f * rnd(f)), false);
				/* make_drop may grow the array; refresh d. */
				d = &f->drops[i];
				float v = d->r * d->r * d->r - rr * rr * rr * 0.5f;
				d->r = cbrtf(v > 0 ? v : 0);
			}
		}
		if (d->r < 2.1f) {
			d->dead = true;
			continue;
		}

		/* coalescence with smaller nearby drops */
		int cx = (int)(d->x / CELL), cy = (int)(d->y / CELL);
		for (int ox = -1; ox <= 1; ox++)
			for (int oy = -1; oy <= 1; oy++) {
				int gx = cx + ox, gy = cy + oy;
				if (gx < 0 || gy < 0 || gx >= f->grid_w ||
				    gy >= f->grid_h)
					continue;
				int j = f->grid_head[gy * f->grid_w + gx];
				for (; j >= 0; j = f->next[j]) {
					if (j == i)
						continue;
					struct drop *e = &f->drops[j];
					if (e->dead || e->r > d->r)
						continue;
					if (hypotf(e->x - d->x, e->y - d->y) <
					    d->r * 0.9f + e->r) {
						d->r = cbrtf(d->r * d->r * d->r +
							     P->mergeGain * e->r *
								     e->r * e->r);
						d->vy += 10;
						e->dead = true;
					}
				}
			}
		if (d->y - d->r > H + 8)
			d->dead = true;
	}

	/* reap dead drops; rescue trails as fading orphans (order-preserving) */
	int w = 0;
	for (int i = 0; i < f->ndrops; i++) {
		struct drop *d = &f->drops[i];
		if (d->dead) {
			if (d->path.count >= 2)
				push_orphan(f, &d->path, (float)f->simTime);
			else
				path_free(&d->path);
			continue;
		}
		if (w != i)
			f->drops[w] = *d;
		w++;
	}
	f->ndrops = w;

	for (int i = f->norphans - 1; i >= 0; i--) {
		if (f->simTime - f->orphans[i].tDeath > TRAIL_FADE) {
			path_free(&f->orphans[i].path);
			memmove(&f->orphans[i], &f->orphans[i + 1],
				sizeof(struct orphan) * (f->norphans - 1 - i));
			f->norphans--;
		}
	}

	/* hard cap relief: drop small non-collecting beads first */
	if (f->ndrops > MAXDROPS - 20) {
		int w2 = 0;
		for (int i = 0; i < f->ndrops; i++) {
			struct drop *d = &f->drops[i];
			if (f->ndrops - (i - w2) > MAXDROPS - 20 &&
			    !d->collect && d->r < 3) {
				path_free(&d->path);
				continue;
			}
			if (w2 != i)
				f->drops[w2] = *d;
			w2++;
		}
		f->ndrops = w2;
	}
}

/* --- vertex buffer plumbing ------------------------------------------------- */
static gs_vertbuffer_t *make_dynamic_vb(size_t max_verts, size_t num_tex,
					const uint8_t *tex_widths,
					struct gs_vb_data **out_vbd)
{
	struct gs_vb_data *vbd = gs_vbdata_create();
	vbd->num = max_verts;
	vbd->points = bzalloc(sizeof(struct vec3) * max_verts);
	vbd->num_tex = num_tex;
	vbd->tvarray = bzalloc(sizeof(struct gs_tvertarray) * num_tex);
	for (size_t t = 0; t < num_tex; t++) {
		vbd->tvarray[t].width = tex_widths[t];
		vbd->tvarray[t].array =
			bzalloc(sizeof(float) * tex_widths[t] * max_verts);
	}
	gs_vertbuffer_t *vb = gs_vertexbuffer_create(vbd, GS_DYNAMIC);
	*out_vbd = vbd;
	return vb;
}

/* Expand each drop into two triangles (VS_SPLAT logic on the CPU). */
static uint32_t build_splats(struct rain_filter *f)
{
	struct gs_vb_data *vbd = f->splatVBD;
	struct vec3 *pos = vbd->points;
	float *tlocal = (float *)vbd->tvarray[0].array; /* width 2 */
	float *tspp = (float *)vbd->tvarray[1].array;   /* width 4 */

	static const float cx[6] = {-1, 1, -1, 1, 1, -1};
	static const float cy[6] = {-1, -1, 1, -1, 1, 1};

	uint32_t v = 0;
	for (int i = 0; i < f->ndrops && v + 6 <= SPLAT_VERTS; i++) {
		struct drop *d = &f->drops[i];
		float dirx = d->dirx, diry = d->diry;
		float perpx = -diry, perpy = dirx;
		float base = d->r * 3.0f;
		float peak = peak_for(d);
		for (int k = 0; k < 6; k++) {
			float lcx = cx[k], lcy = cy[k];
			float offx = perpx * (lcx * base) +
				     dirx * (lcy * base * d->stretch);
			float offy = perpy * (lcx * base) +
				     diry * (lcy * base * d->stretch);
			pos[v].x = d->x + offx;
			pos[v].y = d->y + offy;
			pos[v].z = 0.0f;
			tlocal[v * 2 + 0] = lcx;
			tlocal[v * 2 + 1] = lcy;
			tspp[v * 4 + 0] = d->seed;
			tspp[v * 4 + 1] = peak;
			tspp[v * 4 + 2] = d->stretch;
			tspp[v * 4 + 3] = d->phase;
			v++;
		}
	}
	if (v > 0)
		gs_vertexbuffer_flush(f->splatVB);
	return v;
}

/* Build ribbon triangles from live paths + fading orphans (JS buildRibbons). */
static void rib_emit(struct rain_filter *f, struct path *path, float g,
		     float life, struct vec3 *pos, float *tep, uint32_t *vp)
{
	uint32_t v = *vp;
	int n = path->count;
	if (n < 2)
		return;
	for (int k = 0; k < n - 1 && v + 6 <= MAXRV; k++) {
		struct pt *p0 = path_at(path, k);
		struct pt *p1 = path_at(path, k + 1);
		struct pt *a0 = path_at(path, k > 0 ? k - 1 : k);
		struct pt *b0 = path_at(path, k + 1);
		float t0x = b0->x - a0->x, t0y = b0->y - a0->y;
		float l0 = hypotf(t0x, t0y);
		if (l0 == 0)
			l0 = 1;
		t0x /= l0;
		t0y /= l0;
		struct pt *a1 = path_at(path, k);
		struct pt *b1 = path_at(path, (k + 2 < n) ? k + 2 : n - 1);
		float t1x = b1->x - a1->x, t1y = b1->y - a1->y;
		float l1 = hypotf(t1x, t1y);
		if (l1 == 0)
			l1 = 1;
		t1x /= l1;
		t1y /= l1;
		float px0 = -t0y, py0 = t0x, px1 = -t1y, py1 = t1x;
		float f0 = 1 - ((float)f->simTime - p0->t) / life;
		float f1 = 1 - ((float)f->simTime - p1->t) / life;
		f0 = f0 < 0 ? 0 : f0;
		f1 = f1 < 0 ? 0 : f1;
		float wf0 = f0 * f0 * (3.0f - 2.0f * f0);
		float wf1 = f1 * f1 * (3.0f - 2.0f * f1);
		float pf0 = sqrtf(f0) * g, pf1 = sqrtf(f1) * g;
		float w0 = p0->w * (0.05f + 0.95f * wf0);
		float w1 = p1->w * (0.05f + 0.95f * wf1);
		float pk0 = (0.16f + 0.05f * p0->w) * pf0;
		float pk1 = (0.16f + 0.05f * p1->w) * pf1;
		float L0x = p0->x + px0 * w0, L0y = p0->y + py0 * w0;
		float R0x = p0->x - px0 * w0, R0y = p0->y - py0 * w0;
		float L1x = p1->x + px1 * w1, L1y = p1->y + py1 * w1;
		float R1x = p1->x - px1 * w1, R1y = p1->y - py1 * w1;

#define RIB_PUSH(X, Y, E, PK)                       \
	do {                                        \
		pos[v].x = (X);                     \
		pos[v].y = (Y);                     \
		pos[v].z = 0.0f;                    \
		tep[v * 2 + 0] = (E);               \
		tep[v * 2 + 1] = (PK);              \
		v++;                                \
	} while (0)

		RIB_PUSH(L0x, L0y, 1.0f, pk0);
		RIB_PUSH(R0x, R0y, -1.0f, pk0);
		RIB_PUSH(R1x, R1y, -1.0f, pk1);
		RIB_PUSH(L0x, L0y, 1.0f, pk0);
		RIB_PUSH(R1x, R1y, -1.0f, pk1);
		RIB_PUSH(L1x, L1y, 1.0f, pk1);
#undef RIB_PUSH
	}
	*vp = v;
}

static uint32_t build_ribbons(struct rain_filter *f)
{
	struct gs_vb_data *vbd = f->ribVBD;
	struct vec3 *pos = vbd->points;
	float *tep = (float *)vbd->tvarray[0].array; /* width 2 */
	float life = f->P.trailLife > 0.3f ? f->P.trailLife : 0.3f;
	uint32_t v = 0;

	for (int di = 0; di < f->ndrops && v + 6 <= MAXRV; di++) {
		struct drop *d = &f->drops[di];
		if (d->path.count >= 2)
			rib_emit(f, &d->path, 1.0f, life, pos, tep, &v);
	}
	for (int oi = 0; oi < f->norphans && v + 6 <= MAXRV; oi++) {
		struct orphan *o = &f->orphans[oi];
		float g = 1 - ((float)f->simTime - o->tDeath) / TRAIL_FADE;
		if (g > 0)
			rib_emit(f, &o->path, g, life, pos, tep, &v);
	}
	if (v > 0)
		gs_vertexbuffer_flush(f->ribVB);
	return v;
}

/* --- render passes ---------------------------------------------------------- */
static void render_height(struct rain_filter *f, gs_texrender_t *tr,
			  const char *tech, gs_vertbuffer_t *vb, uint32_t count)
{
	gs_texrender_reset(tr);
	if (!gs_texrender_begin(tr, f->W, f->H))
		return;

	struct vec4 clear;
	vec4_zero(&clear);
	gs_clear(GS_CLEAR_COLOR, &clear, 0.0f, 0);
	gs_ortho(0.0f, (float)f->W, 0.0f, (float)f->H, -100.0f, 100.0f);

	if (count > 0) {
		gs_blend_state_push();
		gs_blend_function(GS_BLEND_ONE, GS_BLEND_ONE);
		gs_enable_depth_test(false);
		if (strcmp(tech, "Splat") == 0)
			sfx_set_float(f->fx, "u_wobble", f->P.wobble);
		while (gs_effect_loop(f->fx, tech)) {
			gs_load_vertexbuffer(vb);
			gs_load_indexbuffer(NULL);
			gs_draw(GS_TRIS, 0, count);
		}
		gs_blend_state_pop();
	}
	gs_texrender_end(tr);
}

static void rain_render(void *data, gs_effect_t *unused)
{
	UNUSED_PARAMETER(unused);
	struct rain_filter *f = data;
	if (!f->fx) {
		obs_source_skip_video_filter(f->context);
		return;
	}

	uint32_t cx, cy;
	sfx_target_size(f->context, &cx, &cy);
	if (cx == 0 || cy == 0) {
		obs_source_skip_video_filter(f->context);
		return;
	}
	f->W = cx;
	f->H = cy;

	gs_texture_t *bg = sfx_capture_input(f->context, f->bg, cx, cy);
	if (!bg) {
		obs_source_skip_video_filter(f->context);
		return;
	}

	uint32_t nsplat = build_splats(f);
	uint32_t nrib = build_ribbons(f);

	render_height(f, f->htrail, "Ribbon", f->ribVB, nrib);
	render_height(f, f->hdrop, "Splat", f->splatVB, nsplat);

	gs_blend_state_push();
	gs_blend_function(GS_BLEND_ONE, GS_BLEND_ZERO);
	gs_enable_depth_test(false);
	sfx_set_texture(f->fx, "u_bg", bg);
	sfx_set_texture(f->fx, "u_hdrop", gs_texrender_get_texture(f->hdrop));
	sfx_set_texture(f->fx, "u_htrail", gs_texrender_get_texture(f->htrail));
	sfx_set_vec2(f->fx, "u_res", (float)cx, (float)cy);
	sfx_set_float(f->fx, "u_refr", f->P.refr);
	sfx_set_float(f->fx, "u_fog", f->P.fog);
	sfx_set_float(f->fx, "u_debug", f->P.debug ? 1.0f : 0.0f);
	while (gs_effect_loop(f->fx, "Composite"))
		gs_draw_sprite(bg, 0, cx, cy);
	gs_blend_state_pop();
}

static void rain_tick(void *data, float seconds)
{
	struct rain_filter *f = data;
	uint32_t cx, cy;
	sfx_target_size(f->context, &cx, &cy);
	if (cx == 0 || cy == 0)
		return;
	f->W = cx;
	f->H = cy;
	if (!f->seeded) {
		seed_field(f);
		f->seeded = true;
	}
	float dt = seconds < 1.0f / 30.0f ? seconds : 1.0f / 30.0f;
	if (dt > 0)
		rain_step(f, dt);
}

/* --- OBS boilerplate -------------------------------------------------------- */
static const char *rain_get_name(void *unused)
{
	UNUSED_PARAMETER(unused);
	return obs_module_text("Rain");
}

static void rain_update(void *data, obs_data_t *s)
{
	struct rain_filter *f = data;
	struct rain_params *P = &f->P;
	P->spawn = (float)obs_data_get_double(s, "spawn");
	P->spawnTrail = (float)obs_data_get_double(s, "spawn_trail");
	P->spawnStatic = (float)obs_data_get_double(s, "spawn_static");
	P->dropSize = (float)obs_data_get_double(s, "drop_size");
	P->dropVar = (float)obs_data_get_double(s, "drop_var");
	P->mergeGain = (float)obs_data_get_double(s, "merge_gain");
	P->refr = (float)obs_data_get_double(s, "refraction");
	P->fog = (float)obs_data_get_double(s, "fog");
	P->grav = (float)obs_data_get_double(s, "flow");
	P->meander = (float)obs_data_get_double(s, "meander");
	P->stopGoProb = (float)obs_data_get_double(s, "stopgo_apply");
	P->sgFreq = (float)obs_data_get_double(s, "stopgo_freq");
	P->sgDecel = (float)obs_data_get_double(s, "stop_decel");
	P->maxStretch = (float)obs_data_get_double(s, "max_stretch");
	P->wobble = (float)obs_data_get_double(s, "roundness");
	P->trailLife = (float)obs_data_get_double(s, "trail_life");
	P->trailWidth = (float)obs_data_get_double(s, "trail_width");
	int mode = (int)obs_data_get_int(s, "trail_mode");
	P->trailBeads = (mode != 1);
	P->trailLine = (mode != 0);
	P->debug = obs_data_get_bool(s, "debug");
}

static void *rain_create(obs_data_t *settings, obs_source_t *context)
{
	struct rain_filter *f = bzalloc(sizeof(*f));
	f->context = context;
	f->next_id = 1;
	f->rng = 0x9e3779b9u;
	f->grid_head = NULL;
	f->grid_cells = 0;

	obs_enter_graphics();
	char *path = obs_module_file("effects/rain.effect");
	if (path) {
		f->fx = gs_effect_create_from_file(path, NULL);
		if (!f->fx)
			obs_log(LOG_ERROR, "failed to load rain.effect (%s)",
				path);
		bfree(path);
	}
	f->bg = gs_texrender_create(GS_RGBA, GS_ZS_NONE);
	f->hdrop = gs_texrender_create(GS_RGBA16F, GS_ZS_NONE);
	f->htrail = gs_texrender_create(GS_RGBA16F, GS_ZS_NONE);

	const uint8_t splat_tex[2] = {2, 4};
	f->splatVB = make_dynamic_vb(SPLAT_VERTS, 2, splat_tex, &f->splatVBD);
	const uint8_t rib_tex[1] = {2};
	f->ribVB = make_dynamic_vb(MAXRV, 1, rib_tex, &f->ribVBD);
	obs_leave_graphics();

	rain_update(f, settings);
	return f;
}

static void rain_destroy(void *data)
{
	struct rain_filter *f = data;
	obs_enter_graphics();
	if (f->fx)
		gs_effect_destroy(f->fx);
	if (f->bg)
		gs_texrender_destroy(f->bg);
	if (f->hdrop)
		gs_texrender_destroy(f->hdrop);
	if (f->htrail)
		gs_texrender_destroy(f->htrail);
	if (f->splatVB)
		gs_vertexbuffer_destroy(f->splatVB);
	if (f->ribVB)
		gs_vertexbuffer_destroy(f->ribVB);
	obs_leave_graphics();

	for (int i = 0; i < f->ndrops; i++)
		path_free(&f->drops[i].path);
	for (int i = 0; i < f->norphans; i++)
		path_free(&f->orphans[i].path);
	if (f->grid_head)
		bfree(f->grid_head);
	bfree(f);
}

static obs_properties_t *rain_properties(void *data)
{
	UNUSED_PARAMETER(data);
	obs_properties_t *p = obs_properties_create();
	obs_properties_t *basic = obs_properties_create();
	obs_properties_t *adv = obs_properties_create();

	obs_properties_add_float_slider(basic, "spawn",
		obs_module_text("Rain.Spawn"), 0.0, 120.0, 1.0);
	obs_properties_add_float_slider(basic, "spawn_trail",
		obs_module_text("Rain.SpawnTrail"), 0.0, 120.0, 1.0);
	obs_properties_add_float_slider(basic, "spawn_static",
		obs_module_text("Rain.SpawnStatic"), 0.0, 120.0, 1.0);
	obs_properties_add_float_slider(basic, "drop_size",
		obs_module_text("Rain.DropSize"), 0.2, 12.0, 0.1);
	obs_properties_add_float_slider(basic, "merge_gain",
		obs_module_text("Rain.MergeGain"), 0.0, 1.0, 0.01);
	obs_properties_add_float_slider(basic, "refraction",
		obs_module_text("Rain.Refraction"), 0.0, 0.30, 0.001);
	obs_properties_add_float_slider(basic, "fog",
		obs_module_text("Rain.Fog"), 0.0, 1.0, 0.01);
	obs_properties_add_float_slider(basic, "trail_life",
		obs_module_text("Rain.TrailLife"), 1.0, 36.0, 0.5);
	obs_properties_add_float_slider(basic, "trail_width",
		obs_module_text("Rain.TrailWidth"), 0.2, 1.5, 0.01);
	obs_property_t *tm = obs_properties_add_list(basic, "trail_mode",
		obs_module_text("Rain.TrailMode"), OBS_COMBO_TYPE_LIST,
		OBS_COMBO_FORMAT_INT);
	obs_property_list_add_int(tm, obs_module_text("Rain.TrailBeads"), 0);
	obs_property_list_add_int(tm, obs_module_text("Rain.TrailLine"), 1);
	obs_property_list_add_int(tm, obs_module_text("Rain.TrailBoth"), 2);

	obs_properties_add_float_slider(adv, "drop_var",
		obs_module_text("Rain.DropVar"), 0.0, 1.0, 0.01);
	obs_properties_add_float_slider(adv, "flow",
		obs_module_text("Rain.Flow"), 80.0, 480.0, 1.0);
	obs_properties_add_float_slider(adv, "meander",
		obs_module_text("Rain.Meander"), 0.0, 200.0, 1.0);
	obs_properties_add_float_slider(adv, "stopgo_apply",
		obs_module_text("Rain.StopGoApply"), 0.0, 1.0, 0.01);
	obs_properties_add_float_slider(adv, "stopgo_freq",
		obs_module_text("Rain.StopGoFreq"), 0.0, 1.0, 0.01);
	obs_properties_add_float_slider(adv, "stop_decel",
		obs_module_text("Rain.StopDecel"), 1.0, 15.0, 0.1);
	obs_properties_add_float_slider(adv, "max_stretch",
		obs_module_text("Rain.MaxStretch"), 1.0, 2.5, 0.01);
	obs_properties_add_float_slider(adv, "roundness",
		obs_module_text("Rain.Roundness"), 0.0, 1.0, 0.01);
	obs_properties_add_bool(adv, "debug", obs_module_text("Rain.Debug"));

	obs_properties_add_group(p, "basic_grp", obs_module_text("GroupBasic"),
				 OBS_GROUP_NORMAL, basic);
	obs_properties_add_group(p, "adv_grp", obs_module_text("GroupAdvanced"),
				 OBS_GROUP_NORMAL, adv);
	return p;
}

static void rain_defaults(obs_data_t *s)
{
	obs_data_set_default_double(s, "spawn", 1.0);
	obs_data_set_default_double(s, "spawn_trail", 1.0);
	obs_data_set_default_double(s, "spawn_static", 119.0);
	obs_data_set_default_double(s, "drop_size", 1.4);
	obs_data_set_default_double(s, "drop_var", 0.18);
	obs_data_set_default_double(s, "merge_gain", 0.15);
	obs_data_set_default_double(s, "refraction", 0.1168);
	obs_data_set_default_double(s, "fog", 0.57);
	obs_data_set_default_double(s, "flow", 480.0);
	obs_data_set_default_double(s, "meander", 20.0);
	obs_data_set_default_double(s, "stopgo_apply", 0.5);
	obs_data_set_default_double(s, "stopgo_freq", 0.25);
	obs_data_set_default_double(s, "stop_decel", 6.6);
	obs_data_set_default_double(s, "max_stretch", 1.38);
	obs_data_set_default_double(s, "roundness", 0.47);
	obs_data_set_default_double(s, "trail_life", 20.0);
	obs_data_set_default_double(s, "trail_width", 0.76);
	obs_data_set_default_int(s, "trail_mode", 2);
	obs_data_set_default_bool(s, "debug", false);
}

struct obs_source_info sfx_rain_info = {
	.id             = "sfx_rain",
	.type           = OBS_SOURCE_TYPE_FILTER,
	.output_flags   = OBS_SOURCE_VIDEO | OBS_SOURCE_CUSTOM_DRAW,
	.get_name       = rain_get_name,
	.create         = rain_create,
	.destroy        = rain_destroy,
	.update         = rain_update,
	.video_tick     = rain_tick,
	.video_render   = rain_render,
	.get_properties = rain_properties,
	.get_defaults   = rain_defaults,
};

void sfx_register_rain(void)
{
	obs_register_source(&sfx_rain_info);
}

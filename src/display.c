#include "internal.h"
#include <limits.h>
#include <stdlib.h>
#include <string.h>

typedef struct cached_image
{
    char *identity;
    hddvd_image_plane plane;
    size_t refs;
    struct cached_image *next;
} cached_image;

struct hddvd_image_cache
{
    hddvd_image_decode_fn decode;
    void *opaque;
    hddvd_display_limits limits;
    cached_image *images;
    size_t count;
    uint64_t pixels;
};

typedef struct display_item
{
    hddvd_display_quad quad;
    cached_image *image;
    uint8_t hit;
    size_t order;
} display_item;
struct hddvd_display_list
{
    display_item *items;
    size_t count;
    hddvd_rect *damage;
    size_t damage_count;
};

void hddvd_display_limits_default(hddvd_display_limits *l)
{
    if (!l)
        return;
    l->max_images = 256;
    l->max_encoded_bytes = 64U * 1024U * 1024U;
    l->max_decoded_pixels = 1920U * 1080U * 8U;
    l->max_dimension = 8192;
    l->max_quads = 4096;
}

int hddvd_image_cache_create(hddvd_image_decode_fn fn, void *opaque,
                             const hddvd_display_limits *limits, hddvd_image_cache **out)
{
    hddvd_image_cache *c;
    if (!fn || !out)
        return HDDVD_EINVAL;
    *out = NULL;
    c = (hddvd_image_cache *)calloc(1, sizeof(*c));
    if (!c)
        return HDDVD_ENOMEM;
    c->decode = fn;
    c->opaque = opaque;
    if (limits)
        c->limits = *limits;
    else
        hddvd_display_limits_default(&c->limits);
    if (!c->limits.max_images || !c->limits.max_decoded_pixels || !c->limits.max_dimension ||
        !c->limits.max_quads)
    {
        free(c);
        return HDDVD_EINVAL;
    }
    *out = c;
    return HDDVD_OK;
}

void hddvd_image_cache_destroy(hddvd_image_cache *c)
{
    cached_image *p, *n;
    if (!c)
        return;
    for (p = c->images; p; p = n)
    {
        n = p->next;
        if (p->plane.release)
            p->plane.release(p->plane.release_opaque, p->plane.pixels);
        free(p->identity);
        free(p);
    }
    free(c);
}

static int image_get(hddvd_image_cache *c, const hddvd_image_source *s, cached_image **out)
{
    cached_image *p, *previous, *victim;
    hddvd_image_plane plane;
    uint64_t pixels;
    if (!s->identity || !s->identity[0] || (!s->encoded && s->encoded_size))
        return HDDVD_EINVAL;
    if ((uint64_t)s->encoded_size > c->limits.max_encoded_bytes)
        return HDDVD_ELIMIT;
    for (p = c->images; p; p = p->next)
        if (!strcmp(p->identity, s->identity))
        {
            p->refs++;
            *out = p;
            return HDDVD_OK;
        }
    memset(&plane, 0, sizeof(plane));
    {
        int rc = c->decode(c->opaque, s->identity, s->encoded, s->encoded_size, &plane);
        if (rc != HDDVD_OK)
            return rc;
    }
    if (!plane.pixels || !plane.width || !plane.height || plane.width > c->limits.max_dimension ||
        plane.height > c->limits.max_dimension || plane.stride < (size_t)plane.width * 4U)
    {
        if (plane.release)
            plane.release(plane.release_opaque, plane.pixels);
        return HDDVD_EPARSE;
    }
    pixels = (uint64_t)plane.width * plane.height;
    while (c->count >= c->limits.max_images || pixels > c->limits.max_decoded_pixels - c->pixels)
    {
        previous = NULL;
        victim = c->images;
        while (victim && victim->refs)
        {
            previous = victim;
            victim = victim->next;
        }
        if (!victim)
        {
            if (plane.release)
                plane.release(plane.release_opaque, plane.pixels);
            return HDDVD_ELIMIT;
        }
        if (previous)
            previous->next = victim->next;
        else
            c->images = victim->next;
        c->pixels -= (uint64_t)victim->plane.width * victim->plane.height;
        c->count--;
        if (victim->plane.release)
            victim->plane.release(victim->plane.release_opaque, victim->plane.pixels);
        free(victim->identity);
        free(victim);
    }
    p = (cached_image *)calloc(1, sizeof(*p));
    if (!p)
    {
        if (plane.release)
            plane.release(plane.release_opaque, plane.pixels);
        return HDDVD_ENOMEM;
    }
    p->identity = hddvd_strdup(s->identity);
    if (!p->identity)
    {
        free(p);
        if (plane.release)
            plane.release(plane.release_opaque, plane.pixels);
        return HDDVD_ENOMEM;
    }
    p->plane = plane;
    p->refs = 1;
    p->next = c->images;
    c->images = p;
    c->count++;
    c->pixels += pixels;
    *out = p;
    return HDDVD_OK;
}

static int rect_intersect(hddvd_rect a, hddvd_rect b, hddvd_rect *o)
{
    int64_t l = a.x > b.x ? a.x : b.x, t = a.y > b.y ? a.y : b.y;
    int64_t ar = (int64_t)a.x + a.width, br = (int64_t)b.x + b.width, ab = (int64_t)a.y + a.height,
            bb = (int64_t)b.y + b.height;
    int64_t r = ar < br ? ar : br, bot = ab < bb ? ab : bb;
    if (r <= l || bot <= t)
        return 0;
    o->x = (int32_t)l;
    o->y = (int32_t)t;
    o->width = (uint32_t)(r - l);
    o->height = (uint32_t)(bot - t);
    return 1;
}
static int cmp_item(const void *a, const void *b)
{
    const display_item *x = a, *y = b;
    if (x->quad.z_index < y->quad.z_index)
        return -1;
    if (x->quad.z_index > y->quad.z_index)
        return 1;
    return x->order < y->order ? -1 : x->order > y->order;
}
static int same_quad(const display_item *a, const display_item *b)
{
    return a->quad.node_id == b->quad.node_id && a->image == b->image &&
           !memcmp(&a->quad.destination, &b->quad.destination, sizeof(hddvd_rect)) &&
           !memcmp(&a->quad.clip, &b->quad.clip, sizeof(hddvd_rect)) &&
           a->quad.z_index == b->quad.z_index && a->quad.opacity == b->quad.opacity;
}
static void add_damage(hddvd_display_list *l, hddvd_rect r)
{
    if (r.width && r.height)
        l->damage[l->damage_count++] = r;
}

int hddvd_display_build(hddvd_image_cache *cache, uint32_t width, uint32_t height,
                        const hddvd_scene_node *nodes, size_t count, const hddvd_display_list *old,
                        hddvd_display_list **out)
{
    hddvd_display_list *l;
    int64_t *xs = NULL, *ys = NULL;
    hddvd_rect *clips = NULL;
    uint8_t *visible = NULL;
    size_t i, j;
    int rc = HDDVD_OK;
    if (!cache || !out || (!nodes && count) || !width || !height)
        return HDDVD_EINVAL;
    *out = NULL;
    if (count > cache->limits.max_quads)
        return HDDVD_ELIMIT;
    l = (hddvd_display_list *)calloc(1, sizeof(*l));
    if (!l)
        return HDDVD_ENOMEM;
    l->items = (display_item *)calloc(count ? count : 1, sizeof(*l->items));
    l->damage = (hddvd_rect *)calloc((count + (old ? old->count : 0)) * 2 + 1, sizeof(*l->damage));
    xs = (int64_t *)calloc(count ? count : 1, sizeof(*xs));
    ys = (int64_t *)calloc(count ? count : 1, sizeof(*ys));
    clips = (hddvd_rect *)calloc(count ? count : 1, sizeof(*clips));
    visible = (uint8_t *)calloc(count ? count : 1, 1);
    if (!l->items || !l->damage || !xs || !ys || !clips || !visible)
    {
        rc = HDDVD_ENOMEM;
        goto fail;
    }
    for (i = 0; i < count; i++)
    {
        const hddvd_scene_node *n = &nodes[i];
        hddvd_rect bounds, ownclip;
        const hddvd_image_source *source;
        cached_image *img;
        if (n->parent != SIZE_MAX && n->parent >= i)
        {
            rc = HDDVD_EINVAL;
            goto fail;
        }
        xs[i] = (n->parent == SIZE_MAX ? 0 : xs[n->parent]) + n->bounds.x;
        ys[i] = (n->parent == SIZE_MAX ? 0 : ys[n->parent]) + n->bounds.y;
        if (xs[i] < INT32_MIN || xs[i] > INT32_MAX || ys[i] < INT32_MIN || ys[i] > INT32_MAX)
        {
            rc = HDDVD_ELIMIT;
            goto fail;
        }
        bounds = (hddvd_rect){(int32_t)xs[i], (int32_t)ys[i], n->bounds.width, n->bounds.height};
        clips[i] = (hddvd_rect){0, 0, width, height};
        if (n->parent != SIZE_MAX)
            clips[i] = clips[n->parent];
        ownclip = n->clip;
        if (!ownclip.width || !ownclip.height)
            ownclip = (hddvd_rect){0, 0, n->bounds.width, n->bounds.height};
        {
            int64_t cx = (int64_t)ownclip.x + xs[i], cy = (int64_t)ownclip.y + ys[i];
            if (cx < INT32_MIN || cx > INT32_MAX || cy < INT32_MIN || cy > INT32_MAX)
            {
                rc = HDDVD_ELIMIT;
                goto fail;
            }
            ownclip.x = (int32_t)cx;
            ownclip.y = (int32_t)cy;
        }
        visible[i] = (uint8_t)(n->visible && (n->parent == SIZE_MAX || visible[n->parent]) &&
                               rect_intersect(clips[i], ownclip, &clips[i]));
        if (!visible[i] || !rect_intersect(clips[i], bounds, &ownclip))
            continue;
        source = (n->focused && n->focused_image.identity) ? &n->focused_image : &n->normal_image;
        if (!source->identity)
            continue;
        rc = image_get(cache, source, &img);
        if (rc != HDDVD_OK)
            goto fail;
        l->items[l->count].quad = (hddvd_display_quad){
            n->id,      img->plane, {0, 0, img->plane.width, img->plane.height}, bounds, ownclip,
            n->z_index, n->opacity};
        l->items[l->count].image = img;
        l->items[l->count].hit = n->hit_test;
        l->items[l->count].order = i;
        l->count++;
    }
    qsort(l->items, l->count, sizeof(*l->items), cmp_item);
    for (i = 0; i < l->count; i++)
    {
        int found = 0;
        for (j = 0; old && j < old->count; j++)
            if (old->items[j].quad.node_id == l->items[i].quad.node_id)
            {
                found = 1;
                if (!same_quad(&l->items[i], &old->items[j]))
                {
                    add_damage(l, old->items[j].quad.clip);
                    add_damage(l, l->items[i].quad.clip);
                }
                break;
            }
        if (!found)
            add_damage(l, l->items[i].quad.clip);
    }
    if (old)
        for (j = 0; j < old->count; j++)
        {
            int found = 0;
            for (i = 0; i < l->count; i++)
                if (old->items[j].quad.node_id == l->items[i].quad.node_id)
                {
                    found = 1;
                    break;
                }
            if (!found)
                add_damage(l, old->items[j].quad.clip);
        }
    free(xs);
    free(ys);
    free(clips);
    free(visible);
    *out = l;
    return HDDVD_OK;
fail:
    free(xs);
    free(ys);
    free(clips);
    free(visible);
    hddvd_display_list_close(l);
    return rc;
}

void hddvd_display_list_close(hddvd_display_list *l)
{
    size_t i;
    if (!l)
        return;
    for (i = 0; i < l->count; i++)
        if (l->items[i].image)
            l->items[i].image->refs--;
    free(l->items);
    free(l->damage);
    free(l);
}
size_t hddvd_display_quad_count(const hddvd_display_list *l)
{
    return l ? l->count : 0;
}
int hddvd_display_get_quad(const hddvd_display_list *l, size_t i, hddvd_display_quad *q)
{
    if (!l || !q || i >= l->count)
        return HDDVD_EINVAL;
    *q = l->items[i].quad;
    return HDDVD_OK;
}
size_t hddvd_display_damage_count(const hddvd_display_list *l)
{
    return l ? l->damage_count : 0;
}
int hddvd_display_get_damage(const hddvd_display_list *l, size_t i, hddvd_rect *r)
{
    if (!l || !r || i >= l->damage_count)
        return HDDVD_EINVAL;
    *r = l->damage[i];
    return HDDVD_OK;
}

void hddvd_video_transform_fit(uint32_t lw, uint32_t lh, uint32_t vw, uint32_t vh,
                               hddvd_video_transform *t)
{
    uint64_t w, h;
    if (!t)
        return;
    memset(t, 0, sizeof(*t));
    if (!lw || !lh || !vw || !vh)
        return;
    t->logical_width = lw;
    t->logical_height = lh;
    if ((uint64_t)vw * lh <= (uint64_t)vh * lw)
    {
        w = vw;
        h = (uint64_t)vw * lh / lw;
    }
    else
    {
        h = vh;
        w = (uint64_t)vh * lw / lh;
    }
    t->width = (uint32_t)w;
    t->height = (uint32_t)h;
    t->x = (int32_t)((vw - w) / 2);
    t->y = (int32_t)((vh - h) / 2);
}
int hddvd_video_to_logical(const hddvd_video_transform *t, int32_t x, int32_t y, int32_t *lx,
                           int32_t *ly)
{
    if (!t || !lx || !ly || !t->width || !t->height || x < t->x || y < t->y ||
        (uint64_t)(x - t->x) >= t->width || (uint64_t)(y - t->y) >= t->height)
        return HDDVD_ENOTFOUND;
    *lx = (int32_t)((uint64_t)(x - t->x) * t->logical_width / t->width);
    *ly = (int32_t)((uint64_t)(y - t->y) * t->logical_height / t->height);
    return HDDVD_OK;
}
int hddvd_display_hit_test(const hddvd_display_list *l, int32_t x, int32_t y, uint64_t *id)
{
    size_t i;
    if (!l || !id)
        return HDDVD_EINVAL;
    for (i = l->count; i; i--)
    {
        const display_item *p = &l->items[i - 1];
        const hddvd_rect *r = &p->quad.clip;
        if (p->hit && x >= r->x && y >= r->y && (uint64_t)(x - r->x) < r->width &&
            (uint64_t)(y - r->y) < r->height)
        {
            *id = p->quad.node_id;
            return HDDVD_OK;
        }
    }
    return HDDVD_ENOTFOUND;
}

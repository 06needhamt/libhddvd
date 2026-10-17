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

/* Items within one node paint as fill, image, text. */
enum item_layer
{
    LAYER_FILL,
    LAYER_IMAGE,
    LAYER_TEXT
};
typedef struct display_item
{
    hddvd_display_quad quad;
    cached_image *image;
    uint8_t hit;
    size_t order;
    enum item_layer layer;
    uint32_t color;
    uint8_t has_color;
    const hddvd_scene_decoration *decoration;
} display_item;
struct hddvd_display_list
{
    display_item *items;
    size_t count;
    /* Indices of the image items, which the quad accessors report. */
    size_t *quads;
    size_t quad_count;
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

int hddvd_image_cache_create_with_config(hddvd_image_decode_fn fn, void *opaque,
                                         const hddvd_config_limits *config, hddvd_image_cache **out)
{
    hddvd_display_limits limits;
    if (!hddvd_config_limits_valid(config))
        return HDDVD_EINVAL;
    limits.max_images = config->max_images;
    limits.max_encoded_bytes = config->max_encoded_image_bytes;
    limits.max_decoded_pixels = config->max_decoded_pixels;
    limits.max_dimension = config->max_image_dimension;
    limits.max_quads = config->max_quads;
    return hddvd_image_cache_create(fn, opaque, &limits, out);
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
    if (x->order != y->order)
        return x->order < y->order ? -1 : 1;
    return (int)x->layer - (int)y->layer;
}
static int same_quad(const display_item *a, const display_item *b)
{
    const char *ta = a->decoration && a->layer == LAYER_TEXT ? a->decoration->text : NULL;
    const char *tb = b->decoration && b->layer == LAYER_TEXT ? b->decoration->text : NULL;
    return a->quad.node_id == b->quad.node_id && a->layer == b->layer && a->image == b->image &&
           !memcmp(&a->quad.destination, &b->quad.destination, sizeof(hddvd_rect)) &&
           !memcmp(&a->quad.clip, &b->quad.clip, sizeof(hddvd_rect)) &&
           a->quad.z_index == b->quad.z_index && a->quad.opacity == b->quad.opacity &&
           a->color == b->color && a->has_color == b->has_color &&
           (ta == tb || (ta && tb && !strcmp(ta, tb)));
}
static int same_item(const display_item *a, const display_item *b)
{
    return a->quad.node_id == b->quad.node_id && a->layer == b->layer;
}

/* Offset of a placed image along one axis of its node. */
static int64_t place(hddvd_xmu_background_position position, int32_t offset, uint32_t box,
                     uint32_t size)
{
    int64_t room = (int64_t)box - size;
    switch (position)
    {
        case HDDVD_XMU_BACKGROUND_CENTER:
            return room / 2;
        case HDDVD_XMU_BACKGROUND_END:
            return room;
        case HDDVD_XMU_BACKGROUND_OFFSET:
            return offset;
        case HDDVD_XMU_BACKGROUND_PERCENT:
            return room * offset / 100;
        default:
            return 0;
    }
}

/* Where a placed image is drawn: its own size, or a percentage of the
   node, positioned within the node. */
static int placed_rect(const hddvd_scene_decoration *d, hddvd_rect bounds,
                       const hddvd_image_plane *plane, hddvd_rect *out)
{
    uint32_t w = d->image_width_percent
                     ? (uint32_t)((uint64_t)bounds.width * d->image_width_percent / 100u)
                     : plane->width;
    uint32_t h = d->image_height_percent
                     ? (uint32_t)((uint64_t)bounds.height * d->image_height_percent / 100u)
                     : plane->height;
    int64_t x = (int64_t)bounds.x + place(d->image_x, d->image_x_offset, bounds.width, w);
    int64_t y = (int64_t)bounds.y + place(d->image_y, d->image_y_offset, bounds.height, h);
    if (x < INT32_MIN || x > INT32_MAX || y < INT32_MIN || y > INT32_MAX)
        return HDDVD_ELIMIT;
    *out = (hddvd_rect){(int32_t)x, (int32_t)y, w, h};
    return HDDVD_OK;
}
static void add_damage(hddvd_display_list *l, hddvd_rect r)
{
    if (r.width && r.height)
        l->damage[l->damage_count++] = r;
}

static int build(hddvd_image_cache *cache, uint32_t width, uint32_t height,
                 const hddvd_scene_node *nodes, const hddvd_scene_decoration *decorations,
                 size_t count, const hddvd_display_list *old, hddvd_display_list **out)
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
    /* Up to a fill, an image and a text item per node. */
    l->items = (display_item *)calloc(count ? count * 3 : 1, sizeof(*l->items));
    l->quads = (size_t *)calloc(count ? count : 1, sizeof(*l->quads));
    l->damage =
        (hddvd_rect *)calloc((count * 3 + (old ? old->count : 0)) * 2 + 1, sizeof(*l->damage));
    xs = (int64_t *)calloc(count ? count : 1, sizeof(*xs));
    ys = (int64_t *)calloc(count ? count : 1, sizeof(*ys));
    clips = (hddvd_rect *)calloc(count ? count : 1, sizeof(*clips));
    visible = (uint8_t *)calloc(count ? count : 1, 1);
    if (!l->items || !l->quads || !l->damage || !xs || !ys || !clips || !visible)
    {
        rc = HDDVD_ENOMEM;
        goto fail;
    }
    for (i = 0; i < count; i++)
    {
        const hddvd_scene_node *n = &nodes[i];
        const hddvd_scene_decoration *d = decorations ? &decorations[i] : NULL;
        hddvd_rect bounds, ownclip, destination;
        const hddvd_image_source *source;
        cached_image *img;
        display_item *item;
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
        if (d && (d->properties & HDDVD_SCENE_FILL))
        {
            item = &l->items[l->count++];
            item->quad = (hddvd_display_quad){n->id,   {0},        {0, 0, 0, 0}, bounds,
                                              ownclip, n->z_index, n->opacity};
            item->layer = LAYER_FILL;
            item->color = d->fill_color;
            item->has_color = 1;
            item->hit = n->hit_test;
            item->order = i;
            item->decoration = d;
        }
        source = (n->focused && n->focused_image.identity) ? &n->focused_image : &n->normal_image;
        if (source->identity)
        {
            hddvd_rect visible_part;
            rc = image_get(cache, source, &img);
            if (rc != HDDVD_OK)
                goto fail;
            destination = bounds;
            if (d && (d->properties & HDDVD_SCENE_IMAGE_PLACED) &&
                (rc = placed_rect(d, bounds, &img->plane, &destination)) != HDDVD_OK)
            {
                img->refs--;
                goto fail;
            }
            /* A placed image entirely outside its node draws nothing. */
            if (!rect_intersect(ownclip, destination, &visible_part))
                img->refs--;
            else
            {
                item = &l->items[l->count++];
                item->quad = (hddvd_display_quad){
                    n->id,       img->plane, {0, 0, img->plane.width, img->plane.height},
                    destination, ownclip,    n->z_index,
                    n->opacity};
                item->image = img;
                item->layer = LAYER_IMAGE;
                item->hit = n->hit_test;
                item->order = i;
                item->decoration = d;
            }
        }
        if (d && (d->properties & HDDVD_SCENE_TEXT))
        {
            item = &l->items[l->count++];
            item->quad = (hddvd_display_quad){n->id,   {0},        {0, 0, 0, 0}, bounds,
                                              ownclip, n->z_index, n->opacity};
            item->layer = LAYER_TEXT;
            item->color = d->text_color;
            item->has_color = (uint8_t)((d->properties & HDDVD_SCENE_TEXT_COLOR) != 0);
            item->hit = n->hit_test;
            item->order = i;
            item->decoration = d;
        }
    }
    qsort(l->items, l->count, sizeof(*l->items), cmp_item);
    for (i = 0; i < l->count; i++)
        if (l->items[i].layer == LAYER_IMAGE)
            l->quads[l->quad_count++] = i;
    for (i = 0; i < l->count; i++)
    {
        int found = 0;
        for (j = 0; old && j < old->count; j++)
            if (same_item(&old->items[j], &l->items[i]))
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
                if (same_item(&old->items[j], &l->items[i]))
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

int hddvd_display_build(hddvd_image_cache *cache, uint32_t width, uint32_t height,
                        const hddvd_scene_node *nodes, size_t count, const hddvd_display_list *old,
                        hddvd_display_list **out)
{
    return build(cache, width, height, nodes, NULL, count, old, out);
}

int hddvd_display_build_scene(hddvd_image_cache *cache, uint32_t width, uint32_t height,
                              const hddvd_scene *scene, const hddvd_display_list *old,
                              hddvd_display_list **out)
{
    if (!scene)
    {
        if (out)
            *out = NULL;
        return HDDVD_EINVAL;
    }
    return build(cache, width, height, hddvd_scene_nodes(scene),
                 hddvd_scene_decorations_internal(scene), hddvd_scene_node_count(scene), old, out);
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
    free(l->quads);
    free(l->damage);
    free(l);
}
size_t hddvd_display_quad_count(const hddvd_display_list *l)
{
    return l ? l->quad_count : 0;
}
int hddvd_display_get_quad(const hddvd_display_list *l, size_t i, hddvd_display_quad *q)
{
    if (!l || !q || i >= l->quad_count)
        return HDDVD_EINVAL;
    *q = l->items[l->quads[i]].quad;
    return HDDVD_OK;
}
size_t hddvd_display_item_count(const hddvd_display_list *l)
{
    return l ? l->count : 0;
}
int hddvd_display_get_item(const hddvd_display_list *l, size_t i, hddvd_display_item *out)
{
    static const hddvd_display_item_kind kinds[] = {
        HDDVD_DISPLAY_ITEM_FILL, HDDVD_DISPLAY_ITEM_IMAGE, HDDVD_DISPLAY_ITEM_TEXT};
    const display_item *p;
    if (!l || !out || i >= l->count)
        return HDDVD_EINVAL;
    p = &l->items[i];
    memset(out, 0, sizeof(*out));
    out->kind = kinds[p->layer];
    out->quad = p->quad;
    out->color = p->color;
    out->has_color = p->has_color;
    if (p->layer == LAYER_TEXT && p->decoration)
    {
        out->text = p->decoration->text;
        out->font = p->decoration->font;
        out->font_size = p->decoration->font_size;
        out->font_style = p->decoration->font_style;
        out->text_align = p->decoration->text_align;
        out->display_align = p->decoration->display_align;
        out->wrap = p->decoration->wrap;
    }
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

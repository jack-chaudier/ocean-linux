/*
 * Ocean Kernel - Capability System
 *
 * Capabilities are unforgeable tokens that grant access to kernel objects.
 * All object access goes through the capability system.
 *
 * Each process has a capability space (cspace) containing its capabilities.
 * Capabilities can be:
 *   - Copied to other slots or processes
 *   - Minted with reduced rights
 *   - Revoked (via generation counters)
 */

#include <ocean/ipc.h>
#include <ocean/process.h>
#include <ocean/types.h>
#include <ocean/defs.h>

/* External functions */
extern int kprintf(const char *fmt, ...);
extern void *kmalloc(size_t size);
extern void kfree(void *ptr);
extern void *memset(void *s, int c, size_t n);
extern void *memcpy(void *dest, const void *src, size_t n);

/*
 * Initialize a capability space
 */
void cspace_init(struct cspace *cs)
{
    if (!cs) {
        return;
    }

    spin_init(&cs->lock);

    /* Allocate initial slot array */
    cs->size = CSPACE_SIZE;
    cs->slots = kmalloc(cs->size * sizeof(struct capability));
    if (!cs->slots) {
        kprintf("[cap] Failed to allocate cspace slots!\n");
        return;
    }

    memset(cs->slots, 0, cs->size * sizeof(struct capability));

    /* Allocate bitmap (1 bit per slot) */
    size_t bitmap_size = (cs->size + 63) / 64 * sizeof(u64);
    cs->bitmap = kmalloc(bitmap_size);
    if (!cs->bitmap) {
        kfree(cs->slots);
        cs->slots = NULL;
        kprintf("[cap] Failed to allocate cspace bitmap!\n");
        return;
    }

    memset(cs->bitmap, 0, bitmap_size);

    cs->used = 0;
    cs->generation = 1;

    kprintf("[cap] Initialized cspace with %u slots\n", cs->size);
}

/*
 * Destroy a capability space
 */
void cspace_destroy(struct cspace *cs)
{
    if (!cs) {
        return;
    }

    spin_lock(&cs->lock);

    /* Release all held capabilities */
    for (u32 i = 0; i < cs->size; i++) {
        if (cs->slots[i].type != CAP_TYPE_NONE) {
            /* TODO: Release the underlying object */
            cs->slots[i].type = CAP_TYPE_NONE;
        }
    }

    if (cs->slots) {
        kfree(cs->slots);
        cs->slots = NULL;
    }

    if (cs->bitmap) {
        kfree(cs->bitmap);
        cs->bitmap = NULL;
    }

    cs->size = 0;
    cs->used = 0;

    spin_unlock(&cs->lock);
}

/*
 * Find a free slot in the capability space
 */
static i32 cspace_find_free_slot(struct cspace *cs)
{
    for (u32 i = 0; i < cs->size; i++) {
        u32 word_idx = i / 64;
        u32 bit_idx = i % 64;

        if (!(cs->bitmap[word_idx] & (1ULL << bit_idx))) {
            return (i32)i;
        }
    }

    return -1;  /* No free slot */
}

/*
 * Mark a slot as used
 */
static void cspace_mark_used(struct cspace *cs, u32 slot)
{
    u32 word_idx = slot / 64;
    u32 bit_idx = slot % 64;
    cs->bitmap[word_idx] |= (1ULL << bit_idx);
    cs->used++;
}

/*
 * Mark a slot as free
 */
static void cspace_mark_free(struct cspace *cs, u32 slot)
{
    u32 word_idx = slot / 64;
    u32 bit_idx = slot % 64;
    cs->bitmap[word_idx] &= ~(1ULL << bit_idx);
    cs->used--;
}

/*
 * Insert a capability into the space
 *
 * Returns the slot number, or -1 on error.
 */
int cap_insert(struct cspace *cs, struct capability *cap)
{
    if (!cs || !cap) {
        return -1;
    }

    spin_lock(&cs->lock);

    /* Find a free slot */
    i32 slot = cspace_find_free_slot(cs);
    if (slot < 0) {
        spin_unlock(&cs->lock);
        kprintf("[cap] No free slots in cspace!\n");
        return -1;
    }

    /* Copy capability into slot */
    cs->slots[slot] = *cap;
    cs->slots[slot].slot = slot;
    cs->slots[slot].generation = cs->generation;

    cspace_mark_used(cs, slot);

    spin_unlock(&cs->lock);

    kprintf("[cap] Inserted cap type %u at slot %d\n", cap->type, slot);

    return slot;
}

/*
 * Look up a capability by slot
 *
 * Returns NULL if slot is empty or invalid.
 */
struct capability *cap_lookup(struct cspace *cs, u32 slot)
{
    if (!cs || slot >= cs->size) {
        return NULL;
    }

    spin_lock(&cs->lock);

    struct capability *cap = NULL;

    if (cs->slots[slot].type != CAP_TYPE_NONE) {
        cap = &cs->slots[slot];
    }

    spin_unlock(&cs->lock);

    return cap;
}

/*
 * Delete a capability from a slot
 */
int cap_delete(struct cspace *cs, u32 slot)
{
    if (!cs || slot >= cs->size) {
        return -1;
    }

    spin_lock(&cs->lock);

    if (cs->slots[slot].type == CAP_TYPE_NONE) {
        spin_unlock(&cs->lock);
        return -1;  /* Slot already empty */
    }

    /* Clear the slot */
    memset(&cs->slots[slot], 0, sizeof(struct capability));
    cspace_mark_free(cs, slot);

    spin_unlock(&cs->lock);

    kprintf("[cap] Deleted cap at slot %u\n", slot);

    return 0;
}

/*
 * Copy a capability from one slot to another
 *
 * Can copy within the same cspace or between different cspaces.
 */
int cap_copy(struct cspace *dst, u32 dst_slot,
             struct cspace *src, u32 src_slot)
{
    if (!dst || !src || src_slot >= src->size) {
        return -1;
    }

    /* Lock in consistent order to avoid deadlock */
    if (dst < src) {
        spin_lock(&dst->lock);
        spin_lock(&src->lock);
    } else if (dst > src) {
        spin_lock(&src->lock);
        spin_lock(&dst->lock);
    } else {
        /* Same cspace */
        spin_lock(&dst->lock);
    }

    int result = -1;

    /* Check source slot */
    if (src->slots[src_slot].type == CAP_TYPE_NONE) {
        goto out;
    }

    /* Check if source has grant rights */
    if (!(src->slots[src_slot].rights & CAP_RIGHT_GRANT)) {
        kprintf("[cap] Copy denied: no GRANT right\n");
        goto out;
    }

    /* Determine destination slot */
    u32 actual_dst;
    if (dst_slot == (u32)-1) {
        /* Find a free slot */
        i32 free = cspace_find_free_slot(dst);
        if (free < 0) {
            goto out;
        }
        actual_dst = (u32)free;
    } else {
        if (dst_slot >= dst->size) {
            goto out;
        }
        if (dst->slots[dst_slot].type != CAP_TYPE_NONE) {
            goto out;  /* Destination not empty */
        }
        actual_dst = dst_slot;
    }

    /* Copy the capability */
    dst->slots[actual_dst] = src->slots[src_slot];
    dst->slots[actual_dst].slot = actual_dst;
    dst->slots[actual_dst].generation = dst->generation;

    cspace_mark_used(dst, actual_dst);

    result = (int)actual_dst;

out:
    if (dst != src) {
        spin_unlock(&src->lock);
    }
    spin_unlock(&dst->lock);

    return result;
}

/*
 * Mint a new capability with reduced rights
 *
 * Creates a derived capability with possibly fewer rights and a badge.
 */
int cap_mint(struct cspace *dst, u32 dst_slot,
             struct cspace *src, u32 src_slot,
             u32 new_rights, u64 badge)
{
    if (!dst || !src || src_slot >= src->size) {
        return -1;
    }

    /* Lock in consistent order */
    if (dst < src) {
        spin_lock(&dst->lock);
        spin_lock(&src->lock);
    } else if (dst > src) {
        spin_lock(&src->lock);
        spin_lock(&dst->lock);
    } else {
        spin_lock(&dst->lock);
    }

    int result = -1;

    /* Check source slot */
    if (src->slots[src_slot].type == CAP_TYPE_NONE) {
        goto out;
    }

    /* Check if source has grant rights */
    if (!(src->slots[src_slot].rights & CAP_RIGHT_GRANT)) {
        kprintf("[cap] Mint denied: no GRANT right\n");
        goto out;
    }

    /* New rights cannot exceed original rights */
    u32 allowed_rights = src->slots[src_slot].rights & new_rights;

    /* Determine destination slot */
    u32 actual_dst;
    if (dst_slot == (u32)-1) {
        i32 free = cspace_find_free_slot(dst);
        if (free < 0) {
            goto out;
        }
        actual_dst = (u32)free;
    } else {
        if (dst_slot >= dst->size) {
            goto out;
        }
        if (dst->slots[dst_slot].type != CAP_TYPE_NONE) {
            goto out;
        }
        actual_dst = dst_slot;
    }

    /* Create minted capability */
    dst->slots[actual_dst] = src->slots[src_slot];
    dst->slots[actual_dst].slot = actual_dst;
    dst->slots[actual_dst].rights = allowed_rights;
    dst->slots[actual_dst].badge = badge;
    dst->slots[actual_dst].generation = dst->generation;

    cspace_mark_used(dst, actual_dst);

    result = (int)actual_dst;

    kprintf("[cap] Minted cap at slot %u (rights=0x%x, badge=0x%llx)\n",
            actual_dst, allowed_rights, badge);

out:
    if (dst != src) {
        spin_unlock(&src->lock);
    }
    spin_unlock(&dst->lock);

    return result;
}

/*
 * Revoke all capabilities derived from a given capability
 *
 * Uses generation counters to invalidate derived capabilities.
 * This is a simplified implementation - full revocation requires
 * tracking the derivation tree.
 */
int cap_revoke(struct cspace *cs, u32 slot)
{
    if (!cs || slot >= cs->size) {
        return -1;
    }

    spin_lock(&cs->lock);

    if (cs->slots[slot].type == CAP_TYPE_NONE) {
        spin_unlock(&cs->lock);
        return -1;
    }

    /* Check revoke rights */
    if (!(cs->slots[slot].rights & CAP_RIGHT_REVOKE)) {
        spin_unlock(&cs->lock);
        kprintf("[cap] Revoke denied: no REVOKE right\n");
        return -1;
    }

    /* Increment generation to invalidate derived caps */
    cs->generation++;

    spin_unlock(&cs->lock);

    kprintf("[cap] Revoked cap at slot %u (new generation: %u)\n",
            slot, cs->generation);

    return 0;
}

/*
 * Create a capability for an endpoint
 */
int cap_create_endpoint(struct cspace *cs, struct ipc_endpoint *ep, u32 rights)
{
    struct capability cap = {
        .type = CAP_TYPE_ENDPOINT,
        .rights = rights,
        .object = (u64)ep,
        .badge = 0,
        .generation = 0,
        .slot = 0
    };

    return cap_insert(cs, &cap);
}

/*
 * Create a capability for a notification object
 */
int cap_create_notification(struct cspace *cs, struct notification *ntfn, u32 rights)
{
    struct capability cap = {
        .type = CAP_TYPE_NOTIFICATION,
        .rights = rights,
        .object = (u64)ntfn,
        .badge = 0,
        .generation = 0,
        .slot = 0
    };

    return cap_insert(cs, &cap);
}

/*
 * Validate a capability has required rights
 */
int cap_check_rights(struct capability *cap, u32 required_rights)
{
    if (!cap) {
        return -1;
    }

    if ((cap->rights & required_rights) != required_rights) {
        return -1;
    }

    return 0;
}

/*
 * Get the endpoint from an endpoint capability
 */
struct ipc_endpoint *cap_get_endpoint(struct cspace *cs, u32 slot)
{
    struct capability *cap = cap_lookup(cs, slot);

    if (!cap || cap->type != CAP_TYPE_ENDPOINT) {
        return NULL;
    }

    return (struct ipc_endpoint *)cap->object;
}

/*
 * Debug: dump capability space
 */
void cap_dump_cspace(struct cspace *cs)
{
    if (!cs) {
        kprintf("  (null cspace)\n");
        return;
    }

    spin_lock(&cs->lock);

    kprintf("Capability Space:\n");
    kprintf("  Size: %u, Used: %u, Generation: %u\n",
            cs->size, cs->used, cs->generation);

    static const char *type_names[] = {
        [CAP_TYPE_NONE] = "NONE",
        [CAP_TYPE_ENDPOINT] = "ENDPOINT",
        [CAP_TYPE_MEMORY] = "MEMORY",
        [CAP_TYPE_THREAD] = "THREAD",
        [CAP_TYPE_PROCESS] = "PROCESS",
        [CAP_TYPE_IRQ] = "IRQ",
        [CAP_TYPE_IO_PORT] = "IO_PORT",
        [CAP_TYPE_NOTIFICATION] = "NOTIFICATION",
    };

    for (u32 i = 0; i < cs->size && i < 32; i++) {  /* Limit output */
        if (cs->slots[i].type != CAP_TYPE_NONE) {
            const char *type = "UNKNOWN";
            if (cs->slots[i].type < 8) {
                type = type_names[cs->slots[i].type];
            }
            kprintf("  [%3u] Type: %-12s Rights: 0x%04x Object: 0x%llx Badge: 0x%llx\n",
                    i, type, cs->slots[i].rights,
                    cs->slots[i].object, cs->slots[i].badge);
        }
    }

    spin_unlock(&cs->lock);
}

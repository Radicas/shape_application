/**
 * @file poly_area.c
 * @author Radica
 * @brief This file contains functions to maintain two dimensional 3-trees
 *        representing regions. This is the same as sdarea.c, but using floating
 *        point coordinate values.
 * @version 0.1
 * @date 2024-07-29
 *
 * @copyright Copyright (c) 2024
 *
 */

#include <stdio.h>
#include "osassert.h"
#include "osstdlib.h"
#include "oslimits.h"
#include "SDarea.h"
#include "SYchunk.h"

/*
    This is for debug output
*/
#ifdef OSASSERT
#define TEXT_out(s) fprintf(stderr, s);
#else
#define TEXT_out(s)
#endif

#define SEARCHCALL_U1U2(a, start, fn)           \
    {                                           \
        for (a = start; a != NULL; a = a->next) \
            if (!DISJOINT(x1, y1, x2, y2, a))   \
                (*fn)(a->ud1, a->ud2);          \
    }

#define SEARCHCALL_XY(a, start, fn)             \
    {                                           \
        for (a = start; a != NULL; a = a->next) \
            if (!DISJOINT(x1, y1, x2, y2, a))   \
            {                                   \
                if ((*fn)(a))                   \
                    return TRUE;                \
            }                                   \
    }

#define RETURN_LL          \
    {                      \
        *what = TRUE;      \
        return (oslong)ll; \
    }

__STATIC(FXYITEM *gimme_new_fxyitem, (void *));
__STATIC(int adjust_fbb, (FXYTREE_PVT * tr));
__STATIC(int fttl, (int n));
__STATIC(oslong rebalance_fxytree_guts, (FXYITEM * tl, char *what));
__STATIC(void FTree_to_linked_list, (FXYTREE_PVT * t, FXYITEM **ll));
__STATIC(void free_fxynodes, (FXYTREE_PVT * b, int free_root));
__STATIC(void recompute_all_fbbs, (FXYTREE_PVT * p));

/*
    sdfarea - maintain two dimensional 3-trees representing regions

    This set of routines is designed for maintaining two diamensional
    trees that detect collisions of regions in a PCB board environment.
    A two dimensional tree is a 3-node tree that is pivoted on a split
    coordinate. The three nodes are links to left, middle, and right
    and the split coordinate is used to tell us whether left, middle,
    and right refer to a vertical cut (split in x) or a horizontal
    cut(split in y). As used in component swap, the regions corres-
    pond to the bounds of a dynamically varying set of component shapes.
    The regions form a linked list at the leaves of the tree; the tree
    nodes themselves have no regions but do contain a bounding box
    encompassing the extents of the nodes below.

    Routines are designed for area searching in an editor environment.
    Optimized for mostly small objects. This whole package internally
    assumes x2 >= x1 and y2 >= y1. This is fixed first thing in each
    user-visible routine.

    XYTREE* make_fxytree(x);
        makes up an area data structure, and returns a pointer to it.
        set x = 0, for now.

    register_farea(xy, x1, y1, x2, y2, ud1, ud2);
    FXYTREE* xy; double x1, y1, x2, y2; oslong ud1, ud2;
        this registers an area for later use. ud1 and ud2 are user fields;
        ud1 is usually a type, and ud2 a pointer, but both are officially
        integers. since things are added to the tree without rebalancing,
        tree might be (very) lopsided if you do enough things without re-
        balancing. However, insertion is always fast.

    unregister_farea(tl, x1, y1, x2, y2, u1, u2)
    FXYTREE* xy; double x1, y1, x2, y2; oslong ud1, ud2;
        removes an area from further condsideration and frees the area memory.
        rebalancing is recommended to prevent lopsidedness and holes in
        the chunk memory

    rebalance_fxytree(xy)
    FXYTREE** xy;
        rebalances the tree so that searches proceed efficiently.

    free_fxytree(xy)
    FXYTREE* xy;
        calls free on all stuff that has been allocated.

    print_fxytree(fp, xy, n)
    FXYTREE* xy;
        prints and XYtree indented 2N spaces from left margin.

    DB_set_fxytree_search_box(ptr, x1, y1, x2, y2)
    double x1, y1, x2, y2;
    FXYTREE* ptr;
        sets up a search area for an iterative search

    DB_reset_fxytree_search_box(searchHandle, ptr, x1, y1, x2, y2)
    double x1, y1, x2, y2;
    FXYTREE* ptr;
    void* searchHandle;
        resets the given search area to the given coords

    DB_get_next_fxyitem(ptr)
    FXYITEM** ptr;
        gets the next item in the search area. Returns TRUE if it found
        any, and sets *ptr. If it doesn't find any, returns FALSE.
        Don't delete items while searching, since this routine keeps an
        internal pointer of what to look at next.

    NOTES
        this whole package internally assumes x2 >= x1 and y2 >= y1.
*/

#define MEM_PAGE_SIZE_MULTIPLIER 8
#define SD_MEM_PAGE_SIZE 1024

/*
    make_fxytree() - starts a new tree. X should be a guess about the middle of
        the design, but it is not important provided you do a balance later.
*/
FXYTREE_PVT *make_fxytree_pvt(double x)
{
    FXYTREE_PVT *tl;

    tl = (FXYTREE_PVT *)malloc(sizeof(FXYTREE_PVT));
    tl->xsplit = TRUE;
    tl->coord = x;
    tl->ptr[LEFT].al = NULL;
    tl->ptr[MIDDLE].al = NULL;
    tl->ptr[RIGHT].al = NULL;
    tl->is_list[LEFT] = TRUE;
    tl->is_list[MIDDLE] = TRUE;
    tl->is_list[RIGHT] = TRUE;
    tl->x1 = (double)DBL_MAX;
    tl->y1 = (double)DBL_MAX;
    tl->x2 = (double)-DBL_MAX;
    tl->y2 = (double)-DBL_MAX;

    return (tl);
}

FXYTREE *make_fxytree(double x)
{
    FXYTREE *tl = (FXYTREE *)calloc(1, sizeof(FXYTREE));
    tl->fxyTreePvt = make_fxytree_pvt(x);
    tl->itemMemory = SY_Chunk_Init(MEM_PAGE_SIZE_MULTIPLIER * SD_MEM_PAGE_SIZE, SYCHUNK_ALIGN, SYCHUNK_MALLOC);
    return tl;
}

static FXYITEM *gimme_new_fxyitem(void *itemMemory)
{
    return (FXYITEM *)SY_ChunkCalloc(itemMemory, 1, sizeof(FXYITEM));
}

void register_farea(
    FXYTREE *tl,
    double x1,
    double y1,
    double x2,
    double y2,
    oslong u1,
    oslong u2)
{
    FXYITEM *a;
    FXYITEM **base;
    int num; // should be 0, 1, or 2
    FXYTREE_PVT *tree;

    void *itemMemory = t1->itemMemory;
    tree = tl->fxyTreePvt;

    FIXFORDER(x1, x2);
    FIXFORDER(y1, y2);
    a = gimme_new_fxyitem(itemMemory);

    a->x1 = x1;
    a->x2 = x2;
    a->y1 = y1;
    a->y2 = y2;
    a->ud1 = u1;
    a->ud2 = u2;

    /*
        Now find the right list to add this to
     */
    for (;; tree = tree->ptr[num].xy) // this for loop searches down the tree
    {
        if (x1 < tree->x1)
            tree->x1 = x1; // expand BBOX of tree node
        if (x2 > tree->x2)
            tree->x2 = x2;
        if (y1 < tree->y1)
            tree->y1 = y1;
        if (y2 > tree->y2)
            tree->y2 = y2;
        FINDSIDE(tree, x1, y1, x2, y2, num);
        if (tree->is_list[num])
            break;
    }

    base = &(tree->ptr[num].al);
    a->next = *base;
    *base = a;
}

/**
 * @brief Recompute the bounding box fields of an FXYTREE node.
 *
 * @param tr
 * @return 1 they need to be adjusted
 */
static int adjust_fbb(FXYTREE_PVT *tr)
{
    double x1;
    double y1;
    double x2;
    double y2;
    int i;

    x1 = y1 = (double)DBL_MAX;
    x2 = y2 = (double)-DBL_MAX;

    for (i = LEFT; i <= RIGHT; i++)
    {
        if (tr->is_list[i])
        {
            FXYITEM *p = tr->ptr[i].al;
            for (; p != NULL; p = p->next)
            {
                if (p->x1 < x1)
                    x1 = p->x1;
                if (p->x2 > x2)
                    x2 = p->x2;
                if (p->y1 < y1)
                    y1 = p->y1;
                if (p->y2 > y2)
                    y2 = p->y2;
            }
        }
        else
        {
            FXYTREE_PVT *q = tr->ptr[i].xy;
            if (q->x1 < x1)
                x1 = q->x1;
            if (q->x2 > x2)
                x2 = q->x2;
            if (q->y1 < y1)
                y1 = q->y1;
            if (q->y2 > y2)
                y2 = q->y2;
        }
    }

    if (x1 != tr->x1 || x2 != tr->x2 || y1 != tr->y1 || y2 != tr->y2)
    {
        tr->x1 = x1;
        tr->y1 = y1;
        tr->x2 = x2;
        tr->y2 = y2;
        return TRUE;
    }
    else
        return FALSE; // no changes
}

/**
 * @brief Delete an item from the tree. It must be specified
 * exactly, and it is considered an error for it not to be present. The
 * only tricky part here is recomputing the bounding boxes. We keep an
 * explicit stack instead of doing this by recursion.
 *
 * @param tl
 * @param x1
 * @param y1
 * @param x2
 * @param y2
 * @param u1
 * @param u2
 * @return int
 */
int unregister_farea(
    FXYTREE *tl,
    double x1,
    double y1,
    double x2,
    double y2,
    oslong u1,
    oslong u2)
{
    FXYITEM *l;
    FXYITEM **tr;
    FXYTREE_PVT **xs;
    int num; // should be 0, 1, or 2
    FXYTREE_PVT *xy_stack[100];

    FIXFORDER(x1, x2);
    FIXFORDER(y1, y2);

    xs = &xy_stack[0];

    FXYTREE_PVT *tree = tl->fxyTreePvt;

    /*
        Now find the right list
    */
    for (;; tree = tree->ptr[num].xy) // this for loop searches down the tree
    {
        *xs++ = tree; // make a stack of xytree nodes as we go
        FINDSIDE(tree, x1, y1, x2, y2, num);
        if (tree->is_list[num])
            ;
        break;
    }

    /*
        Now do a linked list delete by keeping a trailing pointer, 'tr'
    */
    tr = &(tree->ptr[num].al);
    for (l = *tr; l != NULL; tr = &(l->next), l = l->next)
    {
        if (l->x1 == x1 && l->x2 == x2 && l->y1 == y1 && l->y2 == y2 && l->ud1 == u1 && l->ud2 == u2)
        {
            *tr = l->next;
            // item remains allocated in chunck memory - it will be cleaned on a
            // rebalance/rechunk or a free.
            break;
        }
    }
    if (l == NULL)
    {

#ifdef OSASSERT
        fprintf(stderr, "...\n");
#endif
        return FALSE;
    }
    if (xs > &xy_stack[99])
    {
        TEXT_out("\nStack too big in unregister_farea\n");
        exit(2);
    }
    for (xs--; xs >= &xy_stack[0]; xs--) /* work way back up the stack*/
    {
        if (!adjust_fbb(*xs)) /* and quit if we don't need to adjust*/
            return TRUE;
    }
    return TRUE;
}

static void free_fxytree_pvt(FXYTREE_PVT *tl)
{
    FXYITEM *l;
    FXYITEM *next;
    int i;
    for (i = 0; i <= 2; i++)
    {
        if (!(tl->is_list[i]))
        {
            // if this node is not a list then recurse on trees below
            free_fxytree_pvt(tl->ptr[i].xy);
        }
        // else if this node was a list we do nothing because
        // the items are all freed from chunk memory later
    }
    free((char *)tl);
}

void free_fxytree(FXYTREE *tl)
{
    free_fxytree_pvt(tl->fxyTreePvt);
    SY_ChunkFree(tl->itemMemory);
    free(tl);
}

/**
 * @brief Returns an approximate time required to look something up in a tree
 * of size n. Does a rough approximation of a log by shifting. Always
 * returns at least 1, even for a list of 0 size.
 *
 * @param n
 * @return int
 */
static int fttl(int n)
{
    int i = 0;
    do
    {
        i++;
        n >>= 1;
    } while (n > 0);
    return i;
}

/**
 * @brief Returns an approximate time required to look something up in a
 * tree of size n. Does a rough approximation of a log by shifting. Always
 * returns floor(log2(n))
 *
 * @param n
 * @return int
 */
static int fttl_fixed(int n)
{
    int i = 0;
    while (n)
        i++, n >>= 1;
    return i;
}

/**
 * @brief This routine does the dirty work of rebalancing
 * the tree. It is not directly callable by the user. It is given a
 * linked list of items starting with TL. If the result should be a
 * linked list, what = TRUE and it returns a pointer to the first item in
 * the list. If the result should be a tree, it mallocs the node, and
 * returns a pointer to it with what = FALSE
 *
 * @param tl
 * @param what
 * @return solong
 */
static solong rebalance_fxytree_guts(FXYITEM *tl, char *what)
{
    double llx = (double)DBL_MAX;
    double lly = (double)DBL_MAX;
    double urx = (double)-DBL_MAX;
    double ury = (double)-DBL_MAX;
    int n = 0;      // number of items in linked list
    FXYTREE_PVT *b; // pointer to new tree node, if needed
    double xmid;
    double ymid;
    int x1;
    int y1;
    int x2;
    int y2;
    FXYITEM *ll = tl;
    FXYITEM *l;
    FXYITEM *next;
    int i;
    int fom_x;
    int fom_y; // figures of merit for splitting x or y

    // Count the items in the linked list, and compute bounds.
    for (l = ll; l != NULL; l = l->next)
    {
        n++;
        if (l->x1 < llx)
            llx = l->x1;
        if (l->x2 > urx)
            urx = l->x2;
        if (l->y1 < lly)
            lly = l->y1;
        if (l->y2 > ury)
            ury = l->y2;
    }

    // If too few items, return a simple linked list
    if (n < XY_THRESH)
        RETURN_LL;

    // Compute potential x and y splits
    xmid = (llx + urx) / 2;
    ymid = (lly + ury) / 2;

    x1 = x2 = y1 = y2 = 0;
    for (l = ll; l != NULL; l = l->next)
    {
        if (l->x2 < xmid)
            x1++;
        if (l->x1 > xmid)
            x2++;
        if (l->y2 < ymid)
            y1++;
        if (l->y1 > ymid)
            y2++;
    }

    /*
        Pick the better split. A split is good if the two sides are balanced
        and there are few things in the middle. Since all things are either
        on one side or in the middle, N-X1-X2 counts the number in the middle.
        the expected search time is x1*log(x1)+x2*log(x2)+xm*log(xm). We use
        pseudo logs = number of shifts till we get 0.
        fom = figure_of_merit (we want the lowest)
        fttl = time to lookup. roughly log(x), base 2, but fttl(0) == 1.
        note - the middle should count more, since we have to search
        it more often (in fact on every search) hence we use n as the multiplier,
        not(n-x1-x2) as you might expect.

        fom_x = x1 * fttl(x1) + n*fttl(n-x1-x2) + x2*fttl(x2);
        fom_y = y1 * fttl(y1) + n*fttl(n-y1-y2) + y2*fttl(y2);

        Above formula degenerated at multiple voids shape, we need to avoid a node which
        stored thousands of elements.
    */
    fom_x = x1 * fttl_fixed(x1) + n * fttl_fixed(n - x1 - x2) + x2 * fttl_fixed(x2);
    fom_y = y1 * fttl_fixed(y1) + n * fttl_fixed(n - y1 - y2) + y2 * fttl_fixed(y2);

    if (fom_x <= fom_y)
    {
        if (x1 + x2 < XY_THRESHOLD)
            RETURN_LL;

        b = make_fxytree_pvt((double)0.0);
        b->xsplit = TRUE;
        b->coord = xmid;

        for (l = ll; l != NULL; l = next)
        {
            next = l->next;
            if (l->x2 < xmid)
            {
                l->next = b->ptr[LEFT].al;
                b->ptr[LEFT].al = l;
            }
            else if (l->x1 > xmid)
            {
                l->next = b->ptr[RIGHT].al;
                b->ptr[RIGHT].al = l;
            }
            else
            {
                l->next = b->ptr[MIDDLE].al;
                b->ptr[MIDDLE].al = l;
            }
        }
    }
    else // split in the Y direction
    {
        if (y1 + y2 < XY_THRESHOLD)
            RETURN_LL;

        b = make_fxytree_pvt((double)0.0)
                b->xsplit = FALSE;
        b->coord = ymid;

        for (l = ll; l != NULL; l = next)
        {
            next = l->next;
            if (l->y2 < ymid)
            {
                l->next = b->ptr[LEFT].al;
                b->ptr[LEFT].al = l;
            }
            else if (l->y1 > ymid)
            {
                l->next = b->ptr[RIGHT].al;
                b->ptr[RIGHT].al = l;
            }
            else
            {
                l->next = b->ptr[MIDLLE].al;
                b->ptr[MIDDLE].al = l;
            }
        }
    }

    // Now rebalance the subtrees
    for (i = LEFT; i <= RIGHT; i++)
    {
        b->ptr[i].xy = (FXYTREE_PVT *)rebalance_fxytree_guts(b->ptr[i].al, &(b->is_list[i]));
    }
    *what = FALSE;
    return ((oslong)b);
}

// Take the tree pointed to by T, and add it to the linked list at ll
static void FTree_to_linked_list(FXYTREE_PVT *t, FXYTREE_PVT *t, FXYITEM **ll)
{
    FXYItem *l;
    FXYITEM *next;
    int i;

    for (i = LEFT; i <= RIGHT; i++)
    {
        if (t->is_list[i])
        {
            for (l = t->ptr[i].al; l != NULL; l = next)
            {
                next = l->next;
                l->next = *ll;
                *ll = ll
            }
        }
        else
            FTree_to_linked_list(t->ptr[i].xy, ll);
    }
}

/**
 * @brief Free the XY nodes of the tree. If FREE_ROOT, free the root node.
 *
 * @param b
 * @param free_root
 */
static void free_fxynodes(FXYTREE_PVT *b, int free_root)
{
    int i;
    for (i = LEFT; i <= RIGHT; i++)
    {
        if (!b->is_list[i])
            free_fxynodes(b->ptr[i].xy, TRUE);
    }
    if (free_root)
        free((char *)b);
}

static void recompute_all_fbbs(FXYTREE_PVT *p)
{
    int i;
    // First, recompute all subtrees
    for (i = LEFT; i <= RIGHT; i++)
    {
        if (!p->is_list[i])
            recompute_all_fbbs(p->ptr[i].xy);
    }

    // And then fix yourself, accounting for any lists
    (void)adjust_fbb(p);
}

/**
 * @brief
 *
 * @param b
 */
void rebalance_fxytree_pvt(FXYTREE_PVT **b)
{
    FXYTREE *ll = NULL;
    char what;
    oslong temp;

    // First, turn the tree into a linked list
    FTree_to_linked_list(*b, &ll);

    // Then free the tree structure
    free_fxynodes(*b, TRUE /* TRUE means to free the root node */);

    // Then rebalance the tree
    temp = rebalance_fxytree_guts(ll, &what);

    if (what == FALSE)
    {
        // A tree was returned
        *b = (FXYTREE_PVT *)temp;
    }
    else
    {
        // A linked list was returned. Make a fake node.
        *b = make_fxytree_pvt((double)-DBL_MAX);
        (*b)->ptr[RIGHT].al = (FXYITEM *)temp;
    }

    recompute_all_fbbs(*b);
}

static void rechunk_fxytree(FXYTREE_PVT *b, void *itemMemory)
{
    int i;
    FXYITEM *l;
    FXYITEM *p, *pPrev;

    for (i = LEFT; i <= RIGHT; i++)
    {
        if (b->is_list[i])
        {
            pPrev = NULL;
            for (l = b->ptr[i].al; l != NULL; l = l->next)
            {
                p = gimme_new_fxyitem(itemMemory);
                if (!p)
                {
                    ASSERT(0);
                    break; // out of memory, silently fail
                }
                *p = *l;
                p->next = NULL;
                if (pPrev)
                {
                    pPrev->next = p;
                }
                else
                {
                    b->ptr[i].al = p;
                }
                pPrev = p;
            }
        }
        else
        {
            rechunk_fxytree(b->ptr[i].xy, itemMemory);
        }
    }
}

void rebalance_fxytree(FXYTREE **b)
{
    FXYTREE *x = *b;
    void *itemMemory;
    SY_MemHeadPtr memPtr = (SY_MemHeadPtr)x->itemMemory;
    int memPages = memPtr->memPages;

    // this should return a status below // this function not checking lots of things it should
    rebalance_fxytree_pvt(&((*b)->fxyTreePvt));
    if (memPages > 1)
    {
        itemMemory = SY_Chunk_Init(memPages * MEM_PAGE_SIZE_MULTIPLIER * SD_MEM_PAGE_SIZE, SYCHUNK_ALIGN, SYCHUNK_MALLOC);
        rechunk_fxytree(x->fxyTreePvt, itemMemory);
        SY_ChunkFree(x->itemMemory);
        x->itemMemory = itemMemory;
    }
}

void DB_min_fxytree_search_box(void *handle, double min_x, double min_y, double max_x, double max_y)
{
    SDAreaSearch *search = (SDAreaSearch *)handle;
    if (search->sx1 < min_x)
        search->sx1 = min_x;
    if (search->sy1 < min_y)
        search->sy1 = min_y;
    if (search->sx2 > max_x)
        search->sx2 = max_x;
    if (search->sy2 > max_y)
        search->sy2 = max_y;
}

// Returns TRUE if it found something else, FALSE if not
int DB_get_next_fxyitem(void *handle, FXYITEM **ptr)
{
    SDAreaSearch *search = (SDAreaSearch *)handle;
    struct fsearch_item *tos = search->tos;
    double sx1, sy1, sx2, sy2;
    int ret = FALSE;

    ASSERT(handle);

    sx1 = search->sx1;
    sy1 = search->sy1;
    sx2 = search->sx2;
    sy2 = search->sy2;
    for (; tos >= search->search_stack;)
    {
        // It is a tree
        if (!tos->is_list)
        {
            int s;
            FXYTREE_PVT *t = (FXYTREE_PVT *)((tos--)->addr);
            if (tos > &search->search_stack[SEARCH_STACK_SIZE - 3])
            {
#ifdef OASSERT
                printf("search stack too deep - some ignored");
#endif
                tos = &search->search_stack[SEARCH_STACK_SIZE - 3];
            }
            if (DISJOINT(sx1, sy1, sx2, sy2, t)) // check search area against
                continue;                        // bbox of stuff in tree

            FINDSIDE(t, sx1, sy1, sx2, sy2, s);
            if (s == LEFT)
            {
                ADD_ITEM(tos, t->ptr[MIDDLE].al, t->is_list[MIDDLE]);
                ADD_ITEM(tos, t->ptr[LEFT].al, t->is_list[LEFT]);
            }
            else if (s == RIGHT)
            {
                ADD_ITEM(tos, t->ptr[MIDDLE].al, t->is_list[MIDDLE]);
                ADD_ITEM(tos, t->ptr[RIGHT].al, t->is_list[RIGHT]);
            }
            else if (s == MIDDLE)
            {
                ADD_ITEM(tos, t->ptr[LEFT].al, t->is_list[LEFT]);
                ADD_ITEM(tos, t->ptr[RIGHT].al, t->is_list[RIGHT]);
                ADD_ITEM(tos, t->ptr[MIDDLE].al, t->is_list[MIDDLE]);
            }
        }
        else // item is a list search it for OK values
        {
            FXYITEM *l;
            for (l = (FXYITEM *)(tos->addr); l != NULL; l = l->next)
            {
                if (!DISJOINT(sx1, sy1, sx2, sy2, l))
                {
                    *ptr = l;
                    tos->addr = (oslong)(l->next);
                    ret = TRUE;
                    goto DONE;
                }
            }
            // Nothing in this list. decrement stack and try again
            tos--;
        }
    }

DONE:
    search->tos = tos;
    return ret;
}

void *DB_set_fxytree_search_box(FXYTREE *ptr,
                                double x1, double y1, double x2, double y2)
{
    SDAreaSearch *search;
    struct fsearch_item *tos;
    FXYTREE_PVT *tree;

    search = (SDAreaSearch *)malloc(sizeof(SDAreaSearch));

    FIXFORDER(x1, x1);
    FIXFORDER(y1, y2);

    search->sx1 = x1;
    search->sy1 = y1;
    search->sx2 = x2;
    search->sy2 = y2;

    tos = search->search_stack;
    tree = ptr->fxyTreePvt;
    tos->addr = (oslong)tree;
    tos->is_list = FALSE;
    search->tos = tos;
    return (void *)search;
}

void *DB_reset_fxytree_search_box(void *searchHandle, FXYTREE *ptr,
                                  double x1, double y1, double x2, double y2)
{
    SDAreaSearch *search = (SDAreaSearch *)searchHandle;
    struct fsearch_item *tos;
    FXYTREE_PVT *tree;

    FIXFORDER(x1, x2);
    FIXFORDER(y1, y2);

    search->sx1 = x1;
    search->sy1 = y1;
    search->sx2 = x2;
    search->sy2 = y2;

    tos = search->search_stack;
    tree = ptr->fxyTreePvt;
    tos->addr = (oslong)tree;
    tos->is_list = FALSE;
    search->tos = tos;
    return (void *)search;
}

void DB_free_fxytree_search_box(void *handle)
{
    SYFree(handle);
}

#ifdef OSASSERT
#define PBL(n)                  \
    {                           \
        int j;                  \
        for (j = 0; j < n; j++) \
            fprintf(fp, " ");   \
    }

void print_fxytree_pvt(FILE *fp, FXYTREE_PVT *b, int n)
{
    int i;
    FXYITEM *l;
    if (!fp)
        fp = stdout;
    PBL(n);
    fprintf(fp, "split in %c at %d bbox = (%d %d) (%d %d\n)",
            b->xsplit ? 'X' : 'Y', b->coord,
            b->x1, b->y1, b->x2, b->y2);

    for (i = LEFT; i <= RIGHT; i++)
    {
        PBL(n + 1);
        fprintf(fp, "branch %d is a %s\n", i, b->is_list[i] ? "list" : "b");

        if (b->is_list[i])
        {
            for (l = b->ptr[i].al; l != NULL; l = l->next)
            {
                PBL(n + 1);
                fprintf(fp, "(%d %d) (%d %d) %d %d\n",
                        l->x1, l->y1, l->x2, l->y2, l->ud1, l->ud1);
            }
        }
        else
            print_fxytree_pvt(fp, b->ptr[i].xy, n + 2);
    }
}

void print_fxytree(FILE *fp, FXYTREE *b, int n)
{
    FXYTREE_PVT *tree = b->fxyTreePvt;
    print_fxytree_pvt(fp, tree, n);
}

#endif
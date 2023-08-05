# Dynamic Memory Allocator
Designed a dynamic memory allocator written in C. The goal was to implement custom functions from the C standard library including 
malloc, free, realloc, and memalign. My contributions include all of the functions in the sfmm.c file which follows the structure 
described in sfmm.h.

This project was the third homework assignment for CSE 320 at Stony Brook University during the spring 2023 semester. Here is the
information given with the assignment.

# Overview

You will create an allocator for the x86-64 architecture with the following features:

- Free lists segregated by size class, using first-fit policy within each size class,
  augmented with a set of "quick lists" holding small blocks segregated by size.
- Immediate coalescing of large blocks on free with adjacent free blocks;
  delayed coalescing on free of small blocks.
- Boundary tags to support efficient coalescing, with footer optimization that allows
    footers to be omitted from allocated blocks.
- Block splitting without creating splinters.
- Allocated blocks aligned to "single memory row" (8-byte) boundaries.
- Free lists maintained using **last in first out (LIFO)** discipline.
- Use of a prologue and epilogue to achieve required alignment and avoid edge cases
    at the end of the heap.

You will implement your own versions of the **malloc**, **realloc**,
**free**, and **memalign** functions.

You will use existing Criterion unit tests and write your own to help debug
your implementation.

## Free List Management Policy

Your allocator **MUST** use the following scheme to manage free blocks:
Free blocks will be stored in a fixed array of `NUM_FREE_LISTS` free lists,
segregated by size class (see **Chapter 9.9.14 Page 863** for a discussion
of segregated free lists).
Each individual free list will be organized as a **circular, doubly linked list**
(more information below).
The size classes are based on a power-of-two geometric sequence (1, 2, 4, 8, 16, ...),
according to the following scheme:
The first free list (at index 0) holds blocks of the minimum size `M`
(where `M = 32` for this assignment).
The second list (at index 1) holds blocks of size `(M, 2M]`.
The third list (at index 2) holds blocks of size `(2M, 4M]`.
The fourth list holds blocks whose size is in the interval `(4M, 8M]`.
The fifth list holds blocks whose size is in the interval `(8M, 16M]`,
and so on.  This pattern continues up to the interval `(128M, 256M]`,
and then the last list (at index `NUM_FREE_LISTS-1`; *i.e.* 9)
holds blocks of size greater than `256M`.
Allocation requests will be satisfied by searching the free lists in increasing
order of size class.

## Quick Lists

Besides the main free lists, you are also to use additional "quick lists" as a temporary
repository for recently freed small blocks.  There are a fixed number of quick lists,
which are organized as singly linked lists accessed in LIFO fashion.  Each quick lists
holds small blocks of one particular size.  The first quick list holds blocks of the
minimum size (32 bytes).
The second quick list holds blocks of the minimum size plus the alignment size
(32+8 = 40 bytes).  This third quick list holds blocks of size 32+8+8 = 48 bytes,
and so on.  When a small block is freed, it is inserted at the front of the corresponding
quick list, where it can quickly be found to satisfy a subsequent request for a block
of that same size.  The capacity of each quick list is limited; if insertion of a block
would exceed the capacity of the quick list, then the list is "flushed" and the existing
blocks in the quick list are removed from the quick list and added to the main free list,
after coalescing, if possible.

## Block Placement Policy

When allocating memory, use a **segregated fits policy**, modified by the use of quick lists
as follows.  When an allocation request is received, the quick list containing blocks of the
appropriate size is first checked to try to quickly obtain a block of exactly the right size.
If there is no quick list of that size (quick lists are only maintained for a fixed set of
the smallest block sizes), or if there is a quick list but it is empty, then the request will
be satisfied from the main free lists.

Satisfying a request from the main free lists is accomplished as follows:
First, the smallest size class that is sufficiently large to satisfy the request
is determined.  The free lists are then searched, starting from the list for the
determined size class and continuing in increasing order of size, until a nonempty
list is found.  The request is then satisfied by the first block in that list
that is sufficiently large; *i.e.* a **first-fit policy**
(discussed in **Chapter 9.9.7 Page 849**) is applied within each individual free list.

If there is no exact match for an allocation request in the quick lists, and there
is no block in the main free lists that is large enough to satisfy the allocation request,
`sf_mem_grow` should be called to extend the heap by an additional page of memory.
After coalescing this page with any free block that immediately precedes it, you should
attempt to use the resulting block of memory to satisfy the allocation request;
splitting it if it is too large and no splinter would result.  If the block of
memory is still not large enough, another call to `sf_mem_grow` should be made;
continuing to grow the heap until either a large enough block is obtained or the return
value from `sf_mem_grow` indicates that there is no more memory.

As discussed in the book, segregated free lists allow the allocator to approximate a
best-fit policy, with lower overhead than would be the case if an exact best-fit policy
were implemented.  The rationale for the use of quick lists is that when a small block
are freed, it is likely that there will soon be another allocation request for a block
of that same size.  By putting the block in a quick list, it can be re-used for such
a request without the overhead of coalescing and/or splitting that would be required
if the block were inserted back into the main pool.

> :thinking:  Here is an example of determining the block size required to satisfy
> a particular requested payload size.  Suppose the requested size is 25 bytes.
> An additional 8 bytes will be required to store the block header, which must always
> be present.  This means that a block of at least 33 bytes must be used, however due
> to alignment requirements this has to be rounded up to the next multiple of the
> alignment size.  If the alignment size were 8 bytes (which is what we are using for
> this assignment), then a block of at least 40 bytes would have to be used.
> As a result, there would be 7 bytes of "padding" at the end of the payload area,
> which contributes to internal fragmentation.
> Besides the header, when the block is free it is also necessary to store a footer,
> as well and next and previous links for the freelist.
> These will take an additional 24 bytes of space, however when the block is free there
> is no payload so the payload area can be used to store this information, assuming that
> the payload area is big enough in the first place.  But the payload area is 32 bytes
> (25 bytes plus 7 bytes of padding), which is certainly bigger than 24 bytes,
> so a block of total size 40 would be fine.
> Note that a block cannot be smaller than 32 bytes, as there there would not then
> be enough space to store the header, footer, and freelist links when the block is free.

## Splitting Blocks & Splinters

Your allocator must split blocks at allocation time to reduce the amount of
internal fragmentation.  Details about this feature can be found in **Chapter 9.9.8 Page 849**.
Due to alignment and overhead constraints, there will be a minimum useful block size
that the allocator can support.  **For this assignment, pointers returned by the allocator
in response to allocation requests are required to be aligned to 8-byte boundaries**;
*i.e.* the pointers returned will be addresses that are multiples of 2^3.
The 8-byte alignment requirement implies that the minimum block size for your allocator
will be 32 bytes.  No "splinters" of smaller size than this are ever to be created.
If splitting a block to be allocated would result in a splinter, then the block should
not be split; rather, the block should be used as-is to satisfy the allocation request
(*i.e.*, you will "over-allocate" by issuing a block slightly larger than that required).

> :thinking: How do the alignment and overhead requirements constrain the minimum block size?
> As you read more details about the format of a block header, block footer, and alignment requirements,
> you should try to answer this question.

## Freeing a Block

When a block is freed, if it is a small block it is inserted at the front of the quick list of the
appropriate size.  Blocks in the quick lists are free, but the allocation bit remains set in
the header to prevent them from being coalesced with adjacent blocks.  In addition, there is a
separate "in quick list" bit in the block header that is set for blocks in the quick lists,
to allow them to be readily distinguished from blocks that are actually allocated.
To avoid arbitrary growth of the quick lists, the capacity of each is limited to `QUICK_LIST_MAX` blocks.
If an attempt is made to insert a block into a quick list that is already at capacity,
the quick list is *flushed* by removing each of the blocks it currently contains and adding
them back into the main free lists, coalescing them with any adjacent free blocks as described
below.  After flushing the quick list, the block currently being freed is inserted into the
now-empty list, leaving just one block in that list.

When a block is freed and added into the main free lists, an attempt should first be made to
**coalesce** the block with any free block that immediately precedes or follows it in the heap.
(See **Chapter 9.9.10 Page 850** for a discussion of the coalescing procedure.)
Once the block has been coalesced, it should be inserted at the **front** of the free
list for the appropriate size class (based on the size after coalescing).
The reason for performing coalescing is to combat the external fragmentation
that would otherwise result due to the splitting of blocks upon allocation.
Note that blocks inserted into quick lists are not immediately coalesced; they are only
coalesced at such later time as the quick list is flushed and the blocks are moved into the
main free lists.  This is an example of a "deferred coalescing" strategy.

## Block Headers & Footers

In **Chapter 9.9.6 Page 847 Figure 9.35**, a block header is defined as 2 words
(32 bits) to hold the block size and allocated bit. In this assignment, the header
will be 4 words (i.e. 64 bits or 1 memory row). The header fields will be similar
to those in the textbook but you will maintain an extra bit for recording whether
or not the previous block is allocated, and an extra bit for recording whether or not
the block is currently in a quick list.
Each free block will also have a footer, which occupies the last memory row of the block.
The footer of a free block contains exactly the same information as the header.
In an allocated block, the footer is not present, and the space that it would otherwise
occupy may be used for payload.

**Block Header Format:**
```c
    +------------------------------------------------------------+--------+---------+---------+ <- header
    |                                       block_size           |in qklst|prv alloc|  alloc  |
    |                                  (3 LSB's implicitly 0)    | (0/1)  |  (0/1)  |  (0/1)  | 
    |                                        (1 row)             | 1 bit  |  1 bit  |  1 bit  |
    +------------------------------------------------------------+--------+---------+---------+ <- (aligned)
```

- The `block_size` field gives the number of bytes for the **entire** block (including header/footer,
  payload, and padding).  It occupies the entire 64 bits of the block header or footer,
  except that the three least-significant bits of the block size, which would normally always
  be zero due to alignment requirements, are used to store additional information.
  This means that these bits have to be masked when retrieving the block size from the header and
  when the block size is stored in the header the previously existing values of these bits have
  to be preserved.
- The `alloc` bit (bit 0, mask 0x1) is a boolean. It is 1 if the block is allocated and 0 if it is free.
- The `prev_alloc` (bit 1, mask 0x2) is also a boolean. It is 1 if the **immediately preceding** block
  in the heap is allocated and 0 if it is not.
- The `in_qklst` (bit 2, mask 0x4) is also a boolean. It is 1 if the block is currently in a quick list,
  and 0 if it is not.  Note that if this bit is a 1, then the `alloc` bit will also be a 1.

Each free block will also have a footer, which occupies the last memory row of the block.
The footer of a free block (including a block in a quick list) must contain exactly the
same information as the header.  In an allocated block, the footer will not be present,
and the space that it would otherwise occupy may be used for payload.

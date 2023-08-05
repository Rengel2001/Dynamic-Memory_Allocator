/**
 * Do not submit your assignment with a main function in this file.
 * If you submit with a main function in this file, you will get a zero.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include "debug.h"
#include "sfmm.h"

#define WORD_SIZE 2 //size of a word in bytes
#define BLOCK_HDR_SIZE 8//size of the block header in bytes
#define MIN_BLOCK_SIZE 32 //minimum block size in bytes

//packs the size and 3 alloc bits into a value to be stored in the header
#define PACK(size, in_qklst, prv_alloc, alloc) ((size) | (in_qklst) | (prv_alloc) | (alloc))

//read and write a word at a given address a
#define GET(a) (*(unsigned int *)(a))
#define PUT(a, value) (*(unsigned int *)(a) = (value))

//read size and allocate fields of block header
#define GET_SIZE(a) (GET(a) & ~0x7)
#define GET_ALLOC(a) (GET(a) & THIS_BLOCK_ALLOCATED)
#define GET_PRV_ALLOC(a) (GET(a) & PREV_BLOCK_ALLOCATED)
#define GET_IN_QKLST(a) (GET(a) & IN_QUICK_LIST)

//given block pointer bp, get the address of hdr and ftr
//note that hdrptr finds the header, given a ptr to a blocks payload
#define HDRPTR(bp) ((char*)(bp) - BLOCK_HDR_SIZE)
//note that ftrptr finds the footer, given a ptr to the head of a block
#define FTRPTR(bp) ((char*)(bp) + GET_SIZE(bp) - BLOCK_HDR_SIZE)

//given block pointer bp get address of next and previous blocks
#define NEXT_BLKPTR(bp) ((char*)(bp) + GET_SIZE((char*)(bp)))
#define PREV_BLKPTR(bp) ((char*)(bp) - GET_SIZE(((char*)(bp) - BLOCK_HDR_SIZE)))

static int heap_flag = 0;

void *sf_init(){
    //start by using sf_mem_grow to create the initial heap
    void *page_ptr;
    page_ptr = sf_mem_grow();

    //check if there was error creating page
    if (page_ptr == NULL)
        return NULL;

    //set up prologue
    sf_block *prologue_block = page_ptr;
    PUT(prologue_block, PACK(MIN_BLOCK_SIZE, 0, 0, THIS_BLOCK_ALLOCATED));

    //to calculate the size of the free block we subtract 40 because we do not
    //want to count the 32 bytes of the prologue or the 8 bytes of padding
    //we do not count the epilogue because the epilogue is size of 0 bytes
    int size;
    size = PAGE_SZ - MIN_BLOCK_SIZE - BLOCK_HDR_SIZE;

    //set block pointer bp to be directly after the prologue
    page_ptr = sf_mem_start() + MIN_BLOCK_SIZE;
    sf_block *bp = page_ptr;

    //give the free block a header
    bp->header = PACK(size, 0, 0, 0);

    //set the footer address to be the last row of the block
    sf_footer *ftr = (void*)(FTRPTR(bp));

    //give the free block a footer
    PUT(ftr, PACK(size, 0, 0, 0));

    //set up epilogue
    page_ptr = sf_mem_end() - BLOCK_HDR_SIZE;
    sf_block *epilogue_block = page_ptr;
    PUT(epilogue_block, PACK(0, 0, 0, THIS_BLOCK_ALLOCATED));

    //initialize free lists
    int i;
    for (i = 0; i < NUM_FREE_LISTS; i++)
    {
        sf_free_list_heads[i].body.links.next = &sf_free_list_heads[i];
        sf_free_list_heads[i].body.links.prev = &sf_free_list_heads[i];
    }

    //calculate the correct sentinel node to link to
    //we know the size of this free block is 4096-40=4056
    //this means we will be putting this block into main free list
    //at index 7 because 4056 < 4096 and 4056 > 2048.
    sf_block *sentinel;
    sentinel = &sf_free_list_heads[7];

    //set the correct links for the block
    bp->body.links.next = sentinel;
    bp->body.links.prev = sentinel;

    //set the correct links for the sentinel
    sf_free_list_heads[7].body.links.next = bp;
    sf_free_list_heads[7].body.links.prev = bp;

    //return bp that points to header of the block
    return bp;
}

void place(sf_block *bp, size_t size){
    //this function takes a block pointer bp and
    //places a newly allocated block into memory
    bp->header = PACK(size, 0, PREV_BLOCK_ALLOCATED, THIS_BLOCK_ALLOCATED);
}

int get_free_list_index(size_t size){
    int i, j, mult, lower, upper;
    if (size == MIN_BLOCK_SIZE)
        i = 0;
    else{
        //calculate lower bound value of the ith index range of the free list array
        for (i = 1; i < NUM_FREE_LISTS-1; i++){
            mult = 1;
            for (j = 1; j < i; j++)
                mult = mult * 2;
            lower = mult * MIN_BLOCK_SIZE;

            //calculate upper bound value of the ith index range of the free list array
            mult = 1;
            for (j = 1; j <= i; j++)
                mult = mult * 2;
            upper = mult * MIN_BLOCK_SIZE;

            //here we check if the size given is within the range of the ith index of the free list array
            if ((size > lower) && (size <= upper))
                break;
        }
    }
    return i;
}

void *find_fit_free_list(size_t asize){
    //this function searches through each main free list and
    //returns a block pointer if there is a block in a list that matches the needed size
    //returns null if there is not a block of sufficient space in any list

    int i, j;
    i = get_free_list_index(asize);
    //by this point, i is the index of the first free list that would be able to satisfy the request

    for (j = i; j < NUM_FREE_LISTS; j++){

        //if the list is nonempty
        if ( ! ( ((sf_free_list_heads[j].body.links.next) == (&sf_free_list_heads[j])) && ((sf_free_list_heads[j].body.links.prev) == (&sf_free_list_heads[j])))){

            //set the first node after the sentinel
            sf_block *current_node;
            current_node = (sf_free_list_heads[j].body.links.next);

            //set sentinel node
            sf_block *sentinel;
            sentinel = &(sf_free_list_heads[j]);

            //check all other nodes after the first node and return if we find a large enough block
            while (current_node != sentinel){
                int block_size;
                block_size = GET_SIZE(current_node);

                if (asize <= block_size){
                    return current_node;
                }
                current_node = (current_node->body.links.next);

            }
        }
    }
    return NULL;
}


sf_block *find_fit_quick_list(size_t size){
    //this function searches through each quick list, and
    //returns a block pointer if there is a block in the quick list that matches the given size
    //returns null if there is not a block of sufficient space in the quick list

    int i;
    //count through the array of quick lists
    for (i = 0; i < NUM_QUICK_LISTS; i++){
        //if the size matches the number of bytes in the current index of sf_quick_lists
        if (MIN_BLOCK_SIZE + (i * BLOCK_HDR_SIZE) == size){
            //check if the head of that index is NULL
            if ((sf_quick_lists[i].first == NULL) || (sf_quick_lists[i].length == QUICK_LIST_MAX)){
                return NULL;
            }
                //if the head of that index is NULL then there is no block that matches the needed size
            else
                return sf_quick_lists[i].first;
                //if the head is not null, then return the block pointer of that block
        }
    }
    return NULL;
}

void add_to_free_list(sf_block *bp){
    //takes a block pointer and adds it to the respective free list
    //assuming that the bp is a pointer to a free block

    //add to free list
    //to do this, first determine the size of the block
    int block_size;
    block_size = GET_SIZE(bp);

    //then determine which free list it belongs in and add it to the doubly linked list
    int i;
    i = get_free_list_index(block_size);
    //by this point, i is the index of the free list where we would store the remainder

    //set the links field of the block
    bp->body.links.next = (&sf_free_list_heads[i])->body.links.next;
    bp->body.links.prev = &sf_free_list_heads[i];

    //add i to the doubly linked list at the given index
    ((&sf_free_list_heads[i])->body.links.next)->body.links.prev = bp;
    (&sf_free_list_heads[i])->body.links.next = bp;

}

void delete_from_free_list(int i){
    //deletes the first node of the free list given at the ith index

    //reset sentinel node links
    sf_block *temp;
    temp = sf_free_list_heads[i].body.links.next;
    (&sf_free_list_heads[i])->body.links.next = temp->body.links.next;
    temp = temp->body.links.next;
    temp->body.links.prev = &sf_free_list_heads[i];

}

void *coalesce(sf_block *bp){
    //check next and previous blocks alloc bits
    int prev_alloc_bit, next_alloc_bit;
    prev_alloc_bit = GET_PRV_ALLOC(bp);
    next_alloc_bit = GET_ALLOC(NEXT_BLKPTR(bp));

    //calculate size of the given block
    size_t size;
    size = GET_SIZE(bp);

    int i;
    if (prev_alloc_bit && next_alloc_bit){ //case 1
        bp->header = PACK(size, 0, PREV_BLOCK_ALLOCATED, 0);
        PUT(FTRPTR(bp), PACK(size, 0, PREV_BLOCK_ALLOCATED, 0));
    }
    else if (prev_alloc_bit && !next_alloc_bit){ //case 2
        //get i value of the next block
        i = get_free_list_index(GET_SIZE(NEXT_BLKPTR(bp)));

        //delete a node from the ith free list
        delete_from_free_list(i);

        size = size + GET_SIZE(NEXT_BLKPTR(bp));
        bp->header = PACK(size, 0, PREV_BLOCK_ALLOCATED, 0);
        PUT(FTRPTR(bp), PACK(size, 0, PREV_BLOCK_ALLOCATED, 0));
    }
    else if (!prev_alloc_bit && next_alloc_bit){ //case 3
        //get i value of the previous block
        i = get_free_list_index(GET_SIZE(PREV_BLKPTR(bp)));
        //delete a node from the ith free list
        delete_from_free_list(i);

        size = size + GET_SIZE(PREV_BLKPTR(bp));
        PUT(FTRPTR(bp), PACK(size, 0, PREV_BLOCK_ALLOCATED, 0));
        bp = (sf_block*)PREV_BLKPTR(bp);
        bp->header = PACK(size, 0, PREV_BLOCK_ALLOCATED, 0);
    }
    else{ //case 4
        //get i value of the previous block
        i = get_free_list_index(GET_SIZE(PREV_BLKPTR(bp)));

        //delete a node from the ith free list
        delete_from_free_list(i);

        //get i value of the next block
        i = get_free_list_index(GET_SIZE(NEXT_BLKPTR(bp)));

        //delete a node from the ith free list
        delete_from_free_list(i);

        size = size + GET_SIZE(NEXT_BLKPTR(bp)) + GET_SIZE(FTRPTR(PREV_BLKPTR(bp)));
        PUT(FTRPTR(NEXT_BLKPTR(bp)), PACK(size, 0, PREV_BLOCK_ALLOCATED, 0));
        bp = (sf_block*)PREV_BLKPTR(bp);
        bp->header = PACK(size, 0, PREV_BLOCK_ALLOCATED, 0);
    }

    add_to_free_list(bp);
    return bp;

}

void check_for_splinters_and_place(sf_block * bp, size_t adjusted_size){
    //this function checks if splitting creates splinter, and places blocks accordingly

    size_t current_block_size;
    current_block_size = GET_SIZE(bp);

    //note that current block size is the size of the free block we will use to satisfy the request
    //and adjusted size is the block size needed to fulfill the request
    if ((current_block_size - adjusted_size) >= (MIN_BLOCK_SIZE)){
        //if it will not leave a splinter, then split the block and add the remainder to free list

        //delete a node from the free list of the block we split
        delete_from_free_list(get_free_list_index(GET_SIZE(bp)));

        //place lower part to satisfy allocation request
        place(bp, adjusted_size);

        //store the remainder
        sf_block *remainder;
        remainder = (sf_block*)(NEXT_BLKPTR(bp));

        //place remainder in the next block over
        remainder->header = PACK((current_block_size - adjusted_size), 0, PREV_BLOCK_ALLOCATED, 0);

        //remainder is a free block, so it needs a footer as well
        PUT(FTRPTR(remainder), PACK((current_block_size - adjusted_size), 0, PREV_BLOCK_ALLOCATED, 0));

        //coalesce the free blocks if necessary
        coalesce(remainder);

    }
    else{
        //if it leaves a splinter, then dont split the block and delete from free list
        delete_from_free_list(get_free_list_index(GET_SIZE(bp)));
        place(bp, adjusted_size);
    }
}

void *sf_malloc(size_t size) {
    //check if size is 0
    if (size == 0)
        return NULL;

    //initialize the heap if this is the first call to malloc
    if (heap_flag == 0){
        if (sf_init() == NULL)
            return NULL;
        heap_flag = 1;
    }

    size_t adjusted_size;

    //adjust block size to include overhead and alignment requirements
    if (size < MIN_BLOCK_SIZE - BLOCK_HDR_SIZE )
        adjusted_size = MIN_BLOCK_SIZE; //set to a the minimum block size of 32 bytes
    else
        adjusted_size = BLOCK_HDR_SIZE * (( size + BLOCK_HDR_SIZE + (BLOCK_HDR_SIZE - 1)) / BLOCK_HDR_SIZE);
        //round up to multiple of 8 and add 8 bytes to size

    //check quick lists to see if they contain a block that fits the adjusted size
    sf_block *bp;
    if ((bp = find_fit_quick_list(adjusted_size)) != NULL){
        //if the quick lists have enough space for the new block,

        //place the newly allocated block
        place(bp, adjusted_size);

        //return the payload address
        return (bp->body.payload);

    }

    //if quick lists dont contain enough space for block or if the ql is empty, then check for space in main free lists
    else{
        if ((bp = find_fit_free_list(adjusted_size)) != NULL){
            //if there is a free list with enough space to fit the new block
            check_for_splinters_and_place(bp, adjusted_size);
            return bp->body.payload;
        }
        else{
            //in this case, we were not able to find space in the free lists
            //so we allocate a new page of memory, and attempt to place the block

            void *ptr;
            while ((ptr = sf_mem_grow()) != NULL){
                ptr = (ptr) - BLOCK_HDR_SIZE;

                //set header and footer
                PUT(ptr, PACK(PAGE_SZ, 0, 0, 0));
                PUT(FTRPTR(ptr), PACK(PAGE_SZ, 0, 0, 0));

                //create new epilogue
                void *page_ptr;
                page_ptr = sf_mem_end() - BLOCK_HDR_SIZE;
                sf_block *epilogue_block = page_ptr;
                PUT(epilogue_block, PACK(0, 0, 0, THIS_BLOCK_ALLOCATED));

                coalesce((sf_block*)ptr);

                //if the new free block satisfies malloc request, then return the pointer to payload
                if ((bp = find_fit_free_list(adjusted_size)) != NULL){
                    check_for_splinters_and_place(bp, adjusted_size);
                    return bp->body.payload;
                }
                //if the new free block is not large enough to satisfy request, then delete from free list & extend page
            }
            sf_errno = ENOMEM;
            return NULL;
        }
    }
    return NULL;
}

void sf_free(void *pp) {
    //check if pointer is null
    if (pp == NULL)
        abort();

    //check if pointer is not 8 byte aligned
    if (((uintptr_t)pp & 0x7) != 0)
        abort();

    sf_block *bp;
    bp = ((sf_block*)(((uintptr_t)pp) - BLOCK_HDR_SIZE));

    size_t block_size;
    block_size = GET_SIZE(bp);

    //check if block size < 32
    if (block_size < 32)
        abort();

    //check if block size is not a multiple of 8
    if (block_size % 8 != 0)
        abort();

    //check if header is before the start of the heap or after the end of the heap
    if (((void*)bp < sf_mem_start()) || ((void*)bp > sf_mem_end()))
        abort();

    //check if allocated bit is 0
    if (GET_ALLOC(bp) == 0)
        abort();

    //check if the quick list bit is 1
    if (GET_IN_QKLST(bp) == IN_QUICK_LIST)
        abort();

    //check that the prev_alloc field in the header is 0, and the alloc field of the previous block header is not 0
    if ((GET_PRV_ALLOC(bp) == 0) && (GET_ALLOC(PREV_BLKPTR(bp)) == 1))
        abort();

    //determine if the newly freed block goes in quick list, using a similar algorithm to the fit fit quick list function
    int i, in_qklst;
    in_qklst = 0;
    for (i = 0; i < NUM_QUICK_LISTS; i++){
        //if the size of the block matches the size required in the ith index of sf_quick_lists
        if (MIN_BLOCK_SIZE + (i * BLOCK_HDR_SIZE) == block_size){
            //set in_qklst and break from loop
            in_qklst = IN_QUICK_LIST;
            break;
        }
    }
    //at this point i is the correct index of the quick list array, if its necessary to store in a quick list
    //determine if the previous block is allocated in order to set the prev_alloc bit for the newly freed block
    int prev_alloc_bit;
    prev_alloc_bit =  GET_PRV_ALLOC(bp);

    //set header and footer using put and pack macros
    if (in_qklst){
        //if the block is moved to a quick list, set the qklist bit to true, and the alloc bit to true
        PUT((bp), PACK(block_size, IN_QUICK_LIST, prev_alloc_bit, THIS_BLOCK_ALLOCATED));
        PUT(FTRPTR(bp), PACK(block_size, IN_QUICK_LIST, prev_alloc_bit, THIS_BLOCK_ALLOCATED));
    }
    else{
        //if the block does not belong on the qklst, then qklist bit is 0 and alloc bit is 0
        PUT((bp), PACK(block_size, 0, prev_alloc_bit, 0));
        PUT(FTRPTR(bp), PACK(block_size, 0, prev_alloc_bit, 0));
    }

    //if we determine that the block goes in a quick list
    if (in_qklst == IN_QUICK_LIST){

        //check if the ith list is full
        if (sf_quick_lists[i].length == QUICK_LIST_MAX){

            //if it is full then flush each element of free list, by removing them from quick list and coalescing
            while (sf_quick_lists[i].length > 0){

                //loops through each element in the quick list
                sf_block *temp;
                temp = sf_quick_lists[i].first;
                sf_quick_lists[i].first = (sf_quick_lists[i].first)->body.links.next;

                //note that the coalesce function adds to the free list after coalescing if possible
                sf_quick_lists[i].length--;
                temp->header = temp->header ^ 0x1;
                coalesce(temp);
            }
        }
        //insert the newly freed block into the first element of its respective quick list
        bp->body.links.next = sf_quick_lists[i].first;
        sf_quick_lists[i].first = bp;
        sf_quick_lists[i].length++;
    }
    else{
        //if it doesnt go in quick list simply coalesce the newly freed block, adding it to the respective free list
        coalesce(bp);
    }

}

void *sf_realloc(void *pp, size_t rsize) {
    //check if pointer is null
    if (pp == NULL)
        abort();

    //check if pointer is not 8 byte aligned
    if (((uintptr_t)pp & 0x7) != 0)
        abort();

    sf_block *bp;
    bp = (pp - BLOCK_HDR_SIZE);

    size_t block_size;
    block_size = GET_SIZE(bp);

    //check if block size < 32
    if (block_size < 32)
        abort();

    //check if block size is not a multiple of 8
    if (block_size % 8 != 0)
        abort();

    //check if header is before the start of the heap or after the end of the heap
    if (((void*)bp < sf_mem_start()) || ((void*)bp > sf_mem_end()))
        abort();

    //check if allocated bit is 0
    if (GET_ALLOC(bp) == 0)
        abort();

    //check if the quick list bit is 1
    if (GET_IN_QKLST(bp) == IN_QUICK_LIST)
        abort();

    //check that the prev_alloc field in the header is 0, and the alloc field of the previous block header is not 0
    if ((GET_PRV_ALLOC(bp) == 0) && (GET_ALLOC(PREV_BLKPTR(bp))))
        abort();

    //check if size parameter is 0
    if (rsize == 0){
        sf_free(pp);
        return NULL;
    }

    //reallocating to a larger size
    if (rsize > (GET_SIZE(bp) - BLOCK_HDR_SIZE)){
        //1. Call sf_malloc to obtain a larger block.
        void *new_block;
        new_block = sf_malloc(rsize);

        //Note that if sf_malloc returns NULL, sf_realloc must also return NULL.
        if (new_block == NULL)
            return NULL;

        //2. Call memcpy to copy the data in the block given by the client to the block returned by sf_malloc.
        // Be sure to copy the entire payload area, but no more.
        memcpy(new_block, (bp + BLOCK_HDR_SIZE), rsize);

        //3. Call sf_free on the block given by the client (inserting into a quick list or main freelist and coalescing if required).
        sf_free(pp);

        //4. Return the block given to you by sf_malloc to the client.
        return new_block;
    }

    //reallocating to a smaller size
    if (rsize < (GET_SIZE(bp) - BLOCK_HDR_SIZE)){
        // When reallocating to a smaller size, your allocator must use the block that was passed by the caller. You must attempt
        // to split the returned block. There are two cases for splitting:

        size_t current_payload_size;
        current_payload_size = GET_SIZE(bp) - BLOCK_HDR_SIZE;

        if ((current_payload_size - rsize) < (MIN_BLOCK_SIZE)){
            //1. Splitting the returned block results in a splinter. In this case, do not split the block. Leave the splinter in
            // the block, update the header field if necessary, and return the same block back to the caller.
            return pp;
        }
        else{
            //2. The block can be split without creating a splinter. In this case, split the block and update the block size fields in
            // both headers. Free the remainder block by inserting it into the appropriate free list (after coalescing, if possible --
            // do not insert the remainder block into a quick list). Return a pointer to the payload of the now-smaller block

            //if it will not leave a splinter, then split the block and add the remainder to free list

            //calculate the sizes of the new blocks
            size_t new_size, remainder_size;
            if (rsize < 32){
                new_size = 32;
                remainder_size = GET_SIZE(bp) - 32;
            }
            else{
                new_size = GET_SIZE(bp) >> 1;
                remainder_size = (GET_SIZE(bp) - 8) >> 1;
            }

            //place lower part to satisfy allocation request, reusing similar code to the place function
            place(bp, new_size);
            memcpy(bp->body.payload, pp, rsize);

            //store the remainder
            sf_block *remainder;
            remainder = (sf_block*)(NEXT_BLKPTR(bp));

            //place remainder in the next block over
            remainder->header = PACK((remainder_size), 0, PREV_BLOCK_ALLOCATED, 0);

            //remainder is a free block, so it needs a footer as well
            PUT(FTRPTR(remainder), PACK((remainder_size), 0, PREV_BLOCK_ALLOCATED, 0));

            //coalesce the free blocks if necessary
            coalesce(remainder);

            return pp;
        }

    }

    if (rsize == (GET_SIZE(bp) - BLOCK_HDR_SIZE))
        return pp;

    return NULL;
}

void *sf_memalign(size_t size, size_t align) {
    //check that the align parameter is >= 8
    if (align < 8){
        sf_errno = EINVAL;
        return NULL;
    }

    //check that align parameter is power of 2
    if (!(align && !(align & (align - 1)))){
        sf_errno = EINVAL;
        return NULL;
    }

    //calculate the request size and call sf_malloc
    size_t request_size;
    request_size = size + align + MIN_BLOCK_SIZE + BLOCK_HDR_SIZE;

    //store the return address of malloc in a pointer ptr
    void *ptr;
    ptr = sf_malloc(request_size);

    //check 2 cases:
    //if the normal address of the block satisfies the requested alignment
    if ((size_t)ptr%align == 0){
        return ptr;
    }
    else{
        //find the next address within the block that satisfies 3 conditions
        //1. address satisfies the requested alignment
        //2. has sufficient space after it to hold the request size payload
        //3. has sufficient space before it to split the block, such that the first part is 32 bytes or greater

        //create pointers to the beginning and end of the block
        void *end_of_payload;
        sf_block* block_header;
        block_header = (sf_block*)HDRPTR(ptr);
        end_of_payload = FTRPTR(block_header);

        //loop every address for each increment of 8 bytes between the start of the block payload, and the end
        for (; (uintptr_t)ptr < (uintptr_t)end_of_payload; ptr = ptr + 8){

            //if address satisfies alignment, continue
            if ((size_t)ptr%align == 0){

                //if there is enough space after the address to hold the reqested payload, continue
                if (end_of_payload - ptr >= size){

                    //if there is enough space before address to split the block, continue
                    if ((uintptr_t)ptr - (uintptr_t)block_header  >= MIN_BLOCK_SIZE){

                        //calculate new block size
                        size_t new_size;
                        new_size = (uintptr_t)end_of_payload - (uintptr_t)ptr - BLOCK_HDR_SIZE;

                        //add the new free block in place of the original block
                        size_t adjusted_size;
                        adjusted_size = request_size - new_size;
                        block_header->header = PACK(adjusted_size, 0, PREV_BLOCK_ALLOCATED, 0);
                        PUT(FTRPTR(block_header), PACK(adjusted_size, 0, PREV_BLOCK_ALLOCATED, 0));
                        coalesce(block_header);

                        //add the new used block to the address directly after the newly added free block
                        sf_block *new_block;
                        new_block = (sf_block*) (FTRPTR(block_header) + BLOCK_HDR_SIZE);
                        new_size = new_size + BLOCK_HDR_SIZE;
                        new_block->header = PACK(new_size, 0, 0, THIS_BLOCK_ALLOCATED);
                        check_for_splinters_and_place(new_block, new_size);

                        return ptr;
                    }
                }
            }
        }
    }
    return NULL;
}


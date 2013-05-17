/* vim: set expandtab ts=4 sw=4: */
/*
 * You may redistribute this program and/or modify it under the terms of
 * the GNU General Public License as published by the Free Software Foundation,
 * either version 3 of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */
#include <stdint.h>

#include "exception/Except.h"
#include "memory/BufferAllocator.h"
#include "util/Bits.h"
#include "util/Identity.h"

/**
 * TODO: addOnFreeJob adds a job which is only run when the root allocator is freed
 *       and it needs to be run when the allocator which called it, or any of that allocator's
 *       ancestors is freed, not just the root.
 */

/* Define alignment as the size of a pointer which is usually 4 or 8 bytes. */
#define ALIGNMENT sizeof(char*)

struct Job {
    struct Allocator_OnFreeJob generic;
    void (* callback)(void* callbackContext);
    void* callbackContext;
    struct Allocator* alloc;
    struct Job* next;
    Identity
};

/** Internal state for Allocator. */
struct BufferAllocator {
    struct Allocator generic;
    char* basePointer;
    char* pointer;
    char* const endPointer;
    struct Job* onFree;
    struct Except* onOOM;
    char* file;
    int line;
    Identity
};

/**
 * Get a pointer which is aligned on memory boundries.
 *
 * @param pointer the location where the pointer should be.
 * @param alignedOn how big the word is that the boundry should be aligned on.
 */
#define getAligned(pointer, alignedOn) \
    ((char*) ((uintptr_t)( ((char*)(pointer)) + (alignedOn) - 1) & ~ ((alignedOn) - 1)))

/** @see Allocator_malloc() */
static void* allocatorMalloc(unsigned long length,
                             struct Allocator* allocator,
                             const char* identFile,
                             int identLine)
{
    struct BufferAllocator* context = Identity_cast((struct BufferAllocator*) allocator);

    char* pointer = getAligned(context->pointer, ALIGNMENT);
    char* endOfAlloc = pointer + length;

    if (endOfAlloc >= context->endPointer) {
        Except_raise(context->onOOM, -1, "BufferAllocator ran out of memory [%s:%d]",
                     identFile, identLine);
    }

    if (endOfAlloc < context->pointer) {
        Except_raise(context->onOOM, -2, "BufferAllocator integer overflow [%s:%d]",
                     identFile, identLine);
    }

    context->pointer = endOfAlloc;
    return (void*) pointer;
}

/** @see Allocator->calloc() */
static void* allocatorCalloc(unsigned long length,
                             unsigned long count,
                             struct Allocator* allocator,
                             const char* identFile,
                             int identLine)
{
    void* pointer = allocatorMalloc(length * count, allocator, identFile, identLine);
    Bits_memset(pointer, 0, length * count);
    return pointer;
}

/** @see Allocator->clone() */
static void* allocatorClone(unsigned long length,
                            struct Allocator* allocator,
                            const void* toClone,
                            const char* identFile,
                            int identLine)
{
    void* pointer = allocatorMalloc(length, allocator, identFile, identLine);
    Bits_memcpy(pointer, toClone, length);
    return pointer;
}

/** @see Allocator->realloc() */
static void* allocatorRealloc(const void* original,
                              unsigned long length,
                              struct Allocator* allocator,
                              const char* identFile,
                              int identLine)
{
    if (original == NULL) {
        return allocatorMalloc(length, allocator, identFile, identLine);
    }

    // Need to pointer to make sure we dont copy too much.
    struct BufferAllocator* context = Identity_cast((struct BufferAllocator*) allocator);
    char* pointer = context->pointer;
    uint32_t amountToClone = (length < (uint32_t)(pointer - (char*)original))
        ? length
        : (uint32_t)(pointer - (char*)original);

    // The likelyhood of nothing having been allocated since is
    // almost 0 so we will always create a new
    // allocation and copy into it.
    void* newAlloc = allocatorMalloc(length, allocator, identFile, identLine);
    Bits_memcpy(newAlloc, original, amountToClone);
    return newAlloc;
}

/** @see Allocator->free() */
static void freeAllocator(struct Allocator* allocator, const char* identFile, int identLine)
{
    struct BufferAllocator* context = Identity_cast((struct BufferAllocator*) allocator);

    struct Job* job = context->onFree;
    while (job != NULL) {
        job->callback(job->callbackContext);
        job = job->next;
    }

    context->pointer = context->basePointer;
}

static int removeOnFreeJob(struct Allocator_OnFreeJob* toRemove)
{
    struct Job* j = Identity_cast((struct Job*) toRemove);
    struct BufferAllocator* context = Identity_cast((struct BufferAllocator*) j->alloc);
    struct Job** jobPtr = &(context->onFree);
    while (*jobPtr != NULL) {
        if (*jobPtr == j) {
            *jobPtr = (*jobPtr)->next;
            return 0;
        }
        jobPtr = &(*jobPtr)->next;
    }
    return -1;
}

/** @see Allocator->onFree() */
static struct Allocator_OnFreeJob* onFree(void (* callback)(void* callbackContext),
                                          void* callbackContext,
                                          struct Allocator* alloc)
{
    struct BufferAllocator* context = Identity_cast((struct BufferAllocator*) alloc);

    struct Job* newJob = Allocator_clone(alloc, (&(struct Job) {
        .generic = {
            .cancel = removeOnFreeJob
        },
        .callback = callback,
        .callbackContext = callbackContext,
        .alloc = (struct Allocator*) alloc,
    }));

    struct Job* job = context->onFree;
    if (job == NULL) {
        context->onFree = newJob;

    } else {
        while (job->next != NULL) {
            job = job->next;
        }
        job->next = newJob;
    }
    return &newJob->generic;
}

/** @see Allocator_child() */
static struct Allocator* childAllocator(struct Allocator* alloc,
                                        const char* identFile,
                                        int identLine)
{
    Assert_always(!"Unimplemented");
    return alloc;
}

static struct Allocator* adopt(struct Allocator* alloc,
                               struct Allocator* allocB,
                               const char* file,
                               int line)
{
    Assert_always(!"Unimplemented");
    return alloc;
}

/** @see BufferAllocator.h */
struct Allocator* BufferAllocator_newWithIdentity(void* buffer,
                                                  unsigned long length,
                                                  char* file,
                                                  int line)
{
    struct BufferAllocator tempAlloc = {
        .generic = {
            .free = freeAllocator,
            .malloc = allocatorMalloc,
            .calloc = allocatorCalloc,
            .clone = allocatorClone,
            .realloc = allocatorRealloc,
            .child = childAllocator,
            .onFree = onFree,
            .adopt = adopt
        },

        // Align the pointer to do the first write manually.
        .pointer = getAligned(buffer, sizeof(char*)),
        .basePointer = getAligned(buffer, sizeof(char*)),
        .endPointer = ((char*)buffer) + length,
        .file = file,
        .line = line
    };

    if (tempAlloc.endPointer < tempAlloc.pointer) {
        // int64_t overflow.
        return NULL;
    }

    if (length + (char*) buffer < tempAlloc.pointer + sizeof(struct BufferAllocator)) {
        // Not enough space to allocate the context.
        return NULL;
    }

    struct BufferAllocator* alloc = (struct BufferAllocator*) tempAlloc.pointer;
    Bits_memcpyConst(alloc, &tempAlloc, sizeof(struct BufferAllocator));
    alloc->pointer += sizeof(struct BufferAllocator);
    Identity_set(alloc);
    return &alloc->generic;
}

void BufferAllocator_onOOM(struct Allocator* alloc,
                           struct Except* exceptionHandler)
{
    struct BufferAllocator* context = Identity_cast((struct BufferAllocator*) alloc);
    context->onOOM = exceptionHandler;
}

/* ************************************************************************** */
/*                                                                            */
/*                                                        :::      ::::::::   */
/*   malloc.c                                           :+:      :+:    :+:   */
/*                                                    +:+ +:+         +:+     */
/*   By: hpottier <hpottier@student.42.fr>          +#+  +:+       +#+        */
/*                                                +#+#+#+#+#+   +#+           */
/*   Created: 2022/02/18 16:58:54 by hpottier          #+#    #+#             */
/*   Updated: 2022/03/15 16:47:37 by hpottier         ###   ########.fr       */
/*                                                                            */
/* ************************************************************************** */

#include "malloc.h"

/*
** void *mmap(void *addr, size_t lenght, int prot, int flags, int fd, off_t offset);
** int munmap(void *addr, size_t lenght);
** int getpagesize(void);
** int getrlimit(int resource, struct rlimit *rlim);
*/

/* TINY_MAX and SMALL_MAX must be multiple of 16 */
#define TINY_MAX 4096
#define SMALL_MAX 262144

typedef struct	heap_chunk
{
	size_t prev_tail;
	size_t infos;
	struct heap_chunk *next;
	struct heap_chunk *prev;
}				hchunk;

typedef struct	heap_page
{
	struct heap_page *next;
	size_t infos;
}				hpage;

#define INUSE_BIT ((size_t)1)
#define SMALL_BIT ((size_t)2)
#define LARGE_BIT ((size_t)4)
#define END_BIT ((size_t)8)

#define is_inuse(infos) ((infos & INUSE_BIT) == INUSE_BIT)
#define set_inuse(infos) (infos |= INUSE_BIT)
#define clear_inuse(infos) (infos &= ~INUSE_BIT)

#define is_small(infos) ((infos & SMALL_BIT) == SMALL_BIT)
#define set_small(infos) (infos |= SMALL_BIT)
#define clear_small(infos) (infos &= ~SMALL_BIT)

#define is_large(infos) ((infos & LARGE_BIT) == LARGE_BIT)
#define set_large(infos) (infos |= LARGE_BIT)
#define clear_large(infos) (infos &= ~LARGE_BIT)

#define is_end(infos) ((infos & END_BIT) == END_BIT)
#define set_end(infos) (infos |= END_BIT)
#define clear_end(infos) (infos &= ~END_BIT)

#define clear_flags(infos) (infos &= ~(LARGE_BIT | SMALL_BIT | INUSE_BIT))

#define get_infos_size(infos) (infos & ~(INUSE_BIT | SMALL_BIT | INUSE_BIT))

#define next_chunk(chunk) ((hchunk *)((unsigned char *)chunk + get_infos_size(chunk->infos)))
#define prev_chunk(chunk) ((hchunk *)((unsigned char *)chunk - get_infos_size(chunk->infos)))

#define get_chunk_tail(chunk) (next_chunk(chunk)->prev_tail)

#define get_infos_from_ptr(ptr) ((size_t)*((unsigned char *)ptr - sizeof(size_t)))

#define get_chunk(ptr) ((hchunk *)((unsigned char *)ptr - sizeof(hchunk)))
#define get_large_chunk(ptr) ((hpage *)((unsigned char *)ptr - sizeof(hpage)))

static hpage *theap = NULL;
static hpage *sheap = NULL;
static hpage *lheap = NULL;

#define TBINS_SIZE TINY_MAX / 16 - 1
#define SBINS_SIZE SMALL_MAX / 16 - 1

static hchunk	*tbins[TBINS_SIZE];
static hchunk	*sbins[SBINS_SIZE];

static void	bzero_bins(hchunk **bins, size_t size)
{
	hchunk **end = bins + size;
	while (bins != end)
		*bins = NULL;
}

static hpage	*new_heap(const int pagesize, size_t alloc_max_size, hchunk **bins)
{
	size_t size = (alloc_max_size + sizeof(hchunk)) * 100 + sizeof(hpage); // Verifier overflow
	size = size + pagesize - (size % pagesize);

	void *nheap = mmap(NULL, size, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
	if ((hpage *)nheap == MAP_FAILED)
		return (NULL);

	/* Initializing the heap */
	((hpage *)nheap)->infos = size;
	set_inuse(((hpage *)nheap)->infos);
	set_end(((hpage *)nheap)->infos);
	((hpage *)nheap)->next = NULL;

	/* Initializing the chunk */
	size -= sizeof(hpage *);
	hchunk *chunk = (hchunk *)((unsigned char *)nheap + sizeof(hpage *));
	chunk->infos = size;
	get_chunk_tail(chunk) = size;
	set_end(get_chunk_tail(chunk));

	/* Add chunk to bins */
	size_t bindex = (size - 2 * sizeof(size_t)) / 16 - 1;
	if (bins[bindex] == NULL)
	{
		bins[bindex] = chunk;
		chunk->next = NULL;
		chunk->prev = NULL;
	}
	else
	{
		hchunk *curr = bins[bindex];
		if (get_infos_size(curr->infos) > get_infos_size(chunk->infos))
		while (curr->next != NULL && get_infos_size(curr->next->infos) <= get_infos_size(chunk->infos))
			curr = curr->next;
		if (curr->next != NULL)
		{
			curr->next->prev = chunk;
			chunk->next = curr->next;
		}
		else
			chunk->next = NULL;
		curr->next = chunk;
		chunk->prev = curr;
	}
	return ((hpage *)nheap);
}

void	*malloc(size_t size)
{
	write(1, "malloc\n", 7);
	if (size == 0)
		return (NULL);

	const int pagesize = getpagesize();
	void *ret;

	if (size <= TINY_MAX)
	{
		write(1, "mtiny\n", 6);
		if (theap == NULL)
		{
			bzero_bins(tbins, TBINS_SIZE);
			theap = new_heap(pagesize, TINY_MAX, tbins);
			if (theap == NULL)
				return (NULL);
		}
		size_t bindex = (size - 2 * sizeof(size_t)) / 16 - 1;
		while (tbins[bindex] == NULL)
			++bindex;
		hchunk *curr = tbins[bindex]; // Verifier si tbins[bindex] n'est pas NULL
		while (curr->next != NULL && (get_infos_size(curr->next->infos) - 2 * sizeof(size_t)) <= size)
			curr = curr->next;
	}
	else if (0 && size <= SMALL_MAX)
	{
		write(1, "msmall\n", 7);
		if (sheap == NULL)
		{
			bzero_bins(sbins, SBINS_SIZE);
			sheap = new_heap(pagesize, SMALL_MAX, sbins);
			if (sheap == NULL)
				return (NULL);
		}
		; // Chercher dans les sbins
	}
	else
	{
		write(1, "mlarge\n", 7);
		size_t large_size = size + sizeof(hpage); // Verifier overflow
		large_size = large_size + pagesize - (large_size % pagesize);
		ret = mmap(NULL, large_size, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
		if (ret == MAP_FAILED)
			return (NULL);
		ret->infos = large_size;
		set_large(ret->infos);
		ret->next = NULL;
		if (lheap == NULL)
			lheap = (hpage *)ret;
		else
		{
			hpage *curr = lheap;
			while (curr->next != NULL)
				curr = curr->next;
			curr->next = (hpage *)ret;
		}
		ret = (unsigned char *)ret + sizeof(hpage);
	}
	return (ret);
}

void	free(void *ptr)
{
	write(1, "free\n", 5);
	if (ptr != NULL)
	{
		size_t infos = get_infos_from_ptr(ptr); // Verifier si bien alloue pour tester avec certains programmes
		if (is_large(infos) == 1)
		{
			write(1, "flarge\n", 7);
			hpage *page = get_large_chunk(ptr);
			hpage *curr = lheap;
			hpage *prev = NULL;
			while (curr != NULL)
			{
				if (curr == page)
				{
					if (prev != NULL)
						prev->next = curr->next;
					return;
				}
				prev = curr;
				curr = curr->next;
			}
		}
		else if (is_small(infos) == 1)
		{
			write(1, "fsmall\n", 7);
			; // Liberer le chunk et defragmenter si possible
		}
		else
		{
			write(1, "ftiny\n", 6);
			; // Liberer le chunk et defragmenter si possible
		}
	}
	else
		write(1, "ptr = NULL\n", 11);
}

void	*realloc(void *ptr, size_t size)
{
	void *ret = malloc(size);
	if (ret == NULL)
		return (NULL);
	
	size_t x = 0;
	while (x < size)
	{
		((unsigned char *)ret)[x] = ((unsigned char *)ptr)[x];
		++x;
	}

	return (ret);
}

void	show_alloc_mem()
{
	write(1, "show_alloc_mem()\n", 17);
}

void	show_alloc_mem_ex()
{
	write(1, "show_alloc_mem_ex()\n", 20);
}

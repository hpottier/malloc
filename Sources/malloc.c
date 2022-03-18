/* ************************************************************************** */
/*                                                                            */
/*                                                        :::      ::::::::   */
/*   malloc.c                                           :+:      :+:    :+:   */
/*                                                    +:+ +:+         +:+     */
/*   By: hpottier <hpottier@student.42.fr>          +#+  +:+       +#+        */
/*                                                +#+#+#+#+#+   +#+           */
/*   Created: 2022/02/18 16:58:54 by hpottier          #+#    #+#             */
/*   Updated: 2022/03/18 06:58:09 by hpottier         ###   ########.fr       */
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

#define clear_flags(infos) (infos &= ~(LARGE_BIT | SMALL_BIT | INUSE_BIT | END_BIT))

#define get_infos_size(infos) (infos & ~(INUSE_BIT | SMALL_BIT | INUSE_BIT | END_BIT))

#define next_chunk(chunk) ((hchunk *)((unsigned char *)chunk + get_infos_size(chunk->infos) - (sizeof(hchunk *) * 2)))
#define prev_chunk(chunk) ((hchunk *)((unsigned char *)chunk - get_infos_size(chunk->prev_tail) - (sizeof(size_t) * 2)))

#define get_chunk_tail(chunk) (((hchunk *)((unsigned char *)chunk + (sizeof(size_t) * 2) + get_infos_size(chunk->infos)))->prev_tail)

#define get_infos_from_ptr(ptr) ((size_t)*((unsigned char *)ptr - sizeof(size_t)))

#define get_chunk(ptr) ((hchunk *)((unsigned char *)ptr - sizeof(hchunk)))
#define get_large_chunk(ptr) ((hpage *)((unsigned char *)ptr - sizeof(hpage)))

#define get_chunk_page(chunk) ((hpage *)((unsigned char *)chunk - sizeof(hpage *)))

static hpage *theap = NULL;
static hpage *sheap = NULL;
static hpage *lheap = NULL;

#define TBINS_SIZE (TINY_MAX * 100) / 16 - 1
#define SBINS_SIZE (SMALL_MAX * 100) / 4096 - 1

static hchunk	*tbins[TBINS_SIZE];
static hchunk	*sbins[SBINS_SIZE];

static void	bzero_bins(hchunk **bins, size_t size)
{
	hchunk **end = bins + size;
	while (bins != end)
	{
		*bins = NULL;
		++bins;
	}
}

static void	add_chunk_to_bins(size_t size, size_t bin_size, hchunk **bins, hchunk *chunk)
{
	size_t bindex = size / 16 - 1;
	if (bindex >= bin_size)
		bindex = bin_size - 1;
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
}

static hpage	*new_heap(const int pagesize, size_t alloc_max_size, hchunk **bins, size_t bin_size)
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
	size_t oldsize = size;
	(void)oldsize;
	size = (size - sizeof(hpage *)) - (sizeof(size_t) * 2);
	hchunk *chunk = (hchunk *)((unsigned char *)nheap + sizeof(hpage *));
	chunk->infos = size;
	get_chunk_tail(chunk) = size;
	set_end(get_chunk_tail(chunk));

	/* Add chunk to bins */
	add_chunk_to_bins(size, bin_size, bins, chunk);

	return ((hpage *)nheap);
}

static void	remove_from_bins(hchunk *ch, hchunk **bins, size_t bindex)
{
	if (ch->prev != NULL)
	{
		ch->prev->next = ch->next;
		if (ch->next != NULL)
		ch->next->prev = ch->prev;
	}
	else
	{
		bins[bindex] = ch->next;
		if (ch->next != NULL)
			ch->next->prev = NULL;
	}
}

static hchunk	*find_chunk(size_t size, hpage *heap, hchunk **bins, size_t alloc_max_size, size_t bin_size, const int pagesize)
{
	hchunk *curr;
	size_t bindex = size / 16 - 1;

	if (bindex >= bin_size)
		bindex = bin_size - 1;

	/* Searching bins for available chunks */
	while (bindex < bin_size && bins[bindex] == NULL)
		++bindex;

	/* If no bin found, create new page */
	if (bindex >= bin_size)
	{
	write(1, "A\n", 2);
		hpage *pcurr = heap;
		while (pcurr->next != NULL)
			pcurr = pcurr->next;
		pcurr->next = new_heap(pagesize, alloc_max_size, bins, bin_size);
		if (pcurr->next == NULL)
			return (NULL);
		curr = (hchunk *)((unsigned char *)pcurr->next + sizeof(hpage *));
		remove_from_bins(curr, bins, size / 16 - 1);
	}
	else
	{
		/* Search inside the bin for a chunk of the correct size and move to next bins if non found */
		do
		{
			curr = bins[bindex];
	write(1, "G\n", 2);
			while (curr->next != NULL && get_infos_size(curr->next->infos) <= size)
				curr = curr->next;
	write(1, "H\n", 2);
			if (curr->next == NULL && get_infos_size(curr->infos) < size)
				while (++bindex < bin_size && bins[bindex] == NULL)
					;
		} while (get_infos_size(curr->infos) < size && bindex < bin_size);

		/* If no chunk of the correct size is found, create new page */
		if (bindex == bin_size)
		{
			hpage *pcurr = heap;
			while (pcurr->next != NULL)
				pcurr = pcurr->next;
			pcurr->next = new_heap(pagesize, alloc_max_size, bins, bin_size);
			if (pcurr->next == NULL)
				return (NULL);
			curr = (hchunk *)((unsigned char *)pcurr->next + sizeof(hpage *));
			remove_from_bins(curr, bins, size / 16 - 1);
		}
		else
		{
			/* Otherwise return the chunk found */
			if (curr->next != NULL)
				curr = curr->next;
			remove_from_bins(curr, bins, bindex);
		}
	}
	return (curr);
}

static void	split_chunk(hchunk *elem, size_t size, hchunk **bins)
{
	hchunk *split = (hchunk *)((unsigned char *)elem + sizeof(size_t) * 2 + size);

	split->prev_tail = size;
	split->infos = get_infos_size(elem->infos) - size - sizeof(size_t) * 2;
	elem->infos = size;
	if (is_end(get_chunk_tail(split)))
	{
		get_chunk_tail(split) = split->infos;
		set_end(get_chunk_tail(split));
	}
	else
		get_chunk_tail(split) = split->infos;

	size_t bindex = split->infos / 16 - 1;
	if (bins[bindex] == NULL)
	{
		bins[bindex] = split;
		split->prev = NULL;
		split->next = NULL;
	}
	else
	{
		hchunk *curr = bins[bindex];
		if (curr->infos > split->infos)
		{
			bins[bindex] = split;
			split->prev = NULL;
			split->next = curr;
			curr->prev = split;
		}
		else
		{
			while (curr->next != NULL && curr->infos <= split->infos)
				curr = curr->next;
			split->prev = curr;
			split->next = curr->next;
			curr->next = split;
			if (split->next != NULL)
				split->next->prev = split;
		}
	}
}

void	*malloc(size_t size)
{
	write(1, "malloc\n", 7);
	if (size == 0)
		return (NULL);

	const int pagesize = getpagesize();
	void *ret;

	/* For tiny and small sizes, return an address in one of the corresponding heaps */
	if (size <= TINY_MAX)
	{
		write(1, "mtiny\n", 6);
		if (theap == NULL)
		{
			bzero_bins(tbins, TBINS_SIZE);
			theap = new_heap(pagesize, TINY_MAX, tbins, TBINS_SIZE);
			if (theap == NULL)
				return (NULL);
		}

		hchunk *curr = find_chunk(size, theap, tbins, TINY_MAX, TBINS_SIZE, pagesize);
		if (curr == NULL)
			return (NULL);

		if (get_infos_size(curr->infos) - size >= sizeof(hchunk))
			split_chunk(curr, size, tbins);
		set_inuse(curr->infos);
		set_inuse(get_chunk_tail(curr));

		ret = (void *)(curr + (sizeof(size_t) * 2));
	}
	else if (0 && size <= SMALL_MAX)
	{
		write(1, "msmall\n", 7);
		if (sheap == NULL)
		{
			bzero_bins(sbins, SBINS_SIZE);
			sheap = new_heap(pagesize, SMALL_MAX, sbins, SBINS_SIZE);
			if (sheap == NULL)
				return (NULL);
		}

		hchunk *curr = find_chunk(size, sheap, sbins, SMALL_MAX, SBINS_SIZE, pagesize);
		if (curr == NULL)
			return (NULL);

		if (get_infos_size(curr->infos) - size >= sizeof(hchunk))
			split_chunk(curr, size, sbins);
		set_inuse(curr->infos);
		set_inuse(get_chunk_tail(curr));
		set_small(curr->infos);

		ret = (void *)(curr + (sizeof(size_t) * 2));
	}
	else
	{
		/* If size is over SMALL_MAX just return a new map of the correct size */
		write(1, "mlarge\n", 7);

		/* Dealing with overflows */
		if (size > SIZE_MAX - sizeof(hpage))
		{
			errno = ENOMEM;
			return (NULL);
		}
		size_t large_size = size + sizeof(hpage);
		large_size = large_size - (large_size % pagesize);
		if (SIZE_MAX - large_size > (size_t)pagesize)
		{
			errno = ENOMEM;
			return (NULL);
		}
		large_size = large_size + pagesize;

		ret = mmap(NULL, large_size, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
		if (ret == MAP_FAILED)
			return (NULL);

		((hpage *)ret)->infos = large_size;
		set_large(((hpage *)ret)->infos);
		((hpage *)ret)->next = NULL;

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

static void	free_ts_chunk(hchunk *chunk, hchunk **bins, hpage *heap)
{
	if (is_inuse(chunk->prev_tail) == 0)
	{
		/* Defragments with previous chunk */
		hchunk *prev = prev_chunk(chunk);

		prev->infos = get_infos_size(prev->infos) + get_infos_size(chunk->infos);
		if (is_end(get_chunk_tail(chunk)))
		{
			get_chunk_tail(chunk) = prev->infos;
			set_end(get_chunk_tail(chunk));
		}
		else
			get_chunk_tail(chunk) = prev->infos;
		if (prev->next != NULL)
			prev->next->prev = prev->prev;
		if (prev->prev != NULL)
			prev->prev->next = prev->next;
		else
		{
			size_t bindex = prev->infos / 16 - 1;
			bins[bindex] = NULL;
		}
		chunk = prev;
	}

	if (is_end(get_chunk_tail(chunk)) == 0)
	{
		if (is_inuse(chunk->prev_tail) == 0)
		{
			/* Defragments with next chunk */
			hchunk *next = next_chunk(chunk);

			chunk->infos = get_infos_size(chunk->infos) + get_infos_size(next->infos);
			if (is_end(get_chunk_tail(next)))
			{
				get_chunk_tail(next) = chunk->infos;
				set_end(get_chunk_tail(next));
			}
			else
				get_chunk_tail(next) = chunk->infos;
			if (next->next != NULL)
				next->next->prev = next->prev;
			if (next->prev != NULL)
				next->prev->next = next->next;
			else
			{
				size_t bindex = chunk->infos / 16 - 1;
				bins[bindex] = NULL;
			}
		}
	}

	/* Check if the page is empty and there is more than one page in the heap */
	if (is_end(get_chunk_tail(chunk)))
	{
		/* If so, munmaps it */
		hpage *p = get_chunk_page(chunk);
		hpage *curr = heap;
		hpage *prev = curr;

		while (curr != NULL)
		{
			if (curr == p)
			{
				if (curr->next != NULL || heap != curr)
				{
					if (heap == curr)
						heap = curr->next;
					prev->next = curr->next;
					if (munmap(curr, get_infos_size(curr->infos)) < 0)
					{
						if (heap == curr->next)
							heap = curr;
						prev->next = curr;
					}
					return;
				}
				break;
			}
			prev = curr;
			curr = curr->next;
		}
	}
}

void	free(void *ptr)
{
	write(1, "free\n", 5);
	if (ptr != NULL)
	{
		size_t infos = get_infos_from_ptr(ptr); // Vérifier si bien alloué pour tester avec certains programmes
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
					if (munmap(ptr, get_infos_size(page->infos)) < 0)
						prev->next = curr;
					return;
				}
				prev = curr;
				curr = curr->next;
			}
		}
		else if (is_small(infos) == 1)
		{
			write(1, "fsmall\n", 7);
			hchunk *chunk = get_chunk(ptr);

			free_ts_chunk(chunk, sbins, sheap);

			/* Otherwise add the new chunk to the bins */
			add_chunk_to_bins(get_infos_size(chunk->infos), SBINS_SIZE, sbins, chunk);
		}
		else
		{
			write(1, "ftiny\n", 6);
			hchunk *chunk = get_chunk(ptr);

			free_ts_chunk(chunk, sbins, sheap);

			/* Otherwise add the new chunk to the bins */
			add_chunk_to_bins(get_infos_size(chunk->infos), SBINS_SIZE, sbins, chunk);
		}
	}
	else
		write(1, "ptr = NULL\n", 11);
}

void	*realloc(void *ptr, size_t size)
{
	write(1, "realloc\n", 8);

	size_t ptr_size = 0;
	if (ptr != NULL)
	{
		size_t infos = get_infos_from_ptr(ptr); // Vérifier si bien alloué pour tester avec certains programmes
		ptr_size = get_infos_size(infos);

		if (is_small(infos))
		{
			; // Regarder si le chunk suivant à assez de place
		}
		else if (is_large(infos) == 0)
		{
			; // Regarder si le chunk suivant à assez de place
		}
	}

	void *ret = malloc(size);
	if (ret == NULL)
		return (NULL);

	size_t x = 0;
	while (x < ptr_size)
	{
		((unsigned char *)ret)[x] = ((unsigned char *)ptr)[x];
		++x;
	}

	free(ptr);

	return (ret);
}

void	*calloc(size_t nmemb, size_t size)
{
	write(1, "calloc\n", 7);
	if (nmemb == 0 || size == 0 || nmemb > SIZE_MAX / size) // (nmemb * size) / nmemb != size
		return (NULL);

	void *ret = malloc(nmemb * size);
	if (ret == NULL)
		return (NULL);

	size_t x = 0;
	switch (size)
	{
	case sizeof(size_t):
		while (x < nmemb)
		{
			((size_t *)ret)[x] = 0;
			++x;
		}
		break;
	case sizeof(unsigned int):
		while (x < nmemb)
		{
			((unsigned int *)ret)[x] = 0;
			++x;
		}
		break;
	default:
		while (x < nmemb)
		{
			((unsigned char *)ret)[x] = 0;
			++x;
		}
	}
	return (ret);
}

void	show_alloc_mem(void)
{
	write(1, "show_alloc_mem()\n", 17);
}

void	show_alloc_mem_ex(void)
{
	write(1, "show_alloc_mem_ex()\n", 20);
}

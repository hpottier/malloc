/* ************************************************************************** */
/*                                                                            */
/*                                                        :::      ::::::::   */
/*   malloc.c                                           :+:      :+:    :+:   */
/*                                                    +:+ +:+         +:+     */
/*   By: hpottier <hpottier@student.42.fr>          +#+  +:+       +#+        */
/*                                                +#+#+#+#+#+   +#+           */
/*   Created: 2022/02/18 16:58:54 by hpottier          #+#    #+#             */
/*   Updated: 2022/04/07 14:48:49 by hpottier         ###   ########.fr       */
/*                                                                            */
/* ************************************************************************** */

#include "malloc.h"

/*
** void *mmap(void *addr, size_t lenght, int prot, int flags, int fd, off_t offset);
** int munmap(void *addr, size_t lenght);
** int getpagesize(void);
** int getrlimit(int resource, struct rlimit *rlim);
*/

/* TINY_MAX and SMALL_MAX must be multiple of 16 and superior to 16 */
#ifndef TINY_MAX
#define TINY_MAX 4096
#endif

#ifndef SMALL_MAX
#define SMALL_MAX 262144
#endif

/* By default malloc is thread safe, define this to change this behavior */
/* #define MALLOC_NO_LOCK */

#ifndef MALLOC_NO_LOCK
static pthread_mutex_t malloc_lock = PTHREAD_MUTEX_INITIALIZER;
#endif

/* In case MAP_ANONYMOUS or MAP_ANON are not defined */
#ifndef MAP_ANONYMOUS
#ifdef MAP_ANON
#define MMAP_CALL(size) mmap(NULL, size, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANON, -1, 0)
#else
static int dev_zero_fd = -1;
#define MMAP_CALL(size)										\
	do														\
	{														\
		if (dev_zero_fd < 0)								\
			open("/dev/zero", O_RDWR);						\
		mmap(NULL, PROT_READ | PROT_WRITE, MAP_PRIVATE);	\
	} while (0)
#endif
#else
#define MMAP_CALL(size) mmap(NULL, size, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0)
#endif

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

#define next_chunk(chunk) ((hchunk *)((unsigned char *)chunk + get_infos_size(chunk->infos) + (sizeof(hchunk *) * 2)))
#define prev_chunk(chunk) ((hchunk *)((unsigned char *)chunk - get_infos_size(chunk->prev_tail) - (sizeof(size_t) * 2)))

#define get_chunk_tail(chunk) (((hchunk *)((unsigned char *)chunk + (sizeof(size_t) * 2) + get_infos_size(chunk->infos)))->prev_tail)

#define get_infos_from_ptr(ptr) ((size_t)*((unsigned char *)ptr - sizeof(size_t)))

#define get_chunk(ptr) ((hchunk *)((unsigned char *)ptr - (sizeof(size_t) * 2)))
#define get_large_chunk(ptr) ((hpage *)((unsigned char *)ptr - sizeof(hpage)))

#define get_chunk_page(chunk) ((hpage *)((unsigned char *)chunk - sizeof(hpage *)))

static hpage *theap = NULL;
static hpage *sheap = NULL;
static hpage *lheap = NULL;

#define TBINS_SIZE (TINY_MAX * 100) / 16 - 1
#define SBINS_SIZE (SMALL_MAX * 100) / 4096 - 1

static hchunk	*tbins[TBINS_SIZE];
static hchunk	*sbins[SBINS_SIZE];

static size_t ft_count_recurs(size_t nbr)
{
	if (nbr)
		return (ft_count_recurs(nbr / 10) + 1);
	return (0);
}

static void		ft_recurs(char *ret, size_t nbr, size_t i)
{
	if (nbr)
	{
		ft_recurs(ret, nbr / 10, i - 1);
		ret[i] = (nbr % 10) + 48;
	}
}

static void ft_putnbr(size_t i)
{
	if (i == 0)
	{
		write(1, "0\n", 2);
		return;
	}
	char arr[500];
	size_t size = ft_count_recurs(i);
	arr[size] = 0;
	ft_recurs(arr, i, size - 1);
	write(1, arr, size);
	write(1, "\n", 1);
}

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
	write(1, "new_heap\n", 9);
	size_t size = (alloc_max_size + sizeof(hchunk)) * 100 + sizeof(hpage); // Verifier overflow
	size = size + pagesize - (size % pagesize);

	void *nheap = MMAP_CALL(size);
	if ((hpage *)nheap == MAP_FAILED)
		return (NULL);

	/* Initializing the heap */
	((hpage *)nheap)->infos = size;
	set_inuse(((hpage *)nheap)->infos);
	set_end(((hpage *)nheap)->infos);
	((hpage *)nheap)->next = NULL;

	/* Initializing the chunk */
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
	if (ch->next != NULL)
		ch->next->prev = ch->prev;
	if (ch->prev != NULL)
		ch->prev->next = ch->next;
	else
		bins[bindex] = ch->next;
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
		write(1, "No bin found\n", 13);
		hpage *pcurr = heap;
		while (pcurr->next != NULL)
			pcurr = pcurr->next;
		pcurr->next = new_heap(pagesize, alloc_max_size, bins, bin_size);
		if (pcurr->next == NULL)
			return (NULL);
		curr = (hchunk *)((unsigned char *)pcurr->next + sizeof(hpage *));
		size = size / 16 - 1;
		if (size >= bin_size)
			size = bin_size - 1;
		remove_from_bins(curr, bins, size);
	}
	else
	{
		/* Search inside the bin for a chunk of the correct size and move to next bins if non found */
		do
		{
			curr = bins[bindex];
			hchunk *tmp = curr;
			while (tmp && get_infos_size(tmp->infos) <= size)
			{
				curr = tmp;
				tmp = tmp->next;
			}
			if (get_infos_size(curr->infos) < size)
				while (++bindex < bin_size && bins[bindex] == NULL)
					;
		} while (get_infos_size(curr->infos) < size && bindex < bin_size);

		/* If no chunk of the correct size is found, create new page */
		if (bindex >= bin_size)
		{
			write(1, "No chunk found\n", 15);
			hpage *pcurr = heap;
			while (pcurr->next != NULL)
				pcurr = pcurr->next;
			pcurr->next = new_heap(pagesize, alloc_max_size, bins, bin_size);
			if (pcurr->next == NULL)
				return (NULL);
			curr = (hchunk *)((unsigned char *)pcurr->next + sizeof(hpage *));
			size = size / 16 - 1;
			if (size >= bin_size)
				size = bin_size - 1;
			remove_from_bins(curr, bins, size);
		}
		else
		{
			/* Otherwise return the chunk found */
			remove_from_bins(curr, bins, bindex);
		}
	}
	return (curr);
}

static void	split_chunk(hchunk *elem, size_t size, hchunk **bins, size_t bin_size)
{
	write(1, "split_chunk\n", 12);
	if (size < 16)
		size = 16;
	else
		size = size + 16 - (size % 16);
	hchunk *split = (hchunk *)((unsigned char *)elem + (sizeof(size_t) * 2) + size);

	split->prev_tail = size;
	split->infos = get_infos_size(elem->infos) - size - (sizeof(size_t) * 2);
	elem->infos = size;
	if (is_end(get_chunk_tail(split)))
	{
		get_chunk_tail(split) = split->infos;
		set_end(get_chunk_tail(split));
	}
	else
		get_chunk_tail(split) = split->infos;

	size_t bindex = split->infos / 16 - 1;
	if (bindex >= bin_size)
		bindex = bin_size - 1;
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
	ft_putnbr(size);

	#ifndef MALLOC_NO_LOCK
	pthread_mutex_lock(&malloc_lock);
	#endif

	/* For tiny and small sizes, return an address in one of the corresponding heaps */
	if (size <= TINY_MAX)
	{
		write(1, "mtiny\n", 6);
		if (theap == NULL)
		{
			write(1, "theap == NULL\n", 14);
			bzero_bins(tbins, TBINS_SIZE);
			theap = new_heap(pagesize, TINY_MAX, tbins, TBINS_SIZE);
			if (theap == NULL)
			{
				#ifndef MALLOC_NO_LOCK
				pthread_mutex_unlock(&malloc_lock);
				#endif
				return (NULL);
			}
		}

		hchunk *curr = find_chunk(size, theap, tbins, TINY_MAX, TBINS_SIZE, pagesize);
		if (curr == NULL)
		{
			#ifndef MALLOC_NO_LOCK
			pthread_mutex_unlock(&malloc_lock);
			#endif
			return (NULL);
		}
		
		if (get_infos_size(curr->infos) - size >= sizeof(hchunk))
			split_chunk(curr, size, tbins, TBINS_SIZE);
		set_inuse(curr->infos);
		set_inuse(get_chunk_tail(curr));

		ret = (void *)((unsigned char *)curr + (sizeof(size_t) * 2));
	}
	else if (0 && size <= SMALL_MAX)
	{
		write(1, "msmall\n", 7);
		if (sheap == NULL)
		{
			bzero_bins(sbins, SBINS_SIZE);
			sheap = new_heap(pagesize, SMALL_MAX, sbins, SBINS_SIZE);
			if (sheap == NULL)
			{
				#ifndef MALLOC_NO_LOCK
				pthread_mutex_unlock(&malloc_lock);
				#endif
				return (NULL);
			}
		}

		hchunk *curr = find_chunk(size, sheap, sbins, SMALL_MAX, SBINS_SIZE, pagesize);
		if (curr == NULL)
		{
			#ifndef MALLOC_NO_LOCK
			pthread_mutex_unlock(&malloc_lock);
			#endif
			return (NULL);
		}

		if (get_infos_size(curr->infos) - size >= sizeof(hchunk))
			split_chunk(curr, size, sbins, SBINS_SIZE);
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
			#ifndef MALLOC_NO_LOCK
			pthread_mutex_unlock(&malloc_lock);
			#endif
			return (NULL);
		}
		size_t large_size = size + sizeof(hpage);
		large_size = large_size - (large_size % pagesize);
		if (SIZE_MAX - large_size > (size_t)pagesize)
		{
			errno = ENOMEM;
			#ifndef MALLOC_NO_LOCK
			pthread_mutex_unlock(&malloc_lock);
			#endif
			return (NULL);
		}
		large_size = large_size + pagesize;

		ret = MMAP_CALL(large_size);
		if (ret == MAP_FAILED)
		{
			#ifndef MALLOC_NO_LOCK
			pthread_mutex_unlock(&malloc_lock);
			#endif
			return (NULL);
		}

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
	ft_putnbr((size_t)ret);

	#ifndef MALLOC_NO_LOCK
	pthread_mutex_unlock(&malloc_lock);
	#endif

	return (ret);
}

static int	free_ts_chunk(hchunk *chunk, hchunk **bins, hpage **heap, size_t bin_size)
{
	if (is_inuse(chunk->prev_tail) == 0)
	{
		write(1, "defrag prev\n", 12);
		/* Defragments with previous chunk */
		hchunk *prev = prev_chunk(chunk);

		prev->infos = get_infos_size(prev->infos) + get_infos_size(chunk->infos) + (sizeof(size_t) * 2);
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
			if (bindex >= bin_size)
				bindex = bin_size - 1;
			bins[bindex] = NULL;
		}
		prev->next = NULL;
		prev->prev = NULL;
		chunk = prev;
	}

	if (is_end(get_chunk_tail(chunk)) == 0)
	{
		hchunk *next = next_chunk(chunk);
		if (is_inuse(next->infos) == 0)
		{
			write(1, "defrag next\n", 12);
			/* Defragments with next chunk */
			chunk->infos = get_infos_size(next->infos) + get_infos_size(chunk->infos) + (sizeof(size_t) * 2);
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
				if (bindex >= bin_size)
					bindex = bin_size - 1;
				bins[bindex] = NULL;
			}
			next->next = NULL;
			next->prev = NULL;
		}
	}

	/* Check if the page is empty and there is more than one page in the heap */
	if (is_end(get_chunk_tail(chunk)) && is_end(chunk->prev_tail))
	{
		write(1, "empty page\n", 11);
		/* If so, munmaps it */
		hpage *p = get_chunk_page(chunk);
		hpage *curr = *heap;
		hpage *prev = curr;

		while (curr != NULL)
		{
			if (curr == p)
			{
				if (curr->next != NULL || *heap != curr)
				{
					if (*heap == curr)
						*heap = curr->next;
					prev->next = curr->next;
					write(1, "munmap page\n", 12);
					if (munmap(curr, get_infos_size(curr->infos)) < 0)
					{
						if (*heap == curr->next)
							*heap = curr;
						prev->next = curr;
						return (0);
					}
					return (1);
				}
				break;
			}
			prev = curr;
			curr = curr->next;
		}
	}
	return (0);
}

void	free(void *ptr)
{
	write(1, "free\n", 5);
	ft_putnbr((size_t)ptr);

	#ifndef MALLOC_NO_LOCK
	pthread_mutex_lock(&malloc_lock);
	#endif

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
					#ifndef MALLOC_NO_LOCK
					pthread_mutex_unlock(&malloc_lock);
					#endif
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

			if (free_ts_chunk(chunk, sbins, &sheap, SBINS_SIZE) == 1)
			{
				#ifndef MALLOC_NO_LOCK
				pthread_mutex_unlock(&malloc_lock);
				#endif
				return;
			}

			/* If heap wasn't munmap() add the new chunk to the bins */
			add_chunk_to_bins(get_infos_size(chunk->infos), SBINS_SIZE, sbins, chunk);
		}
		else
		{
			write(1, "ftiny\n", 6);
			hchunk *chunk = get_chunk(ptr);

			if (free_ts_chunk(chunk, tbins, &theap, TBINS_SIZE) == 1)
			{
				#ifndef MALLOC_NO_LOCK
				pthread_mutex_unlock(&malloc_lock);
				#endif
				return;
			}

			/* If heap wasn't munmap() add the new chunk to the bins */
			add_chunk_to_bins(get_infos_size(chunk->infos), TBINS_SIZE, tbins, chunk);
		}
	}
	else
		write(1, "ptr = NULL\n", 11);

	#ifndef MALLOC_NO_LOCK
	pthread_mutex_unlock(&malloc_lock);
	#endif
}

void	*realloc(void *ptr, size_t size)
{
	write(1, "realloc\n", 8);

	size_t ptr_size = 0;
	if (ptr != NULL)
	{
		#ifndef MALLOC_NO_LOCK
		pthread_mutex_lock(&malloc_lock);
		#endif

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

		#ifndef MALLOC_NO_LOCK
		pthread_mutex_unlock(&malloc_lock);
		#endif
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
	#ifndef MALLOC_NO_LOCK
	pthread_mutex_lock(&malloc_lock);
	#endif

	write(1, "show_alloc_mem()\n", 17);

	#ifndef MALLOC_NO_LOCK
	pthread_mutex_unlock(&malloc_lock);
	#endif
}

void	show_alloc_mem_ex(void)
{
	#ifndef MALLOC_NO_LOCK
	pthread_mutex_lock(&malloc_lock);
	#endif

	write(1, "show_alloc_mem_ex()\n", 20);

	#ifndef MALLOC_NO_LOCK
	pthread_mutex_unlock(&malloc_lock);
	#endif
}

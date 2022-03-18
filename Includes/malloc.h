/* ************************************************************************** */
/*                                                                            */
/*                                                        :::      ::::::::   */
/*   malloc.h                                           :+:      :+:    :+:   */
/*                                                    +:+ +:+         +:+     */
/*   By: hpottier <hpottier@student.42.fr>          +#+  +:+       +#+        */
/*                                                +#+#+#+#+#+   +#+           */
/*   Created: 2022/02/18 16:56:02 by hpottier          #+#    #+#             */

/*                                                                            */
/* ************************************************************************** */

#ifndef MALLOC_H
# define MALLOC_H

# include <stdint.h>
# include <errno.h>
# include <sys/mman.h>
# include <unistd.h>
# include <sys/time.h>
# include <sys/resource.h>

# include <pthread.h>

void	free(void *ptr);
void	*malloc(size_t size);
void	*realloc(void *ptr, size_t size);
void	*calloc(size_t nmemb, size_t size);

void	show_alloc_mem(void);

void	show_alloc_mem_ex(void);

#endif

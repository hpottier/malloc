/* ************************************************************************** */
/*                                                                            */
/*                                                        :::      ::::::::   */
/*   main.c                                             :+:      :+:    :+:   */
/*                                                    +:+ +:+         +:+     */
/*   By: hpottier <hpottier@student.42.fr>          +#+  +:+       +#+        */
/*                                                +#+#+#+#+#+   +#+           */
/*   Created: 2022/02/23 11:56:26 by hpottier          #+#    #+#             */
/*   Updated: 2022/03/16 03:39:53 by hpottier         ###   ########.fr       */
/*                                                                            */
/* ************************************************************************** */

#include <stdlib.h>
#include <stdio.h>
#include <time.h>
#include "malloc.h"

#define ARR_SIZE 12

int main()
{
	int *arr = (int *)malloc(ARR_SIZE * sizeof(int));

	printf("arr = %p\n", arr);
	if (arr == NULL)
		return (1);
	srand(clock());
	for (int i = 0; i < ARR_SIZE; ++i)
	{
		arr[i] = rand() % 10;
	}
	for (int i = 0; i < ARR_SIZE; ++i)
	{
		printf("%d\n", arr[i]);
	}
	free(arr);
	return (0);
}

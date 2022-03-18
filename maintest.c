/* ************************************************************************** */
/*                                                                            */
/*                                                        :::      ::::::::   */
/*   main.c                                             :+:      :+:    :+:   */
/*                                                    +:+ +:+         +:+     */
/*   By: hpottier <hpottier@student.42.fr>          +#+  +:+       +#+        */
/*                                                +#+#+#+#+#+   +#+           */
/*   Created: 2022/02/23 11:56:26 by hpottier          #+#    #+#             */
/*   Updated: 2022/03/18 06:41:54 by hpottier         ###   ########.fr       */
/*                                                                            */
/* ************************************************************************** */

#include <stdlib.h>
#include <stdio.h>
#include <time.h>
#include "malloc.h"

#define ARR_SIZE 12
#define TAB_SIZE 18

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

	int *tab = (int *)malloc(TAB_SIZE * sizeof(int));

	printf("tab = %p\n", tab);
	if (tab == NULL)
		return (1);
	srand(clock());
	for (int i = 0; i < TAB_SIZE; ++i)
	{
		tab[i] = rand() % 10;
	}
	for (int i = 0; i < TAB_SIZE; ++i)
	{
		printf("%d\n", tab[i]);
	}
	free(tab);
	free(arr);
	return (0);
}

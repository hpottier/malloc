#******************************************************************************#
#                                                                              #
#                                                         :::      ::::::::    #
#    Makefile                                           :+:      :+:    :+:    #
#                                                     +:+ +:+         +:+      #
#    By: hpottier <hpottier@student.42.fr>          +#+  +:+       +#+         #
#                                                 +#+#+#+#+#+   +#+            #
#    Created: 2022/02/18 16:38:19 by hpottier          #+#    #+#              #
#    Updated: 2022/02/24 15:15:45 by hpottier         ###   ########.fr        #
#                                                                              #
#******************************************************************************#

ifeq ($(HOSTTYPE),)
	HOSTTYPE	:= $(shell uname -m)_$(shell uname -s)
endif

NAME			=	libft_malloc_$(HOSTTYPE).so

DIRSRC			=	Sources
OBJD			=	Objects
DIRHDR			=	Includes

HDR				=	malloc.h

SRC				=	malloc.c

OBJ				=	$(SRC:.c=.o)
OBJS			=	$(OBJ:%=$(OBJD)/%)

CFLAGS			=	-Wall -Wextra -pedantic -pipe -O3

CC				=	gcc
RM				=	rm -rf
ECHO			=	echo

all				:	$(NAME)

$(NAME)			:	$(OBJD) $(OBJS)
						@$(ECHO) "\033[32m> Libft_malloc objects compiled\033[0m"
						@$(ECHO) "\033[38;5;208m> Building libft_malloc\033[38;5;125m"
						$(CC) -shared -Wl,-soname,$(NAME) $(CFLAGS) -o $(NAME) $(OBJS)
						@$(ECHO) "\033[32m> Libft_malloc built\033[0m"

$(OBJD)			:
						@mkdir -p $(OBJD)

$(OBJD)/%.o		:	$(DIRSRC)/%.c $(DIRHDR)/$(HDR) Makefile
						@$(ECHO) "\033[38;5;208m> Compiling libft_malloc objects\033[38;5;125m"
						$(CC) -I$(DIRHDR) $(CFLAGS) -fPIC -o $@ -c $<

clean			:
						-@$(RM) $(OBJD) a.out
						@$(ECHO) "\033[94m> All libft_malloc objects removed\033[0m"

fclean			:	clean
						-@$(RM) $(NAME) $(FINALHDR)
						@$(ECHO) "\033[94m> Libft_malloc cleaned all\033[0m"

test			:
						$(CC) -I$(DIRHDR) -c $(DIRSRC)/malloc.c -o $(OBJD)/malloc.o && $(CC) -I $(DIRHDR) $(OBJD)/malloc.o main.c && ./a.out

re				:	fclean all

.PHONY			:	all clean re fclean

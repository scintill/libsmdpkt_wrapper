libsmdpkt_wrapper.so: wrapper.c
	gcc -shared -fPIC -Wall -Wextra -Werror -std=c99 -O2 $< -o $@

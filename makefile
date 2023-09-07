co: main.c libco.c
	gcc main.c libco.c -Wall -Werror -o co

pt: tmain.c
	gcc tmain.c -Wall -Werror -lpthread -o pt

test: pt co
	-timeout 1s ./pt > pt.txt
	-timeout 1s ./co > co.txt
	wc -l co.txt pt.txt

clean:
	rm pt.txt pt
	rm co.txt co

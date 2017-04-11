all: 
	gcc -g grading.c -o grading

setup:
	mkdir newProj newProj/Grading_Materials newProj/Late newProj/On_Time newProj/Grading_Materials/input_data newProj/Grading_Materials/partial_credits newProj/Grading_Materials/test_cases
	cp grading.c newProj/Grading_Materials/grading.c
	cp makefile newProj/Grading_Materials/makefile


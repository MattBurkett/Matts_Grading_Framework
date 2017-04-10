#include <stdio.h>
#include <stdlib.h>	
#include <string.h>
#include <strings.h>
//#include <bsd/string.h>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <dirent.h>
#include <unistd.h>
#include <errno.h>

typedef struct 		partialcredit{
	double 			score;							//score which is given
	char			description[256];				//description of why points were deducted.
	char*			studentPartialCreditFile;		//triggering output file
	struct partialcredit* next;						//next partial credit of the same testID
}partialcredit;	

typedef struct 		test{
	char* 			testFileName;			//of the form: '/Grading_Materials/test1.txt'		*must be absolute file paths*
	char* 			solutionFileName;		//of the form: '/Grading_Materials/solution1.txt'	*must be absolute file paths*
	int 			testID;					//testID, will start from 1 and increment
	int 			maxScore;				//maximum amount of points in this test.
	partialcredit* 	partialCredit;			//all the partial credits which were given for this particular test case.
}test;

typedef struct 		score{
	int 			testID;					//testID, will start from 1 and increment (refers to the struct test.testID)
	double 			score;					//the score the student recived from this particular test.
	char			description[256];		//"\0" if test passed or a copy of partialcredit.description otherwise.
}score;

typedef struct 		submission{
	score* 			studentScores;			//array the size of the number of tests.
	char*  			studentName;			//student's netID
	char*			studentDirName;			//used to easily find the students submission.
	int    			late;					//0 is on time, 1 is late, anything else is undefined.
	int    			total;					//sum of all studentScores[].score
}submission;

typedef struct 		project{
	test*			tests;					//test cases
	int 			testCount;				//number of tests
	test*			valTests;				//valgrind test cases
	int 			valtestCount;			//number of valgrind test cases
	submission*		submissions;			//student submissions
	int 			submissionCount;		//number of submissions
	char*			rootDir;				//root directory for grading.
}project;

const double latePercentage = 0.75;

int atoic(char* a){
	char* str = malloc(sizeof(a)*(strlen(a)+1));
	strcpy(str, a);
	int num = 0;
	char* tok;
	tok = strtok(str, ",");
	while (tok != NULL && tok[0] >= '0' && tok[0] <= '9')
	{
		num = num*1000 + atoi(tok);
		tok = strtok(NULL, ",");
	}
	free(str);
	return num;
}

void SetUpLocalSymLinks(char* root){
	char symCall[512];
	sprintf(symCall, "find %s/Grading_Materials/input_data -type f -exec ln -s '{}' \\;", root);
	system(symCall);	
}

void RemoveLocalSymLinks(){
	system("find -type l -delete");
}

//not yet implemented
void SetUpAllSymLinks(char* root){
}

void RemoveAllSymLinks(char* root){
	chdir(root);
	system("find -type l -delete");
}

void FreePartialCredit(partialcredit* cur){
	if(cur == NULL)
		return;
	FreePartialCredit(cur->next);
	free(cur);
}

void FreeMem(project* thisProject){
	for(int i = 0; i < thisProject->submissionCount; i++)
	{
		free(thisProject->submissions[i].studentScores);
		free(thisProject->submissions[i].studentName);
		free(thisProject->submissions[i].studentDirName);
	}	
	free(thisProject->submissions);
	for(int i = 1; i < thisProject->testCount; i++)
	{
		if(thisProject->valTests[i].testFileName == NULL)
			continue;
		free(thisProject->valTests[i].testFileName);
	}
	free(thisProject->valTests);
	for(int i = 1; i < thisProject->testCount; i++)
	{
		free(thisProject->tests[i].testFileName);
		free(thisProject->tests[i].solutionFileName);
		FreePartialCredit(thisProject->tests[i].partialCredit);
	}
	free(thisProject->tests);
	free(thisProject->rootDir);
	free(thisProject);
}

//on return of n != 0, skip that many lines of grading
int ShowStatus(project* thisProject, int curSubmissionNum){
	char buff[256];
	static char prevLine[256] = { 'g', 'r', 'a', 'd', 'e', '\n', '\0'};
	printf("rootDir: %s\n", thisProject->rootDir);
	printf("current status queue: \n");
	const int printQueueSize = 10;
	char firstName[printQueueSize][64];
	char lastName[printQueueSize][64];
	int maxFirstNameLength = 0;
	int maxLastNameLength = 0;
	for(int i = 0; i < printQueueSize && i < thisProject->submissionCount - curSubmissionNum; i++)
	{
		firstName[i][64]; strncpy(firstName[i], thisProject->submissions[curSubmissionNum+i].studentName, strcspn(thisProject->submissions[curSubmissionNum+i].studentName, " ")+1); *strstr(firstName[i], " ") = '\0';
		lastName[i][64]; strcpy(lastName[i], strstr(thisProject->submissions[curSubmissionNum+i].studentName," ")+1); *strstr(lastName[i], " ") = ',';
		int tempFirstNameLen = strlen(firstName[i]);
		int tempLastNameLen = strlen(lastName[i]);
		if(tempFirstNameLen > maxFirstNameLength) maxFirstNameLength = tempFirstNameLen;
		if(tempLastNameLen > maxLastNameLength) maxLastNameLength = tempLastNameLen;	
	}
	for(int i = 0; i < printQueueSize && i < thisProject->submissionCount - curSubmissionNum; i++)
	{
		char* late;
		if(thisProject->submissions[curSubmissionNum+i].late == 0)
			late = "(Late)";
		else
			late = "(On Time)";
		printf("  %d. %*s %*s %*s\n", curSubmissionNum+i, -maxLastNameLength, lastName[i], -maxFirstNameLength, firstName[i], -10, late);
	}
	printf("   .\n   .\n   .\n\n");
	printf("options: \n");
	//printf("  save - Saves grading progress for future grading\n"); //didn't implement load yet, so no point in saving
	printf("  skip - Skips the next student for grading\n");
	printf("  skip %%N - grades students starting at element %d+N\n", curSubmissionNum);
	printf("  goto %%N - grades students starting at element N\n");
	printf("  gotos %%s - grades students starting at the first student dir begining with %%s\n");
	printf("  exit - Exits the program (don't forget to save first!)\n");
	printf("  grade - Grades the next student submission\n");
	printf("  *nothing* - Repeates previous skip command or Grades the next student submission\n");
	printf("\n[options] ");
	fgets(buff, 256, stdin);
	while(1)
	{
		if(strcmp(buff, "save\n") == 0)
		{
			sprintf(buff, "echo \"%d\" > %s/Grading_Materials/save.txt", curSubmissionNum, thisProject->rootDir);
			system(buff);
		}
		else if(strcmp(buff, "exit\n") == 0)
			{ FreeMem(thisProject); exit(1);}
		else if(strcmp(buff, "skip\n") == 0)
		{
			strcpy(prevLine, buff); 
			if(curSubmissionNum >= thisProject->submissionCount);
			return 1;
		}
		else if(strncmp(buff, "skip ", 5) == 0)
		{
			strcpy(prevLine, buff);	
			int retVal = atoi(buff+5);
			if(retVal >= 0 - curSubmissionNum && retVal != 0 && retVal < thisProject->submissionCount - curSubmissionNum)
				return retVal;
			else if(retVal == 0)
				printf("**already at %d**\n", curSubmissionNum);
			else 
				printf("**out of range**\n");
		}
		else if(strncmp(buff, "goto ", 5) == 0)
		{
			strcpy(prevLine, "grade\n");
			int retVal = atoi(buff+5)-curSubmissionNum;
			if(retVal >= 0 - curSubmissionNum && retVal != 0 && retVal < thisProject->submissionCount - curSubmissionNum)
				return retVal;
			else if(retVal == 0)
				printf("**already at %d**\n", curSubmissionNum);
			else 
				printf("**out of range**\n");
		}
		else if(strncmp(buff, "gotos ", 6) == 0)
		{
			buff[strcspn(buff, "\n")] = '\0';
			strcpy(prevLine, "grade\n");
			strcpy(buff, buff+6);
			for(int i = curSubmissionNum + 1; i != curSubmissionNum; i = (i+1) % thisProject->submissionCount)
				if(strstr(thisProject->submissions[i].studentDirName, buff) != NULL)
					return i - curSubmissionNum;
			printf("%s not found\n", buff);
		}
		else if(strcmp(buff, "grade\n") == 0)
		{
			strcpy(prevLine, "grade\n");
			return 0;
		}
		else if(strcmp(buff, "\n") == 0)
			if(strcmp(prevLine, "save\n") == 0)
			{
				sprintf(prevLine, "echo \"%d\" > %s/Grading_Materials/save.txt", curSubmissionNum, thisProject->rootDir);
				system(prevLine);
			}
			else if(strcmp(prevLine, "exit\n") == 0)
				{ FreeMem(thisProject); exit(1);}
			else if(strcmp(prevLine, "skip\n") == 0)
			{
				if(curSubmissionNum >= thisProject->submissionCount);
				return 1;
			}
			else if(strncmp(prevLine, "skip ", 5) == 0)
			{
				int retVal = atoi(prevLine+5);
				if(retVal >= 0 - curSubmissionNum && retVal != 0 && retVal < thisProject->submissionCount - curSubmissionNum)
					return retVal;
				else if(retVal == 0)
					printf("**already at %d**\n", curSubmissionNum);
				else 
					printf("**out of range**\n");
			}
			else if(strcmp(prevLine, "grade\n") == 0)
				return 0;
		else 
			printf("**invalid input**\n");
		strcpy(prevLine, buff);
		printf("[options] ");
		fgets(buff, 256, stdin);
	}
}

void sort(project* thisProject, submission* arr, int n){
	for(int i = 0; i < n; i++)
		for(int j = i; j < n; j++)
		{
			//sort in ascending order based on 2rd element of directory name, i.e. last name
			char* namei;
			char* namej;
			if(arr[i].late == 0)
				namei = strstr(arr[i].studentDirName + strlen(thisProject->rootDir), "_")+1;
			else
				namei = strstr(strstr(arr[i].studentDirName + strlen(thisProject->rootDir), "_")+1,"_")+1;

			if(arr[j].late == 0)
				namej = strstr(arr[j].studentDirName + strlen(thisProject->rootDir), "_")+1;
			else
				namej = strstr(strstr(arr[j].studentDirName + strlen(thisProject->rootDir), "_")+1,"_")+1;

			if(arr[i].late > arr[j].late || ( arr[i].late == arr[j].late && strcasecmp(namei, namej) > 0))
			{
				submission tmp = arr[i];
				arr[i] = arr[j];
				arr[j] = tmp;
			}
		}
}
	
int Filecmp(char* a, char* b){
	char buff[256];
	char buff2[256];
	sprintf(buff, "diff s %s %s > tmp.txt", a, b);
 	sprintf(buff, "Files %s and %s are identical\n", a, b);

	FILE *cmp = fopen("tmp.txt", "r");
	fgets(buff2, 256, cmp);
	fclose(cmp);
	system("rm tmp.txt");

	if(strcmp(buff2, buff) == 0)
		return 1;
	else
		return 0;
}

char* GetDiffCount(project* thisProject, int testID){
	char buff1[256];
	char buff2[256];
	char* diffString;
	sprintf(buff1, "grep \\< ./grading_output/test%ddiff.txt | wc -l > temp1.txt", testID);
	sprintf(buff2, "wc -l ./grading_output/test%d.out.txt > temp2.txt", testID);
	system(buff1);
	system(buff2);
	FILE* temp1 = fopen("temp1.txt", "r");
	FILE* temp2 = fopen("temp2.txt", "r");
	if(temp1 != NULL && temp2 != NULL)
	{
		fgets(buff1, 256, temp1); *strstr(buff1, "\n") = '\0';
		fgets(buff2, 256, temp2); *strstr(buff2, " ") = '\0';
		int diffLines = atoi(buff1);
		int totalLines = atoi(buff2);
		diffLines = totalLines - diffLines; //difflines is the number of lines which are different, more usefule
		sprintf(buff1, "%d", diffLines);       //  to use as a ratio of correct lines in solution file match.
		char color[16]; double diffPercent = (double)diffLines / (double)totalLines;
		if(diffPercent > 0.75) strcpy(color, "\e[0;32m");
		else if (diffPercent > 0.25) strcpy(color, "\e[0;93m");
		else strcpy(color, "\e[0;31m");

		diffString = (char*)malloc(sizeof(char)*256);
		sprintf(diffString, "%s[%s/%s]\e[0m", color, buff1, buff2);
	}
	else
	{
		diffString = (char*)malloc(sizeof(char)*256);
		strcpy(diffString, "[?/?]");
	}
	return diffString;
}	

double AssignPartialCredit(project* thisProject, char* studentOutput, int testID, submission* studentSubmission){
	char buf[256];
	partialcredit* cur = thisProject->tests[testID].partialCredit;
	partialcredit* prev = cur;
	int freeFlag = 0;
	char buff[512], buff2[256];
	char* matchesBuff = (char*)malloc(sizeof(char)*512); matchesBuff[0] = '\0';
	char* matchesBuffScore = (char*)malloc(sizeof(char)*512); matchesBuff[0] = '\0';
	sprintf(buff, "find %sGrading_Materials/partial_credits/test%d/ -type f -exec diff -s -q grading_output/test%d.out.txt '{}' \\; | grep -o \"and .* are identical\" | grep -o \"[0-9A-Za-z_./]*.txt\" > partialcreditmatches.txt", thisProject->rootDir, testID, testID);
	sprintf(buff2, "mkdir %sGrading_Materials/partial_credits/test%d 2>/dev/null", thisProject->rootDir, testID);
	system(buff2);
	system(buff);
	//system("rm partialcreditmatches.txt");
	FILE* matches;
	matches = fopen("partialcreditmatches.txt", "r");
	if(matches == NULL)
		{ printf("partialcreditmatches.txt not found\n"); exit(-1);}
	fgets(matchesBuff, 256, matches); 
	if(matchesBuff[0] != '\0')
	{
		*strstr(matchesBuff, "\n") = '\0';
		fclose(matches);
		sprintf(matchesBuffScore, "%s.score", matchesBuff);
		matches = fopen(matchesBuffScore, "r");
		if(matches == NULL)
		{
			printf("invalid path: %s\n", matchesBuffScore);
			exit(-1);
		}
		fgets(buff, 256, matches);
		fgets(buff2, 256, matches);
		fclose(matches);

		printf("\rTest %d: .......... \e[0;93m[Found Partial Credit]\e[0m\n", testID);
		printf("  Filename: %s\n", matchesBuff);
		printf("  Description: %s", buff2);
		printf("  Found matching partial credit (%.1f/%d) use? ('yes', 'no', 'subl', or 'remove') \n  ", atoi(buff), thisProject->tests[testID].maxScore);
		while(1)
		{
			fgets(buf, 256, stdin); 
			if(strcmp(buf, "no\n") == 0)
			{
				printf("\r  Test %d partial credit score: ", testID);
				fgets(buf, 256, stdin); 
				studentSubmission->studentScores[testID].score = atof(buf);
				printf("\r  Test %d partial credit description (max 256 characters): ", testID);
				fgets(buf, 256, stdin); buf[strcspn(buf, "\n")] = '\0';
				
				strcpy(studentSubmission->studentScores[testID].description, buf);
				return studentSubmission->studentScores[testID].score;
			}
			else if(strcmp(buf, "yes\n") == 0)
			{
				strcpy(studentSubmission->studentScores[testID].description, buff2);
				studentSubmission->studentScores[testID].score = atoi(buff);
				return studentSubmission->studentScores[testID].score;
			}
			else if(strcmp(buf, "remove\n") == 0)
			{
				printf("removing this partial credit\n");
				sprintf(buf, "rm %s %s", matchesBuffScore, matchesBuff);
				system(buf);
				printf("\r  Test %d partial credit score: ", testID);
				fgets(buf, 256, stdin); 
				studentSubmission->studentScores[testID].score = atof(buf);
				printf("\r  Test %d partial credit description (max 256 characters): ", testID);
				fgets(buf, 256, stdin); buf[strcspn(buf, "\n")] = '\0';

				strcpy(studentSubmission->studentScores[testID].description, buf);
				return studentSubmission->studentScores[testID].score;
			}
			else if(strcmp(buf, "subl\n") == 0)
				system("subl ./");
		}
	}
	else
	{
		char* diffCount = GetDiffCount(thisProject, testID);
		printf("\rTest %d: .......... \e[0;93m[Assigning Partial Credit]\e[0m %s\n", testID, diffCount);
		free(diffCount);
		printf("  Test %d: view output with sublime? ('yes' or 'no') ", testID);
		fgets(buf, 256, stdin);
		if(strcmp(buf, "yes\n") == 0)
			system("subl ./");
		printf("\r  Test %d: partial credit score: ", testID);
		fgets(buf, 256, stdin);
		double thisScore = atof(buf);
		printf("\r  Test %d partial credit description (max 256 characters): ", testID);
		fgets(buf, 256, stdin); buf[strcspn(buf, "\n")] = '\0';
		
		char buf2[256];
		sprintf(buf2, "%s -%.1f ", buf, thisProject->tests[testID].maxScore - thisScore);
		strcpy(buf, buf2);
		strcpy(studentSubmission->studentScores[testID].description, buf);
		studentSubmission->studentScores[testID].score = thisScore;

		/*
		cur = (partialcredit*)malloc(sizeof(partialcredit));
		cur->score = thisScore;
		strcpy(cur->description, buf);
		cur->studentPartialCreditFile = studentOutput;
		cur->next = thisProject->tests[testID].partialCredit;
		*/

		printf("\r  Save for automated grading? ('yes' or 'no') ");
		fgets(buf, 256, stdin);
		if(strcmp(buf,"yes\n") == 0)
		{	
			char buff1[512], buff2[512], buff3[512], nameBuff[256];
			strcpy(nameBuff, studentSubmission->studentName); *strstr(nameBuff, " ") = '_'; *strstr(nameBuff, " ") = '\0'; 
			sprintf(buff1, "mkdir %sGrading_Materials/partial_credits/test%d 2>/dev/null; ", thisProject->rootDir, testID);
			sprintf(buff2, "cp %s %sGrading_Materials/partial_credits/test%d/%s%d.txt; ", studentOutput, thisProject->rootDir, testID, nameBuff, testID);
			sprintf(buff3, "echo \"%1.1f\n%s\" > %sGrading_Materials/partial_credits/test%d/%s%d.txt.score;", thisScore, studentSubmission->studentScores[testID].description, thisProject->rootDir, testID, nameBuff, testID);
			strcat(buff1, buff2);
			strcat(buff1, buff3);
			system(buff1);
			//thisProject->tests[testID].partialCredit = cur;
		}
		else if(strcmp(buf,"no\n") == 0)
		{	
			freeFlag = 1;
		}
		return thisScore;

	}
	//fclose(matches); //TODO: FIX SEGFAULT

	//return 0.0;

	/*

	while(cur != NULL)
	{
		printf("%s\n", cur->studentPartialCreditFile);
		printf("%s\n", studentOutput);
		if(Filecmp(cur->studentPartialCreditFile, studentOutput) == 1)
			break;
		prev = cur;
		cur = cur->next;
	}
	if(cur == NULL)
	{
		char* diffCount = GetDiffCount(thisProject, testID);
		printf("\rTest %d: .......... \e[0;93m[Assigning Partial Credit]\e[0m %s\n", testID, diffCount);
		free(diffCount);
		printf("  Test %d: view output with sublime? ('yes' or 'no') ", testID);
		fgets(buf, 256, stdin);
		if(strcmp(buf, "yes\n") == 0)
			system("subl ./");
		printf("\r  Test %d: partial credit score: ", testID);
		fgets(buf, 256, stdin);
		double thisScore = atof(buf);
		printf("\r  Test %d partial credit description (max 256 characters): ", testID);
		fgets(buf, 256, stdin); buf[strcspn(buf, "\n")] = '\0';
		
		char buf2[256];
		sprintf(buf2, "%s -%.1f ", buf, thisProject->tests[testID].maxScore - thisScore);
		strcpy(buf, buf2);

		cur = (partialcredit*)malloc(sizeof(partialcredit));
		cur->score = thisScore;
		strcpy(cur->description, buf);
		strcpy(studentSubmission->studentScores[testID].description, buf);
		cur->studentPartialCreditFile = studentOutput;
		cur->next = thisProject->tests[testID].partialCredit;
		
		printf("\r  Save for automated grading? ('yes' or 'no') ");
		fgets(buf, 256, stdin);
		if(strcmp(buf,"yes\n") == 0)
		{	
			char buff1[512], buff2[512], buff3[512], nameBuff[256];
			strcpy(nameBuff, studentSubmission->studentName); *strstr(nameBuff, " ") = '_'; *strstr(nameBuff, " ") = '\0'; 
			sprintf(buff1, "mkdir %sGrading_Materials/partial_credits/test%d 2>/dev/null; ", thisProject->rootDir, testID);
			sprintf(buff2, "cp %s %sGrading_Materials/partial_credits/test%d/%s%d.txt; ", studentOutput, thisProject->rootDir, testID, nameBuff, testID);
			sprintf(buff3, "echo \"%1.1f\n%s\" > %sGrading_Materials/partial_credits/test%d/%s%d.txt.score;", thisScore, studentSubmission->studentScores[testID].description, thisProject->rootDir, testID, nameBuff, testID);
			strcat(buff1, buff2);
			strcat(buff1, buff3);
			system(buff1);
			thisProject->tests[testID].partialCredit = cur;
		}
		else if(strcmp(buf,"no\n") == 0)
		{	
			freeFlag = 1;
		}
		
	}
	else
	{
		while(1)
		{
			
			printf("\rTest %d: Found matching partial credit (%.1f/%d) use? ('yes', 'no', or 'subl') \n", testID, cur->score, thisProject->tests[testID].maxScore);
			printf("  Description: %s\n", cur->description);
			fgets(buf, 256, stdin); 
			if(strcmp(buf, "no\n") == 0)
			{
				printf("\r  Test %d partial credit score: ", testID);
				fgets(buf, 256, stdin); 
				studentSubmission->studentScores[testID].score = atof(buf);
				printf("\r  Test %d partial credit description (max 256 characters): ", testID);
				fgets(buf, 256, stdin); buf[strcspn(buf, "\n")] = '\0';
				
				strcpy(studentSubmission->studentScores[testID].description, buf);
				return studentSubmission->studentScores[testID].score;
			}
			else if(strcmp(buf, "yes\n") == 0)
				break;

			else if(strcmp(buf, "subl\n") == 0)
				system("subl ./");
		}
	}

	studentSubmission->studentScores[testID].score = cur->score;
	if(freeFlag == 1)
	{
		double score = cur->score;
		free(cur);
		return score;
	}
	return cur->score;
	*/
}

project *ReadTestCases(char* rootDir){
	const char* testSubDir = "Grading_Materials/test_cases/";
	char dirName[256];
	sprintf(dirName, "%s%s", rootDir, testSubDir);

	DIR *testsDir;
	if ((testsDir = opendir(dirName)) == NULL)
		{printf("cannot open dir: %s\nUse absolute file names.\n", dirName); exit(-1);}
	
	struct dirent *curFile;
	int numTests = 1;
	int numSolutions = 1;
	int numValgrindTests = 1;
	while((curFile = readdir(testsDir)) != NULL)
	{
		if(strncmp(curFile->d_name, "test", 4) == 0)
			numTests++;
		else if (strncmp(curFile->d_name, "solution", 8) == 0)
			numSolutions++;
		else if (strncmp(curFile->d_name, "valgrind", 8) == 0)
			numValgrindTests++;
	}
	closedir(testsDir);

	if(numTests != numSolutions)
		{printf("there are not a matching number of tests and solutions\n"); exit(-1);}

	testsDir = opendir(dirName);
	project *thisProject = (project*)malloc(sizeof(project));
	thisProject->tests = (test*)malloc(sizeof(test)*numTests);
	thisProject->valTests = (test*)malloc(sizeof(test)*numTests);
	for(int i = 0; i < numTests; i++) thisProject->valTests[i].testFileName = NULL;
	while((curFile = readdir(testsDir)) != NULL)
	{
		if(strncmp(curFile->d_name, "test", 4) == 0)
		{
			int testNum = atoi(curFile->d_name+4);
			if(testNum >= numTests) {printf("Test cases do not increment from 1\n"); exit(-1);}

			thisProject->tests[testNum].testFileName = (char*)malloc(sizeof(char)*(strlen(curFile->d_name)+strlen(rootDir)+strlen("Grading_Materials/test_cases/")+1));
			sprintf(thisProject->tests[testNum].testFileName, "%s%s%s", rootDir, "Grading_Materials/test_cases/", curFile->d_name);
			thisProject->tests[testNum].testID = testNum;
			thisProject->tests[testNum].partialCredit = NULL;
		}
		else if (strncmp(curFile->d_name, "solution", 8) == 0)
		{
			int solutionNum = atoi(curFile->d_name+8);
			if(solutionNum >= numSolutions) {printf("Test cases do not increment from 1\n"); exit(-1);}

			thisProject->tests[solutionNum].solutionFileName = (char*)malloc(sizeof(char)*(strlen(curFile->d_name)+strlen(rootDir)+strlen("Grading_Materials/test_cases/")+1));
			sprintf(thisProject->tests[solutionNum].solutionFileName, "%s%s%s", rootDir, "Grading_Materials/test_cases/", curFile->d_name);
			thisProject->tests[solutionNum].testID = solutionNum;	//superfluous
			thisProject->tests[solutionNum].partialCredit = NULL;	
		}
		else if (strncmp(curFile->d_name, "valgrind", 8) == 0)
		{
			int valgrindNum = atoi(curFile->d_name+8);
			if(valgrindNum >= numTests || valgrindNum < 1) {printf("Invalid Valgrind test name %s\n", curFile->d_name); exit(-1);}
			thisProject->valTests[valgrindNum].testFileName = (char*)malloc(sizeof(char)*(strlen(curFile->d_name)+strlen(rootDir)+strlen("Grading_Materials/test_cases/")+1));
			sprintf(thisProject->valTests[valgrindNum].testFileName, "%s%s%s", rootDir, "Grading_Materials/test_cases/", curFile->d_name);
			thisProject->valTests[valgrindNum].testID = valgrindNum;
			thisProject->valTests[valgrindNum].partialCredit = NULL;
		}
	}
	closedir(testsDir);
	for(int i = 1; i < numTests; i++)
	{
		if(thisProject->tests[i].solutionFileName == NULL || thisProject->tests[i].testFileName == NULL)
			{printf("Missing test or solution %d\n", i); exit(-1);}
	}

	const int defaultMax = 10;
	thisProject->submissions = NULL;
	thisProject->testCount = numTests;
	thisProject->rootDir = (char*)malloc(sizeof(char)*(strlen(rootDir)+1));
	for(int i = 0; i < defaultMax; i++)
	{
		thisProject->tests[i].partialCredit = NULL;
		//thisProject->tests[i].partialCredit = (partialcredit*)malloc(sizeof(partialcredit)*defaultMax);
	}
	strcpy(thisProject->rootDir, rootDir);
	return thisProject;
}	

void ReadStudentDirs(project* thisProject){
	char buff[256]; strcpy(buff, "");
	char buff2[256]; strcpy(buff2, "");
	char* onTimeDirName = (char*)malloc(sizeof(char)*(strlen(thisProject->rootDir)+strlen("On_Time/")+1));
	char* lateDirName = (char*)malloc(sizeof(char)*(strlen(thisProject->rootDir)+strlen("Late/")+1));
	strcpy(onTimeDirName, thisProject->rootDir); strcat(onTimeDirName, "On_Time/");
	strcpy(lateDirName, thisProject->rootDir); strcat(lateDirName, "Late/");	

	DIR *onTimeDir;
	DIR *lateDir;

	if((onTimeDir = opendir(onTimeDirName)) == NULL) {printf("cannot open dir: %s\n", onTimeDirName); exit(-1);}
	if((lateDir = opendir(lateDirName)) == NULL) {printf("cannot open dir: %s\n", lateDirName); exit(-1);}

	int numSubmissions = 0;
	struct dirent *curDirElem = NULL;

	while((curDirElem = readdir(onTimeDir)) != NULL)
		if(curDirElem->d_name[0] != '.')
			numSubmissions++;
	while((curDirElem = readdir(lateDir)) != NULL)
		if(curDirElem->d_name[0] != '.')
			numSubmissions++;

	closedir(onTimeDir);
	closedir(lateDir);
	if((onTimeDir = opendir(onTimeDirName)) == NULL) {printf("cannot open dir: %s\n", onTimeDirName); exit(-1);}
	if((lateDir = opendir(lateDirName)) == NULL) {printf("cannot open dir: %s\n", lateDirName); exit(-1);}
	free(onTimeDirName);
	free(lateDirName);
	thisProject->submissions = (submission*)malloc(sizeof(submission)*numSubmissions);
	thisProject->submissionCount = numSubmissions;

	int curSubmission = 0;
	while((curDirElem = readdir(onTimeDir)) != NULL)
		if(curDirElem->d_name[0] != '.')
		{
			thisProject->submissions[curSubmission].studentScores = (score*)malloc(sizeof(score)*thisProject->testCount);
			thisProject->submissions[curSubmission].studentName = NULL;
			thisProject->submissions[curSubmission].studentDirName = (char*)malloc(sizeof(char)*(strlen(thisProject->rootDir)+strlen("On_Time/")+strlen(curDirElem->d_name)+1));
			sprintf(thisProject->submissions[curSubmission].studentDirName, "%sOn_Time/%s", thisProject->rootDir, curDirElem->d_name);
			thisProject->submissions[curSubmission].late = 1;
			thisProject->submissions[curSubmission].total = 0;
			strcpy(buff, curDirElem->d_name);
			thisProject->submissions[curSubmission].studentName = (char*)malloc(sizeof(char)*(strlen(buff)+1));
			char* tok;
			strcpy(buff2, strtok(buff, "_")); strcat(buff2, " ");
			while(strstr((tok = strtok(NULL, "_")), ".") == NULL) 
				{strcat(buff2, tok); strcat(buff2, " ");}
			strcpy(thisProject->submissions[curSubmission].studentName, buff2);

			curSubmission++;
		}
	while((curDirElem = readdir(lateDir)) != NULL)
		if(curDirElem->d_name[0] != '.')
		{
			thisProject->submissions[curSubmission].studentScores = (score*)malloc(sizeof(score)*thisProject->testCount);
			thisProject->submissions[curSubmission].studentName = NULL;
			thisProject->submissions[curSubmission].studentDirName = (char*)malloc(sizeof(char)*(strlen(thisProject->rootDir)+strlen("Late/")+strlen(curDirElem->d_name)+1));
			strcpy(thisProject->submissions[curSubmission].studentDirName, thisProject->rootDir); 
			strcat(thisProject->submissions[curSubmission].studentDirName, "Late/"); 
			strcat(thisProject->submissions[curSubmission].studentDirName, curDirElem->d_name);
			thisProject->submissions[curSubmission].late = 0;
			thisProject->submissions[curSubmission].total = 0;
			strcpy(buff, curDirElem->d_name);
			thisProject->submissions[curSubmission].studentName = (char*)malloc(sizeof(char)*(strlen(buff)+1));
			char* tok;
			strcpy(buff2, strtok(buff, "_")); strcat(buff2, " ");
			while(strstr((tok = strtok(NULL, "_")), ".") == NULL) 
				{strcat(buff2, tok); strcat(buff2, " ");}
			strcpy(thisProject->submissions[curSubmission].studentName, buff2);

			curSubmission++;
		}

	closedir(onTimeDir);
	closedir(lateDir);
	sort(thisProject, thisProject->submissions, thisProject->submissionCount);
	return;
}

void ValgrindTest(project* thisProject, char command[], int j){
	char buff[256];
	char buff2[256];
	printf("Valgrind %d ...... \e[0;93m[running]\e[0m", j);
	fflush(stdout);
	sprintf(buff, "%s 2>&1 < ../../Grading_Materials/test_cases/test%d.txt | grep \"total heap usage\" > temp.txt", command, j);
	system(buff);
	FILE* valOut = fopen("temp.txt", "r");
	if(valOut != NULL)
	{
		fgets(buff2, 256, valOut);
		if(strcmp(buff, buff2) != 0)
		{
			strcpy(buff, buff2);
			char* buffPtr = &buff[strcspn(buff, ":")];
			char* allocs = strtok(buffPtr+2, " allocs");
			strtok(NULL, " ");
			char* frees  = strtok(NULL, " ");
			double freePercentage = (double)atoic(frees) / (double)atoic(allocs);
			char color[32];
			if(freePercentage >= 0.9)
				strcpy(color, "\e[0;32m");
			else if(freePercentage >= 0.5)
				strcpy(color, "\e[0;93m");
			else
				strcpy(color, "\e[0;31m");
			sprintf(buff2, "\rValgrind %d ...... %s[%.1f%%]\e[0m %s (frees) / %s (allocs)\n", j, color, freePercentage*100, frees, allocs);
			printf("\r                            \r");
			printf("%s", buff2);
		}
		else
		{
			printf("\rValgrind %d ...... \e[0;31m[interupted]\e[0m\n", j);
		}
		fclose(valOut);
		system("rm temp.txt");
	}
	else
	{
		printf("\rValgrind %d ...... \e[0;31m[no output]\e[0m\n", j);
	}
}

void GradeSubmissions(project* thisProject){
	const int buffSize = 128;
	char buf[buffSize];
	for(int i = 0; i < thisProject->submissionCount; i++)
	{
		system("clear");
		int status = ShowStatus(thisProject, i);
		if(status != 0)
		{
			i += status - 1;
			continue;
		}
		system("clear");
		char *late;
		if (0 == thisProject->submissions[i].late)
			late = "Late";
		else
			late = "On Time";
		printf("Student Name: %s- %s\n", thisProject->submissions[i].studentName, late);
		printf("Folder:  %s\n\n", thisProject->submissions[i].studentDirName);

		chdir(thisProject->submissions[i].studentDirName);

		system("rm -r -f grading_output");
		RemoveLocalSymLinks();
		SetUpLocalSymLinks(thisProject->rootDir);

		system("mkdir -p grading_output");
		system("make -s compile");
		for(int j = 1; j < thisProject->testCount; j++)
		{
			char buff1[256];
			sprintf(buff1, "./a.out < %s 1> grading_output/test%d.out.txt 2> grading_output/error.txt", thisProject->tests[j].testFileName, j);

			char buff2[256];
			sprintf(buff2, "grading_output/test%ddiff.txt", j);

			char buff3[256];
			sprintf(buff3, "grading_output/test%d.out.txt", j);

			char buff4[256];
			sprintf(buff4, "diff grading_output/test%d.out.txt -s %s > grading_output/test%ddiff.txt", j, thisProject->tests[j].solutionFileName, j);
			//sprintf(buff4, "colordiff grading_output/test%d.out.txt -s %s > grading_output/test%ddiff.txt", j, thisProject->tests[j].solutionFileName, j);

			printf("Test %d: .......... \e[0;93m[running]\e[0m", j);
			fflush(stdout);

			system(buff1);
			system(buff4);
			system("rm grading_output/error.txt");
			
			char buff6[256];
			sprintf(buff6, "Files grading_output/test%d.out.txt and %s are identical\n", j, thisProject->tests[j].solutionFileName);
			
			FILE* diff;
			if((diff = fopen(buff2, "r")) == NULL) {printf("could not open: %s", buff2); exit(-1);}
			char fileInput[512];
			fgets(fileInput, 512, diff);
			fclose(diff);
			if(strcmp(fileInput, buff6) == 0)
			{
				thisProject->submissions[i].studentScores[j].score = (double)thisProject->tests[j].maxScore;
				strcpy(thisProject->submissions[i].studentScores[j].description, "");
				printf("\rTest %d: .......... \e[0;32m%s\e[0m %.1f/%d\n", j, "[passed]", thisProject->submissions[i].studentScores[j].score, thisProject->tests[j].maxScore);
			}
			else
			{
				thisProject->submissions[i].studentScores[j].score = AssignPartialCredit(thisProject, buff3, j, &(thisProject->submissions[i]));
				printf("\rTest %d: .......... ", j);
				if(thisProject->submissions[i].studentScores[j].score == 0)
					printf("\e[0;31m%s\e[0m", "[failed]");
				else
					printf("\e[0;93m%s\e[0m", "[partial credit]");

				printf(" %.1f/%d\n", thisProject->submissions[i].studentScores[j].score, thisProject->tests[j].maxScore);
			}
		}

		for(int j = 1; j < thisProject->testCount; j++)
		{
			char buff[256];
			char buff2[256];
			if(thisProject->valTests[j].testFileName == NULL) continue;
			FILE* valFile = fopen(thisProject->valTests[j].testFileName, "r");
			fgets(buff2, 256, valFile); 
			fclose(valFile);
			char* tmp = strstr(buff2, "\n");
			if(tmp != NULL) *tmp = '\0';
			
			ValgrindTest(thisProject, buff2, j);		
		}

		printf("\nAditional commands\n'next' or blank to continue to the next student\n\n");
		printf("[Grading] ");
		fgets(buf, buffSize, stdin);
		buf[strcspn(buf, "\n")] = '\0';
		while(strcmp(buf, "next") != 0 && strcmp(buf, "\0") != 0)
		{
			if(strcmp(buf, "exit") == 0)
				{ FreeMem(thisProject); exit(1);}
			else if(strcmp(buf, "subl diff") == 0)
			{
				char thisTestNum[32];
				sprintf(thisTestNum,"%d", thisProject->testCount);
				char* sublDiff = (char*)malloc(sizeof(char)*(strlen("subl -n")+(strlen(thisTestNum)*thisProject->testCount)*strlen(" grading_output/testdiff.txt")+1));
				strcpy(sublDiff, "subl -n");
				for(int i = 1; i < thisProject->testCount; i++)
				{
					sprintf(thisTestNum,"%d", i);
					strcat(sublDiff, " grading_output/test");
					strcat(sublDiff, thisTestNum);
					strcat(sublDiff, "diff.txt");
				}
				system(sublDiff);
				free(sublDiff);
			}
			else if (strcmp(buf, "subl out") == 0)
			{
				char thisTestNum[32];
				sprintf(thisTestNum,"%d", thisProject->testCount);
				char* sublOutput = (char*)malloc(sizeof(char)*(strlen("subl -n")+(strlen(thisTestNum)*thisProject->testCount)*(strlen(" grading_output/test.out.txt")+strlen(thisTestNum))+1));
				strcpy(sublOutput, "subl -n");
				for(int i = 1; i < thisProject->testCount; i++)
				{
					sprintf(thisTestNum,"%d", i);
					strcat(sublOutput, " grading_output/test");
					strcat(sublOutput, thisTestNum);
					strcat(sublOutput, ".out.txt");
				}
				//printf("%s\n", sublOutput);
				system(sublOutput);
				free(sublOutput);
			}
			else if (strcmp(buf, "subl all") == 0)
			{
				system("subl -n ./");
			}
			else if (strncmp(buf, "valgrind ", strlen("valgrind ")) == 0){
				ValgrindTest(thisProject, "valgrind ./a.out --leak-check=full", atoi(buf+strlen("valgrind ")));
			}
			else 	
				system(buf);

			printf("[Grading] ");
			fgets(buf, buffSize, stdin);
			buf[strcspn(buf, "\n")] = '\0';
		}

		printf("Grade Summary: \n\n");
		for(int j = 1; j < thisProject->testCount; j++)
		{
			if(thisProject->submissions[i].studentScores[j].score < 0.1)
				printf("Test %d: \e[0;91m%.1f/%d\e[0m\n", j, thisProject->submissions[i].studentScores[j].score, thisProject->tests[j].maxScore);
			else if(thisProject->submissions[i].studentScores[j].score < (double)thisProject->tests[j].maxScore - 0.1)
				printf("Test %d: \e[0;93m%.1f/%d\e[0m\n", j, thisProject->submissions[i].studentScores[j].score, thisProject->tests[j].maxScore);
			else 
				printf("Test %d: \e[0;32m%.1f/%d\e[0m\n", j, thisProject->submissions[i].studentScores[j].score, thisProject->tests[j].maxScore);
		}

		int descFlag = 0;
		printf("Description: ");
		for(int j = 1; j < thisProject->testCount; j++)
		{
			if(strlen(thisProject->submissions[i].studentScores[j].description) != 0)
			{
				if(descFlag++ != 0)
					printf("; ");
				printf("Test %d: %s", j, thisProject->submissions[i].studentScores[j].description);
			}
		}
		printf(".\nContinue...");
		fgets(buf, 256, stdin);
	}
}

void GetMaxScores(project* thisProject){
	chdir(thisProject->rootDir);
	FILE *maxScoresFile = fopen("Grading_Materials/test_cases/scores.txt", "r");
	if(maxScoresFile == NULL) {printf("cannot read: scores.txt\n"); exit(-1);}
	char buf[64];
	fgets(buf, 64, maxScoresFile);
	while(!feof(maxScoresFile))
	{
		int testNum = atoi(strtok(buf, " "));
		int testScore = atoi(strtok(NULL, "\n"));
		thisProject->tests[testNum].maxScore = testScore;
		fgets(buf, 64, maxScoresFile);
	}
	fclose(maxScoresFile);
}

int main(int argc, char*argv[]){
	char rootDir[256]; getcwd(rootDir, 245); 
	char* tmp = &rootDir[0]; while(1) if(strcmp(tmp, "Grading_Materials") == 0) break; else tmp++; *tmp = '\0';

	project *thisProject;
	thisProject = ReadTestCases(rootDir);
	GetMaxScores(thisProject);
	ReadStudentDirs(thisProject);
	GradeSubmissions(thisProject);

	FreeMem(thisProject);
	exit(1);
}
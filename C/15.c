/*************************************************************************
    > File Name: 15.c
    > Author: ma6174
    > Mail: ma6174@163.com 
    > Created Time: 2016年02月26日 星期五 22时00分12秒
 ************************************************************************/

#include<stdio.h>

int main()
{
	int a, b, c;
	for(a = 1; a < 3; a++)
		for(b= 1; b< 3; b++)
			for(c = 1; c < 3; c++)
			{
				if (a != 1 && c != 1 && c != 3 && a != b && b != c && a != c)

		    	{
	
					printf("a与%c结婚\n"，'x'+a-1);
					printf("b与%c结婚\n"，'x'+b-1);
					printf("c与%c结婚\n"，'x'+c-1);
				}
			}
	return 0;
}
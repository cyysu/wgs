/*************************************************************************
    > File Name: 13.c
    > Author: ma6174
    > Mail: ma6174@163.com 
    > Created Time: 2016年02月25日 星期四 22时01分51秒
 ************************************************************************/

#include<stdio.h>

int main()
{
	int a,b,c,d,e;
	int s = 1;

	scanf("%d%d",&a,&b);
	for(int i = 1; i <= b; i++)
	{
		s = s * a;
	}
	s = s % 1000;
	c = s / 100;
	d = (s - (c * 100)) / 10;
	e = s - c * 100 - d * 10;
	printf("%d %d %d\n",c,d,e);

	return 0;
}

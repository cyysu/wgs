/*************************************************************************
	> File Name: b.c
	> Author: ma6174
	> Mail: ma6174@163.com 
	> Created Time: 2016年03月18日 星期五 15时31分36秒
 ************************************************************************/

int shared = 1;
void swap( int *a, int *b)
{
	*a ^=  *b ^= *a ^= *b;
}

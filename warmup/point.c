#include <assert.h>
#include "common.h"
#include "point.h"
#include <math.h>

void
point_translate(struct point *p, double x, double y)
{
    p->x=p->x+x;
    p->y=p->y+y;
}

double
point_distance(const struct point *p1, const struct point *p2)
{
    double distance;
    distance=sqrt(pow((p1->x-p2->x),2)+pow((p1->y-p2->y),2));
	return distance;
}

int
point_compare(const struct point *p1, const struct point *p2)
{
    struct point origin;
    origin.x=0;
    origin.y=0;
    struct point *origin_ptr=&origin;
    double euc_distance_1=point_distance(p1,origin_ptr);
    double euc_distance_2=point_distance(p2,origin_ptr);
    if (euc_distance_1==euc_distance_2)
	return 0;
    if (euc_distance_1<euc_distance_2)
        return -1;
    if (euc_distance_1>euc_distance_2)
        return 1;
    return 0;
}

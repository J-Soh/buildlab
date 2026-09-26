#include <math.h>
#include "cprocessing.h"
#include <stdio.h>
#include <stdbool.h>

int IsAreaClicked(float area_center_x, float area_center_y, float area_width, float area_height, float click_x, float click_y)
{
	float left = area_center_x - (area_width / 2.0f);
	float right = area_center_x + (area_width / 2.0f);
	float top = area_center_y - (area_height / 2.0f);
	float bottom = area_center_y + (area_height / 2.0f);

	if (click_x > left && click_x < right && click_y > top && click_y < bottom)
	{
		return 1;
	}

	return 0;
}

int IsCircleClicked(float circle_center_x, float circle_center_y, float diameter, float click_x, float click_y)
{
	float radius = diameter / 2.0f;
	float dx = click_x - circle_center_x;
	float dy = click_y - circle_center_y;

	// math collision optimization formula
	// mouse touches ball only if d^2 less than or equals to (r_1 + r_2)^2
	if (((dx * dx) + (dy * dy)) <= (radius * radius))
	{
		return 1;
	}

	return 0;
}

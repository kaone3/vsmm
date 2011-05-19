#define ROUND(x) ((int)((x) + ((x) > 0 ? 0.5 : -0.5)))

int round(float x)
{
	return ROUND(x);
}

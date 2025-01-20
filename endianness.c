#include <stdio.h>
#include <stdint.h>

int main()
{
	uint32_t num = 0x12345678;
	uint8_t *bytePtr = (uint8_t *)&num;

	printf("Memory order: ");
	for (int i = 0; i < sizeof(num); i++)
	{
		printf("0x%02X ", bytePtr[i]);
	}

	printf("\n");
	if (bytePtr[0] == 0x78)
	{
		printf("Little-Endian\n");
	}
	else
	{
		printf("Big-Endian\n");
	}

	return 0;
}


/*
=================
=
= VL_MungePic
=
=================
*/

#include "SDL2/SDL.h"

void VL_MungePic (unsigned char *source, unsigned width, unsigned height)
{
	unsigned	x,y,plane,size,pwidth;
	unsigned char	*temp, *dest, *srcline;

	size = width*height;

	if (width&3) {
		SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,"VL_MungePic: Not divisable by 4! %d\n", width);
	}

//
// copy the pic to a temp buffer
//
	temp = (unsigned char *)malloc (size);
	if (!temp) {
		SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Non enough memory for munge buffer!\n");
		exit(1);
	}

	memcpy(temp,source,size);

//
// munge it back into the original buffer
//
	dest = source;
	pwidth = width/4;

	for (plane=0;plane<4;plane++)
	{
		srcline = temp;
		for (y=0;y<height;y++)
		{
			for (x=0;x<pwidth;x++)
				*dest++ = *(srcline+x*4+plane);
			srcline+=width;
		}
	}

	free (temp);
}


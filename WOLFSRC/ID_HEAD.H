#ifndef IDHEAD_H
#define	IDHEAD_H

// ID_HEAD.H


#define	EXTENSION	"WLF"
#define WOLF

#define	TEXTGR	0
#define	CGAGR	1
#define	EGAGR	2
#define	VGAGR	3

#define GRMODE	VGAGR

#include <stdint.h>

typedef	uint8_t		byte;
typedef	uint16_t	word;
typedef	uint32_t	longword;
typedef	byte		*Ptr;

typedef	struct {
	int	x, y;
} Point;

typedef	struct {
	Point ul, lr;
} Rect;


void	Quit (char *error);		// defined in user program

#endif

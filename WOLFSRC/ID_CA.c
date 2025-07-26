// ID_CA.C

// this has been customized for WOLF

/*
=============================================================================

Id Software Caching Manager
---------------------------

Must be started BEFORE the memory manager, because it needs to get the headers
loaded into the data segment

=============================================================================
*/

#include "ID_HEADS.H"
#pragma hdrstop

#pragma warn -pro
#pragma warn -use

#define THREEBYTEGRSTARTS

/*
=============================================================================

						 LOCAL CONSTANTS

=============================================================================
*/

typedef struct
{
	uint16_t bit0,bit1;	// 0-255 is a character, > is a pointer to a node
} huffnode;


typedef struct
{
	uint16_t	RLEWtag;
	int32_t		headeroffsets[100];
	byte		tileinfo[];
} mapfiletype;


/*
=============================================================================

						 GLOBAL VARIABLES

=============================================================================
*/

byte 		*tinf;
int			mapon;

word		*mapsegs[MAPPLANES];
maptype		*mapheaderseg[NUMMAPS];
byte		*audiosegs[NUMSNDCHUNKS];
byte		*grsegs[NUMCHUNKS];

byte		grneeded[NUMCHUNKS];
byte		ca_levelbit,ca_levelnum;

int			profilehandle;
PHYSFS_File *debughandle;

char		audioname[13]="AUDIO.";

/*
=============================================================================

						 LOCAL VARIABLES

=============================================================================
*/

extern	long	CGAhead;
extern	long	EGAhead;
extern	byte	CGAdict;
extern	byte	EGAdict;
extern	byte	maphead;
extern	byte	mapdict;
extern	byte	audiohead;
extern	byte	audiodict;


char extension[5],	// Need a string, not constant to change cache files
     gheadname[10]=GREXT"HEAD.",
     gfilename[10]=GREXT"GRAPH.",
     gdictname[10]=GREXT"DICT.",
     mheadname[10]="MAPHEAD.",
     mfilename[10]="MAPTEMP.",
     aheadname[10]="AUDIOHED.",
     afilename[10]="AUDIOT.";

void CA_CannotOpen(char *string);

memptr		*grstarts;	// array of offsets in egagraph, -1 for sparse
memptr		*audiostarts;	// array of offsets in audio / audiot

#ifdef GRHEADERLINKED
huffnode	*grhuffman;
#else
huffnode	grhuffman[255];
#endif

#ifdef AUDIOHEADERLINKED
huffnode	*audiohuffman;
#else
huffnode	audiohuffman[255];
#endif


PHYSFS_File *grhandle;		// handle to EGAGRAPH
PHYSFS_File	*maphandle;		// handle to MAPTEMP / GAMEMAPS
PHYSFS_File	*audiohandle;	// handle to AUDIOT / AUDIO

long		chunkcomplen,chunkexplen;

SDMode		oldsoundmode;



void CAL_CarmackExpand (byte *source, word *dest, unsigned length);


#ifdef THREEBYTEGRSTARTS
#define FILEPOSSIZE	3
//#define	GRFILEPOS(c) (*(long*)(((byte*)grstarts)+(c)*3)&0xffffff)
long GRFILEPOS(int c)
{
	byte *offset = (byte*)grstarts + (c * 3);

	long value = offset[0] | (offset[1] << 8) | (offset[2] << 16);

	if (value == 0xffffff)
		value = -1;

	return value;

	// return (long)grstarts[c];
};
#else
#define FILEPOSSIZE	4
#define	GRFILEPOS(c) (grstarts[c])
#endif

/*
=============================================================================

					   LOW LEVEL ROUTINES

=============================================================================
*/

/*
============================
=
= CA_OpenDebug / CA_CloseDebug
=
= Opens a binary file with the handle "debughandle"
=
============================
*/

void CA_OpenDebug (void)
{
	PHYSFS_delete("DEBUG.TXT");
	debughandle = PHYSFS_openWrite("DEBUG.TXT");;
}

void CA_CloseDebug (void)
{
	PHYSFS_close(debughandle);
}



/*
============================
=
= CAL_GetGrChunkLength
=
= Gets the length of an explicit length chunk (not tiles)
= The file pointer is positioned so the compressed data can be read in next.
=
============================
*/

void CAL_GetGrChunkLength (int chunk)
{
	PHYSFS_seek(grhandle,GRFILEPOS(chunk));
	PHYSFS_readBytes(grhandle, &chunkexplen,sizeof(chunkexplen));
	chunkcomplen = GRFILEPOS(chunk+1)-GRFILEPOS(chunk)-4;
}


/*
==========================
=
= CA_FarRead
=
= Read from a file to a pointer
=
==========================
*/

bool CA_FarRead (PHYSFS_File *handle, byte *dest, long length)
{
	const PHYSFS_sint64 bytesRead = PHYSFS_readBytes(handle, dest, length);
	if (bytesRead == -1 || bytesRead != length) {
		return false;
	}

	return true;
}


/*
==========================
=
= CA_SegWrite
=
= Write from a file to a pointer
=
==========================
*/

bool CA_FarWrite (PHYSFS_File *handle, byte *source, long length)
{
	return PHYSFS_writeBytes(handle, source, length) != length;
}


/*
==========================
=
= CA_ReadFile
=
= Reads a file into an allready allocated buffer
=
==========================
*/

bool CA_ReadFile (char *filename, memptr *ptr)
{
	PHYSFS_File *handle = PHYSFS_openRead(filename);

	if (handle == NULL)
		return false;

	const long size = PHYSFS_fileLength(handle);
	if (!CA_FarRead(handle, *ptr, size))
	{
		PHYSFS_close(handle);
		return false;
	}

	PHYSFS_close(handle);
	return true;
}


/*
==========================
=
= CA_WriteFile
=
= Writes a file from a memory buffer
=
==========================
*/

bool CA_WriteFile (char *filename, void *ptr, long length)
{
	PHYSFS_File *handle = PHYSFS_openWrite(filename);

	if (handle == NULL)
		return false;

	if (!CA_FarWrite(handle, ptr, length))
	{
		PHYSFS_close(handle);
		return false;
	}

	PHYSFS_close(handle);
	return true;
}



/*
==========================
=
= CA_LoadFile
=
= Allocate space for and load a file
=
==========================
*/

bool CA_LoadFile (char *filename, memptr **ptr)
{
	PHYSFS_File *handle = PHYSFS_openRead(filename);
	long size;

	if (handle == NULL)
		return false;

	size = PHYSFS_fileLength(handle);
	MM_GetPtr (ptr, size);
	if (!CA_FarRead (handle, **ptr, size))
	{
		PHYSFS_close(handle);
		return false;
	}
	PHYSFS_close(handle);
	return true;
}

/*
============================================================================

		COMPRESSION routines, see JHUFF.C for more

============================================================================
*/



/*
===============
=
= CAL_OptimizeNodes
=
= Goes through a huffman table and changes the 256-511 node numbers to the
= actular address of the node.  Must be called before CAL_HuffExpand
=
===============
*/

void CAL_OptimizeNodes (huffnode *table)
{
  huffnode *node;
  int i;

  node = table;

  for (i=0;i<255;i++)
  {
	if (node->bit0 >= 256)
	  node->bit0 = (unsigned)(table+(node->bit0-256));
	if (node->bit1 >= 256)
	  node->bit1 = (unsigned)(table+(node->bit1-256));
	node++;
  }
}



/*
======================
=
= CAL_HuffExpand
=
= Length is the length of the EXPANDED data
= If screenhack, the data is decompressed in four planes directly
= to the screen
=
======================
*/

void CAL_HuffExpand (byte *source, byte *dest, long length,huffnode *hufftable, bool screenhack)
{
	byte *end;
	huffnode *headptr, *huffptr;

	if(!length || !dest)
	{
		Quit("length or dest is null!");
		return;
	}

	headptr = hufftable+254;        // head node is always node 254

	int written = 0;

	end=dest+length;

	int i = 1;
	byte val = *source++;
	byte mask = 1;
	word nodeval;
	huffptr = headptr;
	while(1)
	{
		if(!(val & mask))
			nodeval = huffptr->bit0;
		else
			nodeval = huffptr->bit1;
		if(mask==0x80)
		{
			i++;
			val = *source++;
			mask = 1;
		}
		else mask <<= 1;

		if(nodeval<256)
		{
			*dest++ = (byte) nodeval;
			written++;
			huffptr = headptr;
			if(dest>=end) break;
		}
		else
		{
			huffptr = hufftable + (nodeval - 256);
		}
	}
}


/*
======================
=
= CAL_CarmackExpand
=
= Length is the length of the EXPANDED data
=
======================
*/

#define NEARTAG	0xa7
#define FARTAG	0xa8

void CAL_CarmackExpand (byte *source, word *dest, unsigned length)
{
	word	ch,chhigh,count,offset;
	byte	*inptr;
	word	*copyptr, *outptr;

	length/=2;

	inptr = source;
	outptr = dest;

	while (length)
	{
		ch = *inptr++;
		chhigh = ch>>8;
		if (chhigh == NEARTAG)
		{
			count = ch&0xff;
			if (!count)
			{				// have to insert a word containing the tag byte
				ch |= *(unsigned char*)(inptr++);
				*outptr++ = ch;
				length--;
			}
			else
			{
				offset = *(unsigned char*)(inptr++);
				copyptr = outptr - offset;
				length -= count;
				while (count--)
					*outptr++ = *copyptr++;
			}
		}
		else if (chhigh == FARTAG)
		{
			count = ch&0xff;
			if (!count)
			{				// have to insert a word containing the tag byte
				ch |= *(unsigned char*)(inptr++);
				*outptr++ = ch;
				length --;
			}
			else
			{
				offset = *inptr++;
				copyptr = dest + offset;
				length -= count;
				while (count--)
					*outptr++ = *copyptr++;
			}
		}
		else
		{
			*outptr++ = ch;
			length --;
		}
	}
}



/*
======================
=
= CA_RLEWcompress
=
======================
*/

long CA_RLEWCompress (unsigned *source, long length, unsigned *dest,
  unsigned rlewtag)
{
  long complength;
  unsigned value,count,i;
  unsigned *start, *end;

  start = dest;

  end = source + (length+1)/2;

//
// compress it
//
  do
  {
	count = 1;
	value = *source++;
	while (*source == value && source<end)
	{
	  count++;
	  source++;
	}
	if (count>3 || value == rlewtag)
	{
    //
    // send a tag / count / value string
    //
      *dest++ = rlewtag;
      *dest++ = count;
      *dest++ = value;
    }
    else
    {
    //
    // send word without compressing
    //
      for (i=1;i<=count;i++)
	*dest++ = value;
	}

  } while (source<end);

  complength = 2*(dest-start);
  return complength;
}


/*
======================
=
= CA_RLEWexpand
= length is EXPANDED length
=
======================
*/

void CA_RLEWexpand (word *source, word *dest, int32_t length, word rlewtag) {
	word value,count,i;
	word *end=dest+length/2;

	//
	// expand it
	//
	do
	{
		value = *source++;
		if (value != rlewtag)
			//
				// uncompressed
					//
						*dest++=value;
		else
		{
			//
			// compressed string
			//
			count = *source++;
			value = *source++;
			for (i=1;i<=count;i++)
				*dest++ = value;
		}
	} while (dest<end);
}



/*
=============================================================================

					 CACHE MANAGER ROUTINES

=============================================================================
*/


/*
======================
=
= CAL_SetupGrFile
=
======================
*/

void CAL_SetupGrFile (void)
{
	char fname[13];
	memptr *compseg;

//
// load ???dict.ext (huffman dictionary for graphics files)
//

	strcpy(fname,gdictname);
	strcat(fname,extension);

	PHYSFS_File *handle = PHYSFS_openRead(fname);
	if (handle == NULL)
		CA_CannotOpen(fname);

	PHYSFS_readBytes(handle, &grhuffman, sizeof(grhuffman));
	PHYSFS_close(handle);

//
// load the data offsets from ???head.ext
//

	MM_GetPtr (&grstarts,(NUMCHUNKS+1)*FILEPOSSIZE);

	strcpy(fname,gheadname);
	strcat(fname,extension);

	handle = PHYSFS_openRead(fname);
	if (handle == NULL)
		CA_CannotOpen(fname);

	CA_FarRead(handle, (byte*)grstarts, (NUMCHUNKS+1)*FILEPOSSIZE);

	PHYSFS_close(handle);

//
// Open the graphics file, leaving it open until the game is finished
//
	strcpy(fname,gfilename);
	strcat(fname,extension);

	grhandle = PHYSFS_openRead(fname);
	if (grhandle == NULL)
		CA_CannotOpen(fname);


//
// load the pic and sprite headers into the arrays in the data segment
//
	MM_GetPtr((memptr**)&pictable,NUMPICS*sizeof(pictabletype));
	CAL_GetGrChunkLength(STRUCTPIC);		// position file pointer
	MM_GetPtr(&compseg,chunkcomplen);
	CA_FarRead (grhandle,compseg,chunkcomplen);
	CAL_HuffExpand (compseg, (byte*)pictable,NUMPICS*sizeof(pictabletype),grhuffman,false);
	MM_FreePtr(&compseg);
}

//==========================================================================


/*
======================
=
= CAL_SetupMapFile
=
======================
*/

void CAL_SetupMapFile (void)
{
	int	i;
	long length,pos;
	char fname[13];

//
// load maphead.ext (offsets and tileinfo for map file)
//
	strcpy_s(fname, sizeof(mheadname), mheadname);
	strcat_s(fname, sizeof(fname), extension);

	PHYSFS_File *handle = PHYSFS_openRead(fname);
	if (handle == NULL)
		CA_CannotOpen(fname);

	length = PHYSFS_fileLength(handle);
	MM_GetPtr ((memptr**)&tinf,length);
	CA_FarRead(handle, tinf, length);
	PHYSFS_close(handle);

//
// open the data file
//
	strcpy_s(fname, sizeof(fname), "GAMEMAPS.");
	strcat_s(fname, sizeof(fname), extension);

	maphandle = PHYSFS_openRead(fname);
	if (maphandle == NULL)
		CA_CannotOpen(fname);

//
// load all map header
//
	for (i=0;i<NUMMAPS;i++)
	{
		pos = ((mapfiletype*)tinf)->headeroffsets[i];
		if (pos<0)						// $FFFFFFFF start is a sparse map
			continue;

		MM_GetPtr((memptr**)&mapheaderseg[i],sizeof(maptype));
		PHYSFS_seek(maphandle, pos);
		CA_FarRead (maphandle,(memptr)mapheaderseg[i],sizeof(maptype));
	}

//
// allocate space for 3 64*64 planes
//
	for (i=0;i<MAPPLANES;i++) {
		MM_GetPtr ((memptr**)&mapsegs[i],64*64*2);
	}

	//PHYSFS_close(maphandle);
}


//==========================================================================


/*
======================
=
= CAL_SetupAudioFile
=
======================
*/

void CAL_SetupAudioFile (void)
{
	long length;
	char fname[13];

//
// load maphead.ext (offsets and tileinfo for map file)
//
#ifndef AUDIOHEADERLINKED
	strcpy(fname,aheadname);
	strcat(fname,extension);

	PHYSFS_File *handle = PHYSFS_openRead(fname);
	if (handle == NULL)
		CA_CannotOpen(fname);

	length = PHYSFS_fileLength(handle);
	MM_GetPtr (&audiostarts,length);
	CA_FarRead(handle, (byte *)audiostarts, length);
	PHYSFS_close(handle);
#else
	audiohuffman = (huffnode *)&audiodict;
	CAL_OptimizeNodes (audiohuffman);
	audiostarts = (long*)FP_SEG(&audiohead);
#endif

//
// open the data file
//
#ifndef AUDIOHEADERLINKED
	strcpy(fname,afilename);
	strcat(fname,extension);

	audiohandle = PHYSFS_openRead(fname);
	if (audiohandle == NULL)
		CA_CannotOpen(fname);
#else
	if (audioHandle = PHYSFS_openRead(fname) == NULL)
		Quit ("Can't open AUDIO."EXTENSION"!");
#endif
}

//==========================================================================


/*
======================
=
= CA_Startup
=
= Open all files and load in headers
=
======================
*/

void CA_Startup (void)
{
#ifdef PROFILE
	unlink ("PROFILE.TXT");
	profilehandle = open("PROFILE.TXT", O_CREAT | O_WRONLY | O_TEXT);
#endif

	CAL_SetupMapFile ();
	CAL_SetupGrFile ();
	CAL_SetupAudioFile ();

	mapon = -1;
	ca_levelbit = 1;
	ca_levelnum = 0;

}

//==========================================================================


/*
======================
=
= CA_Shutdown
=
= Closes all files
=
======================
*/

void CA_Shutdown (void)
{
#ifdef PROFILE
	close (profilehandle);
#endif

	PHYSFS_close (maphandle);
	PHYSFS_close(grhandle);
	PHYSFS_close (audiohandle);
}

//===========================================================================

/*
======================
=
= CA_CacheAudioChunk
=
======================
*/

void CA_CacheAudioChunk (int chunk)
{
	long	pos,compressed;
#ifdef AUDIOHEADERLINKED
	long	expanded;
	memptr	bigbufferseg;
	byte	*source;
#endif

	if (audiosegs[chunk])
	{
		MM_SetPurge ((memptr*)audiosegs[chunk],0);
		return;							// allready in memory
	}

//
// load the chunk into a buffer, either the miscbuffer if it fits, or allocate
// a larger buffer
//
	pos = audiostarts[chunk];
	compressed = (uintptr_t)audiostarts[chunk+1]-pos;

	PHYSFS_seek(audiohandle, pos);

#ifndef AUDIOHEADERLINKED

	MM_GetPtr ((memptr**)&audiosegs[chunk],compressed);
	if (mmerror)
		return;

	CA_FarRead(audiohandle,audiosegs[chunk],compressed);

#else

	if (compressed<=BUFFERSIZE)
	{
		CA_FarRead(audiohandle,bufferseg,compressed);
		source = bufferseg;
	}
	else
	{
		MM_GetPtr(&bigbufferseg,compressed);
		if (mmerror)
			return;
		MM_SetLock (&bigbufferseg,true);
		CA_FarRead(audiohandle,bigbufferseg,compressed);
		source = bigbufferseg;
	}

	expanded = *(long *)source;
	source += 4;			// skip over length
	MM_GetPtr (&(memptr)audiosegs[chunk],expanded);
	if (mmerror)
		goto done;
	CAL_HuffExpand (source,audiosegs[chunk],expanded,audiohuffman,false);

done:
	if (compressed>BUFFERSIZE)
		MM_FreePtr(&bigbufferseg);
#endif
}

//===========================================================================

/*
======================
=
= CA_LoadAllSounds
=
= Purges all sounds, then loads all new ones (mode switch)
=
======================
*/

void CA_LoadAllSounds (void)
{
	unsigned	start,i;

	switch (oldsoundmode)
	{
	case sdm_Off:
		goto cachein;
	case sdm_PC:
		start = STARTPCSOUNDS;
		break;
	case sdm_AdLib:
		start = STARTADLIBSOUNDS;
		break;
	}

	for (i=0;i<NUMSOUNDS;i++,start++)
		if (audiosegs[start])
			MM_SetPurge ((memptr*)audiosegs[start],3);		// make purgable

cachein:

	switch (SoundMode)
	{
	case sdm_Off:
		return;
	case sdm_PC:
		start = STARTPCSOUNDS;
		break;
	case sdm_AdLib:
		start = STARTADLIBSOUNDS;
		break;
	}

	for (i=0;i<NUMSOUNDS;i++,start++)
		CA_CacheAudioChunk (start);

	oldsoundmode = SoundMode;
}

//===========================================================================


/*
======================
=
= CAL_ExpandGrChunk
=
= Does whatever is needed with a pointer to a compressed chunk
=
======================
*/

void CAL_ExpandGrChunk (int chunk, byte *source)
{
	long	expanded;


	if (chunk >= STARTTILE8 && chunk < STARTEXTERNS)
	{
	//
	// expanded sizes of tile8/16/32 are implicit
	//

#define BLOCK		64
#define MASKBLOCK	128

		if (chunk<STARTTILE8M)			// tile 8s are all in one chunk!
			expanded = BLOCK*NUMTILE8;
		else if (chunk<STARTTILE16)
			expanded = MASKBLOCK*NUMTILE8M;
		else if (chunk<STARTTILE16M)	// all other tiles are one/chunk
			expanded = BLOCK*4;
		else if (chunk<STARTTILE32)
			expanded = MASKBLOCK*4;
		else if (chunk<STARTTILE32M)
			expanded = BLOCK*16;
		else
			expanded = MASKBLOCK*16;
	}
	else
	{
	//
	// everything else has an explicit size longword
	//
		expanded = *(long *)source;
		source += 4;			// skip over length
	}

//
// allocate final space, decompress it, and free bigbuffer
// Sprites need to have shifts made and various other junk
//
	MM_GetPtr ((memptr**)&grsegs[chunk],expanded);

	CAL_HuffExpand (source,grsegs[chunk],expanded,grhuffman,false);
}


void CAL_DeplaneGrChunk (int chunk)
{
	int     i;
	int16_t width,height;

	if (chunk == STARTTILE8)
	{
		width = height = 8;

		for (i = 0; i < NUMTILE8; i++)
			VL_DePlaneVGA (grsegs[chunk] + (i * (width * height)),width,height);
	}
	else
	{
		width = pictable[chunk - STARTPICS].width;
		height = pictable[chunk - STARTPICS].height;

		VL_DePlaneVGA (grsegs[chunk],width,height);
	}
}

/*
======================
=
= CA_CacheGrChunk
=
= Makes sure a given chunk is in memory, loadiing it if needed
=
======================
*/

void CA_CacheGrChunk (int chunk)
{
	long	pos,compressed;
	memptr	*bigbufferseg;
	byte	*source;
	int		next;

	grneeded[chunk] |= ca_levelbit;		// make sure it doesn't get removed
	if (grsegs[chunk])
	{
		// MM_SetPurge (&grsegs[chunk],0);
		return;							// allready in memory
	}

//
// load the chunk into a buffer, either the miscbuffer if it fits, or allocate
// a larger buffer
//
	pos = GRFILEPOS(chunk);
	if (pos<0)							// $FFFFFFFF start is a sparse tile
	  return;

	next = chunk +1;
	while (GRFILEPOS(next) == -1)		// skip past any sparse tiles
		next++;

	compressed = GRFILEPOS(next)-pos;

	PHYSFS_seek(grhandle,pos);

	if (compressed<=BUFFERSIZE)
	{
		CA_FarRead(grhandle,bufferseg,compressed);
		source = bufferseg;
	}
	else
	{
		MM_GetPtr(&bigbufferseg,compressed);
		CA_FarRead(grhandle,bigbufferseg,compressed);
		source = bigbufferseg;
	}

	CAL_ExpandGrChunk (chunk,source);

	if (chunk >= STARTPICS && chunk < STARTEXTERNS)
		CAL_DeplaneGrChunk (chunk);

	if (compressed>BUFFERSIZE)
		MM_FreePtr(&bigbufferseg);
}



//==========================================================================

/*
======================
=
= CA_CacheScreen
=
= Decompresses a chunk from disk straight onto the screen
=
======================
*/

void CA_CacheScreen (int chunk)
{
	long	pos,compressed,expanded;
	memptr	*bigbufferseg;
	byte	*source;
	int		next;

//
// load the chunk into a buffer
//
	pos = GRFILEPOS(chunk);
	next = chunk +1;
	while (GRFILEPOS(next) == -1)		// skip past any sparse tiles
		next++;
	compressed = GRFILEPOS(next)-pos;

	PHYSFS_seek(grhandle,pos);

	MM_GetPtr(&bigbufferseg,compressed);
	MM_SetLock (&bigbufferseg,true);
	CA_FarRead(grhandle,bigbufferseg,compressed);
	source = bigbufferseg;

	expanded = *(long *)source;
	source += 4;			// skip over length

//
// allocate final space, decompress it, and free bigbuffer
// Sprites need to have shifts made and various other junk
//
	CAL_HuffExpand (source,(byte*)bufferofs,expanded,grhuffman,true);
	VW_MarkUpdateBlock (0,0,319,199);
	MM_FreePtr(&bigbufferseg);
}

//==========================================================================

/*
======================
=
= CA_CacheMap
=
= WOLF: This is specialized for a 64*64 map size
=
======================
*/

void CA_CacheMap (int mapnum)
{
	long		pos,compressed;
	int			plane;
	memptr		*dest,*buffer;
	unsigned	size;
	word		*source;
#ifdef CARMACIZED
	memptr	*buffer2seg;
	long	expanded;
#endif

	mapon = mapnum;

//
// load the planes into the allready allocated buffers
//
	size = 64*64*2;

	for (plane = 0; plane<MAPPLANES; plane++)
	{
		pos = mapheaderseg[mapnum]->planestart[plane];
		compressed = mapheaderseg[mapnum]->planelength[plane];

		dest = (memptr*)mapsegs[plane];

		PHYSFS_seek(maphandle, pos);
		MM_GetPtr(&buffer,compressed);
		source = (word*)buffer;

		CA_FarRead(maphandle,(byte *)source,compressed);
#ifdef CARMACIZED
		//
		// unhuffman, then unRLEW
		// The huffman'd chunk has a two byte expanded length first
		// The resulting RLEW chunk also does, even though it's not really
		// needed
		//
		expanded = *source;
		source++;
		MM_GetPtr (&buffer2seg,expanded);
		CAL_CarmackExpand ((byte*)source, (word*)buffer2seg,expanded);
		CA_RLEWexpand ((word*)buffer2seg+1,(word*)dest,size, ((mapfiletype*)tinf)->RLEWtag);
		MM_FreePtr (&buffer2seg);

#else
		//
		// unRLEW, skipping expanded length
		//
		CA_RLEWexpand (source+1, *dest,size,
		((mapfiletype*)tinf)->RLEWtag);
#endif

		MM_FreePtr(&buffer);
	}
}

//===========================================================================

/*
======================
=
= CA_UpLevel
=
= Goes up a bit level in the needed lists and clears it out.
= Everything is made purgable
=
======================
*/

void CA_UpLevel (void)
{
	int	i;

	if (ca_levelnum==7)
		Quit ("CA_UpLevel: Up past level 7!");

	// for (i=0;i<NUMCHUNKS;i++)
	// 	if (grsegs[i])
	// 		MM_SetPurge ((memptr*)grsegs[i],3);
	ca_levelbit<<=1;
	ca_levelnum++;
}

//===========================================================================

/*
======================
=
= CA_DownLevel
=
= Goes down a bit level in the needed lists and recaches
= everything from the lower level
=
======================
*/

void CA_DownLevel (void)
{
	// if (!ca_levelnum)
	// 	Quit ("CA_DownLevel: Down past level 0!");
	// ca_levelbit>>=1;
	// ca_levelnum--;
	CA_CacheMarks();
}

//===========================================================================

/*
======================
=
= CA_ClearMarks
=
= Clears out all the marks at the current level
=
======================
*/

void CA_ClearMarks (void)
{
	int i;

	for (i=0;i<NUMCHUNKS;i++)
		grneeded[i]&=~ca_levelbit;
}


//===========================================================================

/*
======================
=
= CA_ClearAllMarks
=
= Clears out all the marks on all the levels
=
======================
*/

void CA_ClearAllMarks (void)
{
	memset(grneeded,0,sizeof(grneeded));
	ca_levelbit = 1;
	ca_levelnum = 0;
}


//===========================================================================


/*
======================
=
= CA_FreeGraphics
=
======================
*/


void CA_SetGrPurge (void)
{
	int i;

//
// free graphics
//
	CA_ClearMarks ();

	for (i=0;i<NUMCHUNKS;i++)
		if (grsegs[i])
			MM_SetPurge ((memptr*)grsegs[i],3);
}



/*
======================
=
= CA_SetAllPurge
=
= Make everything possible purgable
=
======================
*/

void CA_SetAllPurge (void)
{
	int i;


//
// free sounds
//
	for (i=0;i<NUMSNDCHUNKS;i++)
		if (audiosegs[i])
			MM_SetPurge ((memptr*)audiosegs[i],3);

//
// free graphics
//
	CA_SetGrPurge ();
}


//===========================================================================

/*
======================
=
= CA_CacheMarks
=
======================
*/
#define MAXEMPTYREAD	1024

void CA_CacheMarks (void)
{
	for (int i = STRUCTPIC + 1; i < NUMCHUNKS; i++)
		if (!grsegs[i]) {
			const long pos = GRFILEPOS(i);
			if (pos<0)
				continue;

			int next = i + 1;
			while (GRFILEPOS(next) == -1)		// skip past any sparse tiles
				next++;

			const long compressed = GRFILEPOS(next) - pos;

			memptr	*buffer;
			MM_GetPtr(&buffer, compressed);

			PHYSFS_seek(grhandle, pos);
			CA_FarRead(grhandle, (byte*)buffer, compressed);
			byte *source = (byte *) buffer;

			CAL_ExpandGrChunk (i,source);

			if (i >= STARTPICS && i < STARTEXTERNS)
				CAL_DeplaneGrChunk (i);

			MM_FreePtr(&buffer);
		}
}

void CA_CannotOpen(char *string)
{
 char str[30];

 strcpy(str,"Can't open ");
 strcat(str,string);
 strcat(str,"!\n");
 Quit (str);
}

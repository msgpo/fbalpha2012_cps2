#include "cps.h"
#include <retro_inline.h>
// CPS (general)

INT32 Cps = 0;							// 1 = CPS1, 2 = CPS2, 3 = CPS Changer
INT32 Cps2DisableQSnd = 0;				// Disables the Z80 as well

INT32 nCPS68KClockspeed = 0;
INT32 nCpsCycles = 0;						// 68K Cycles per frame
INT32	nCpsZ80Cycles;

UINT8 *CpsGfx =NULL; UINT32 nCpsGfxLen =0; // All the graphics
UINT8 *CpsRom =NULL; UINT32 nCpsRomLen =0; // Program Rom (as in rom)
UINT8 *CpsCode=NULL; UINT32 nCpsCodeLen=0; // Program Rom (decrypted)
UINT8 *CpsZRom=NULL; UINT32 nCpsZRomLen=0; // Z80 Roms
INT8 *CpsQSam=NULL; UINT32 nCpsQSamLen=0;	// QSound Sample Roms
UINT8 *CpsAd  =NULL; UINT32 nCpsAdLen  =0; // ADPCM Data
UINT8 *CpsStar=NULL;
UINT32 nCpsGfxScroll[4]={0,0,0,0}; // Offset to Scroll tiles
UINT32 nCpsGfxMask=0;	  // Address mask

// Separate out the bits of a byte
static INLINE UINT32 Separate(UINT32 b)
{
	UINT32 a = b;									// 00000000 00000000 00000000 11111111
	a  =((a & 0x000000F0) << 12) | (a & 0x0000000F);	// 00000000 00001111 00000000 00001111
	a = ((a & 0x000C000C) <<  6) | (a & 0x00030003);	// 00000011 00000011 00000011 00000011
	a = ((a & 0x02020202) <<  3) | (a & 0x01010101);	// 00010001 00010001 00010001 00010001

	return a;
}

// Precalculated table of the Separate function
static UINT32 SepTable[256];

static INT32 SepTableCalc()
{
   INT32 i;
	static INT32 bDone = 0;
	if (bDone)
		return 0;										// Already done it

	for (i = 0; i < 256; i++)
		SepTable[i] = Separate(255 - i);

	bDone = 1;											// done it
	return 0;
}

// Allocate space and load up a rom
static INT32 LoadUp(UINT8** pRom, INT32* pnRomLen, INT32 nNum)
{
	UINT8 *Rom;
	struct BurnRomInfo ri;

	ri.nLen = 0;
	BurnDrvGetRomInfo(&ri, nNum);	// Find out how big the rom is
	if (ri.nLen <= 0)
		return 1;

	// Load the rom
	Rom = (UINT8*)BurnMalloc(ri.nLen);
	if (Rom == NULL)
		return 1;

	if (BurnLoadRom(Rom,nNum,1))
   {
		BurnFree(Rom);
		return 1;
	}

	// Success
	*pRom = Rom; *pnRomLen = ri.nLen;
	return 0;
}

static INT32 LoadUpSplit(UINT8** pRom,
      INT32* pnRomLen, INT32 nNum, INT32 nNumRomsGroup)
{
	UINT8 *Rom;
	struct BurnRomInfo ri;
	UINT32 nRomSize[8], nTotalRomSize = 0;
	INT32 i;
   INT32 Offset = 0;

	ri.nLen = 0;
	for (i = 0; i < nNumRomsGroup; i++)
   {
		BurnDrvGetRomInfo(&ri, nNum + i);
		nRomSize[i] = ri.nLen;
	}
	
	for (i = 0; i < nNumRomsGroup; i++)
		nTotalRomSize += nRomSize[i];
	if (!nTotalRomSize) return 1;

	Rom = (UINT8*)BurnMalloc(nTotalRomSize);
	if (Rom == NULL) return 1;
	
	for (i = 0; i < nNumRomsGroup; i++)
   {
		if (i > 0)
         Offset += nRomSize[i - 1];
		if (BurnLoadRom(Rom + Offset, nNum + i, 1))
      {
			BurnFree(Rom);
			return 1;
		}
	}

	*pRom = Rom;
	*pnRomLen = nTotalRomSize;
	
	return 0;
}

// ----------------------------CPS2--------------------------------
// Load 1 rom and interleve in the CPS2 style:
// rom  : aa bb -- -- (4 bytes)
// --ba --ba --ba --ba --ba --ba --ba --ba 8 pixels (four bytes)
//                                                  (skip four bytes)

// memory 000000-100000 are in even word fields of first 080000 section
// memory 100000-200000 are in  odd word fields of first 080000 section
// i = ABCD nnnn nnnn nnnn nnnn n000
// s = 00AB Cnnn nnnn nnnn nnnn nnD0

static INLINE void Cps2Load100000(UINT8* Tile, UINT8* Sect, INT32 nShift)
{
	UINT8 *pt, *pEnd, *ps;
	pt = Tile; pEnd = Tile + 0x100000; ps = Sect;

	do {
		UINT32 Pix;				// Eight pixels
		Pix  = SepTable[ps[0]];
		Pix |= SepTable[ps[1]] << 1;
		Pix <<= nShift;
		*((UINT32*)pt) |= Pix;

		pt += 8; ps += 4;
	}
	while (pt < pEnd);
}

static INT32 Cps2LoadOne(UINT8* Tile, INT32 nNum, INT32 nWord, INT32 nShift)
{
   INT32 b;
	UINT8 *Rom = NULL; INT32 nRomLen = 0;
	UINT8 *pt, *pr;

	LoadUp(&Rom, &nRomLen, nNum);
	if (Rom == NULL) {
		return 1;
	}

	if (nWord == 0)
   {
      INT32 i;
		UINT8*Rom2 = NULL; INT32 nRomLen2 = 0;
		UINT8*Rom3 = Rom;

		LoadUp(&Rom2, &nRomLen2, nNum + 1);
		if (Rom2 == NULL) {
			return 1;
		}

		nRomLen <<= 1;
		Rom = (UINT8*)BurnMalloc(nRomLen);
		if (Rom == NULL) {
			BurnFree(Rom2);
			BurnFree(Rom3);
			return 1;
		}

		for (i = 0; i < nRomLen2; i++)
      {
			Rom[(i << 1) + 0] = Rom3[i];
			Rom[(i << 1) + 1] = Rom2[i];
		}

		BurnFree(Rom2);
		BurnFree(Rom3);
	}

	// Go through each section
	pt = Tile; pr = Rom;
	for (b = 0; b < nRomLen >> 19; b++)
   {
		Cps2Load100000(pt, pr,     nShift); pt += 0x100000;
		Cps2Load100000(pt, pr + 2, nShift); pt += 0x100000;
		pr += 0x80000;
	}

	BurnFree(Rom);

	return 0;
}

static INT32 Cps2LoadSplit(UINT8* Tile, INT32 nNum, INT32 nShift, INT32 nNumRomsGroup)
{
   INT32 b;
	UINT8 *Rom = NULL; INT32 nRomLen = 0;
	UINT8 *pt, *pr;

	LoadUpSplit(&Rom, &nRomLen, nNum, nNumRomsGroup);
	if (Rom == NULL)
		return 1;
	
	// Go through each section
	pt = Tile; pr = Rom;
	for (b = 0; b < nRomLen >> 19; b++)
   {
		Cps2Load100000(pt, pr,     nShift); pt += 0x100000;
		Cps2Load100000(pt, pr + 2, nShift); pt += 0x100000;
		pr += 0x80000;
	}

	BurnFree(Rom);

	return 0;
}

INT32 Cps2LoadTiles(UINT8* Tile, INT32 nStart)
{
	// left  side of 16x16 tiles
	Cps2LoadOne(Tile,     nStart,     1, 0);
	Cps2LoadOne(Tile,     nStart + 1, 1, 2);
	// right side of 16x16 tiles
	Cps2LoadOne(Tile + 4, nStart + 2, 1, 0);
	Cps2LoadOne(Tile + 4, nStart + 3, 1, 2);

	return 0;
}

INT32 Cps2LoadTilesSplit4(UINT8* Tile, INT32 nStart)
{
	// left  side of 16x16 tiles
	Cps2LoadSplit(Tile,     nStart +  0, 0, 4);
	Cps2LoadSplit(Tile,     nStart +  4, 2, 4);
	// right side of 16x16 tiles
	Cps2LoadSplit(Tile + 4, nStart +  8, 0, 4);
	Cps2LoadSplit(Tile + 4, nStart + 12, 2, 4);

	return 0;
}

INT32 Cps2LoadTilesSplit8(UINT8* Tile, INT32 nStart)
{
	// left  side of 16x16 tiles
	Cps2LoadSplit(Tile,     nStart +  0, 0, 8);
	Cps2LoadSplit(Tile,     nStart +  8, 2, 8);
	// right side of 16x16 tiles
	Cps2LoadSplit(Tile + 4, nStart + 16, 0, 8);
	Cps2LoadSplit(Tile + 4, nStart + 24, 2, 8);

	return 0;
}

INT32 Cps2LoadTilesSIM(UINT8* Tile, INT32 nStart)
{
	Cps2LoadOne(Tile,     nStart,     0, 0);
	Cps2LoadOne(Tile,     nStart + 2, 0, 2);
	Cps2LoadOne(Tile + 4, nStart + 4, 0, 0);
	Cps2LoadOne(Tile + 4, nStart + 6, 0, 2);

	return 0;
}

INT32 Cps2LoadTilesGigaman2(UINT8 *Tile, UINT8 *pSrc)
{
   INT32 b;
	UINT8 *pt = Tile;
	UINT8 *pr = pSrc;
	for (b = 0; b < 0x200000 >> 19; b++) {
		Cps2Load100000(pt, pr,     0); pt += 0x100000;
		Cps2Load100000(pt, pr + 2, 0); pt += 0x100000;
		pr += 0x80000;
	}
	
	pt = Tile;
	pr = pSrc + 0x200000;
	for (b = 0; b < 0x200000 >> 19; b++)
   {
		Cps2Load100000(pt, pr,     2); pt += 0x100000;
		Cps2Load100000(pt, pr + 2, 2); pt += 0x100000;
		pr += 0x80000;
	}
	
	pt = Tile + 4;
	pr = pSrc + 0x400000;
	for (b = 0; b < 0x200000 >> 19; b++) {
		Cps2Load100000(pt, pr,     0); pt += 0x100000;
		Cps2Load100000(pt, pr + 2, 0); pt += 0x100000;
		pr += 0x80000;
	}
	
	pt = Tile + 4;
	pr = pSrc + 0x600000;
	for (b = 0; b < 0x200000 >> 19; b++) {
		Cps2Load100000(pt, pr,     2); pt += 0x100000;
		Cps2Load100000(pt, pr + 2, 2); pt += 0x100000;
		pr += 0x80000;
	}

	return 0;
}

// ----------------------------------------------------------------

// The file extension indicates the data contained in a file.
// it consists of 2 numbers optionally followed by a single letter.
// The letter indicates the version. The meaning for the nubmers
// is as follows:
// 01 - 02 : Z80 program
// 03 - 10 : 68K program (filenames ending with x contain the XOR table)
// 11 - 12 : QSound sample data
// 13 - nn : Graphics data

static UINT32 nGfxMaxSize;

static INT32 CpsGetROMs(BOOL bLoad)
{
   INT32 i = 0;
	struct BurnRomInfo ri;

	UINT8* CpsCodeLoad = CpsCode;
	UINT8* CpsRomLoad = CpsRom;
	UINT8* CpsGfxLoad = CpsGfx;
	UINT8* CpsZRomLoad = CpsZRom;
	UINT8* CpsQSamLoad = (UINT8*)CpsQSam;

	INT32 nGfxNum = 0;

	if (bLoad) {
		if (!CpsCodeLoad || !CpsRomLoad || !CpsGfxLoad || !CpsZRomLoad || !CpsQSamLoad) {
			return 1;
		}
	} else {
		nCpsCodeLen = nCpsRomLen = nCpsGfxLen = nCpsZRomLen = nCpsQSamLen = 0;

		nGfxMaxSize = 0;
		if (BurnDrvGetHardwareCode() & HARDWARE_CAPCOM_CPS2_SIMM) {
			nGfxMaxSize = ~0U;
		}
	}

	do {
		ri.nLen = 0;
		ri.nType = 0;
		BurnDrvGetRomInfo(&ri, i);
		
		if ((ri.nType & 0x0f) == CPS2_PRG_68K) {
			if (bLoad) {
				BurnLoadRom(CpsRomLoad, i, 1);
				CpsRomLoad += ri.nLen;
			} else {
				nCpsRomLen += ri.nLen;
			}
			i++;
		}
		
		if ((ri.nType & 0x0f) == CPS2_PRG_68K_SIMM) {
			if (bLoad) {
				BurnLoadRom(CpsRomLoad + 0x000001, i + 0, 2);
				BurnLoadRom(CpsRomLoad + 0x000000, i + 1, 2);
				CpsRomLoad += ri.nLen * 2;
				i += 2;
			} else {
				nCpsRomLen += ri.nLen;
				i++;
			}
		}
		
		if ((ri.nType & 0x0f) == CPS2_PRG_68K_XOR_TABLE) {
			if (bLoad) {
				BurnLoadRom(CpsCodeLoad, i, 1);
				CpsCodeLoad += ri.nLen;
			} else {
				nCpsCodeLen += ri.nLen;
			}
			i++;
		}
		
		if ((ri.nType & 0x0f) == CPS2_GFX) {
			if (bLoad) {
				Cps2LoadTiles(CpsGfxLoad, i);
				CpsGfxLoad += (nGfxMaxSize == ~0U ? ri.nLen : nGfxMaxSize) * 4;
				i += 4;
			} else {
				if (ri.nLen > nGfxMaxSize) {
					nGfxMaxSize = ri.nLen;
				}
				if (ri.nLen < nGfxMaxSize) {
					nGfxMaxSize = ~0U;
				}
				nCpsGfxLen += ri.nLen;
				nGfxNum++;
				i++;
			}
		}
		
		if ((ri.nType & 0x0f) == CPS2_GFX_SIMM) {
			if (bLoad) {
				Cps2LoadTilesSIM(CpsGfxLoad, i);
				CpsGfxLoad += ri.nLen * 8;
				i += 8;
			} else {
				nCpsGfxLen += ri.nLen;
				i++;
			}
		}
		
		if ((ri.nType & 0x0f) == CPS2_GFX_SPLIT4) {
			if (bLoad) {
				Cps2LoadTilesSplit4(CpsGfxLoad, i);
				CpsGfxLoad += (nGfxMaxSize == ~0U ? ri.nLen : nGfxMaxSize) * 16;
				i += 16;
			} else {
				if (ri.nLen > nGfxMaxSize) {
					nGfxMaxSize = ri.nLen;
				}
				if (ri.nLen < nGfxMaxSize) {
					nGfxMaxSize = ~0U;
				}
				nCpsGfxLen += ri.nLen;
				nGfxNum++;
				i++;
			}
		}
		
		if ((ri.nType & 0x0f) == CPS2_GFX_SPLIT8) {
			if (bLoad) {
				Cps2LoadTilesSplit8(CpsGfxLoad, i);
				CpsGfxLoad += (nGfxMaxSize == ~0U ? ri.nLen : nGfxMaxSize) * 32;
				i += 32;
			} else {
				if (ri.nLen > nGfxMaxSize) {
					nGfxMaxSize = ri.nLen;
				}
				if (ri.nLen < nGfxMaxSize) {
					nGfxMaxSize = ~0U;
				}
				nCpsGfxLen += ri.nLen;
				nGfxNum++;
				i++;
			}
		}
				
		if ((ri.nType & 0x0f) == CPS2_PRG_Z80) {
			if (bLoad) {
				BurnLoadRom(CpsZRomLoad, i, 1);
				CpsZRomLoad += ri.nLen;
			} else {
				nCpsZRomLen += ri.nLen;
			}
			i++;
		}
		
		if ((ri.nType & 0x0f) == CPS2_QSND) {
			if (bLoad) {
				BurnLoadRom(CpsQSamLoad, i, 1);
				BurnByteswap(CpsQSamLoad, ri.nLen);
				CpsQSamLoad += ri.nLen;
			} else {
				nCpsQSamLen += ri.nLen;
			}
			i++;
		}
		
		if ((ri.nType & 0x0f) == CPS2_QSND_SIMM) {
			if (bLoad) {
				BurnLoadRom(CpsQSamLoad, i, 1);
				BurnByteswap(CpsQSamLoad, ri.nLen);
				CpsQSamLoad += ri.nLen;
			} else {
				nCpsQSamLen += ri.nLen;
			}
			i++;
		}
		
		if ((ri.nType & 0x0f) == CPS2_QSND_SIMM_BYTESWAP) {
			if (bLoad) {
				BurnLoadRom(CpsQSamLoad + 1, i + 0, 2);
				BurnLoadRom(CpsQSamLoad + 0, i + 1, 2);
				i += 2;
			} else {
				nCpsQSamLen += ri.nLen;
				i++;
			}
		}
	} while (ri.nLen);

	if (bLoad) {
#if 0
		for (UINT32 i = 0; i < nCpsCodeLen / 4; i++) {
			((UINT32*)CpsCode)[i] ^= ((UINT32*)CpsRom)[i];
		}
#endif
		cps2_decrypt_game_data();
		
//		if (!nCpsCodeLen) return 1;
	} else {

		if (nGfxMaxSize != ~0U) {
			nCpsGfxLen = nGfxNum * nGfxMaxSize;
		}

#if 1 && defined FBA_DEBUG
		if (!nCpsCodeLen) {
			bprintf(PRINT_IMPORTANT, _T("  - 68K ROM size:\t0x%08X (Decrypted with key)\n"), nCpsRomLen);
		} else {
			bprintf(PRINT_IMPORTANT, _T("  - 68K ROM size:\t0x%08X (XOR table size: 0x%08X)\n"), nCpsRomLen, nCpsCodeLen);
		}
		bprintf(PRINT_IMPORTANT, _T("  - Z80 ROM size:\t0x%08X\n"), nCpsZRomLen);
		bprintf(PRINT_IMPORTANT, _T("  - Graphics data:\t0x%08X\n"), nCpsGfxLen);
		bprintf(PRINT_IMPORTANT, _T("  - QSound data:\t0x%08X\n"), nCpsQSamLen);
#endif

		if (/*!nCpsCodeLen ||*/ !nCpsRomLen || !nCpsGfxLen || !nCpsZRomLen || ! nCpsQSamLen) {
			return 1;
		}
	}

	return 0;
}

// ----------------------------------------------------------------

INT32 CpsInit()
{
	INT32 nMemLen, i;
	
   if (Cps == 2)
      BurnSetRefreshRate(59.629403);

	if (!nCPS68KClockspeed) {
			nCPS68KClockspeed = 11800000;
	}
	nCPS68KClockspeed = nCPS68KClockspeed * 100 / nBurnFPS;

	nMemLen = nCpsGfxLen + nCpsRomLen + nCpsCodeLen + nCpsZRomLen + nCpsQSamLen + nCpsAdLen;

	// Allocate Gfx, Rom and Z80 Roms
	CpsGfx = (UINT8*)BurnMalloc(nMemLen);
	if (CpsGfx == NULL) {
		return 1;
	}
	memset(CpsGfx, 0, nMemLen);

	CpsRom  = CpsGfx + nCpsGfxLen;
	CpsCode = CpsRom + nCpsRomLen;
   CpsZRom = CpsCode + nCpsCodeLen;
	CpsQSam =(INT8*)(CpsZRom + nCpsZRomLen);
	CpsAd   =(UINT8*)(CpsQSam + nCpsQSamLen);

	// Create Gfx addr mask
	for (i = 0; i < 31; i++) {
		if ((1 << i) >= (INT32)nCpsGfxLen) {
			break;
		}
	}
	nCpsGfxMask = (1 << i) - 1;

	// Offset to Scroll tiles
   nCpsGfxScroll[1] = nCpsGfxScroll[2] = nCpsGfxScroll[3] = 0x800000;

#if 0
	if (nCpsZRomLen>=5) {
		// 77->cfff and rst 00 in case driver doesn't load
		CpsZRom[0] = 0x3E; CpsZRom[1] = 0x77;
		CpsZRom[2] = 0x32; CpsZRom[3] = 0xFF; CpsZRom[4] = 0xCF;
		CpsZRom[5] = 0xc7;
	}
#endif

	SepTableCalc();									  // Precalc the separate table

	CpsReset = 0; Cpi01A = Cpi01C = Cpi01E = 0;		  // blank other inputs

	// Use this as default - all CPS-2 games use it
	SetCpsBId(CPS_B_21_DEF, 0);

	return 0;
}

INT32 Cps2Init()
{
	Cps = 2;

	if (CpsGetROMs(FALSE))
		return 1;

	CpsInit();

	if (CpsGetROMs(TRUE))
		return 1;

	return CpsRunInit();
}

INT32 CpsExit()
{
   CpsRunExit();

	CpsLayEn[1] = CpsLayEn[2] = CpsLayEn[3] = CpsLayEn[4] = CpsLayEn[5] = 0;
	nCpsLcReg = 0;
	nCpsGfxScroll[1] = nCpsGfxScroll[2] = nCpsGfxScroll[3] = 0;
	nCpsGfxMask = 0;
	
	Scroll1TileMask = 0;
	Scroll2TileMask = 0;
	Scroll3TileMask = 0;

	nCpsCodeLen = nCpsRomLen = nCpsGfxLen = nCpsZRomLen = nCpsQSamLen = nCpsAdLen = 0;
	CpsRom = CpsZRom = CpsAd = CpsStar = NULL;
	CpsQSam = NULL;

	// All Memory is allocated to this (this is the only one we can free)
	BurnFree(CpsGfx);
	
	BurnFree(CpsCode);
	
	bCpsUpdatePalEveryFrame = 0;

	nCPS68KClockspeed = 0;
	Cps = 0;
	nCpsNumScanlines = 259;

	return 0;
}

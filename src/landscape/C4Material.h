/*
 * OpenClonk, http://www.openclonk.org
 *
 * Copyright (c) 1998-2000, Matthes Bender
 * Copyright (c) 2001-2009, RedWolf Design GmbH, http://www.clonk.de/
 * Copyright (c) 2009-2016, The OpenClonk Team and contributors
 *
 * Distributed under the terms of the ISC license; see accompanying file
 * "COPYING" for details.
 *
 * "Clonk" is a registered trademark of Matthes Bender, used with permission.
 * See accompanying file "TRADEMARK" for details.
 *
 * To redistribute this file separately, substitute the full license texts
 * for the above references.
 */

/* Material definitions used by the landscape */

#ifndef INC_C4Material
#define INC_C4Material

#include "config/C4Constants.h"
#include "graphics/C4Facet.h"
#include "graphics/CSurface8.h"
#include "object/C4Id.h"
#include "object/C4Shape.h"

#define C4MatOv_Default     0
#define C4MatOv_Exact       1
#define C4MatOv_None        2
#define C4MatOv_HugeZoom    4

enum MaterialInteractionEvent
{
	meePXSPos=0,  // PXS check before movement
	meePXSMove=1, // PXS movement
	meeMassMove=2 // MassMover-movement
};

typedef bool (*C4MaterialReactionFunc)(struct C4MaterialReaction *pReaction, int32_t &iX, int32_t &iY, int32_t iLSPosX, int32_t iLSPosY, C4Real &fXDir, C4Real &fYDir, int32_t &iPxsMat, int32_t iLsMat, MaterialInteractionEvent evEvent, bool *pfPosChanged);

struct C4MaterialReaction
{
	static inline bool NoReaction(struct C4MaterialReaction *pReaction, int32_t &iX, int32_t &iY, int32_t iLSPosX, int32_t iLSPosY, C4Real &fXDir, C4Real &fYDir, int32_t &iPxsMat, int32_t iLsMat, MaterialInteractionEvent evEvent, bool *pfPosChanged) { return false; }

	C4MaterialReactionFunc pFunc; // Guarantueed to be non-nullptr
	bool fUserDefined{true};        // false for internal reactions generated by material parameters
	StdCopyStrBuf TargetSpec; // target material specification
	StdCopyStrBuf ScriptFunc; // for reaction func 'script': Script func to be called for reaction evaluation
	C4AulFunc *pScriptFunc{nullptr};   // resolved script function
	uint32_t iExecMask;       // execution mask: Bit mask with indices into MaterialInteractionEvent
	bool fReverse{false};            // if set, spec will be handled as if specified in target mat def
	bool fInverseSpec{false};        // if set, all mats except the given are used
	bool fInsertionCheck{true};     // if set, splash/slide checks are done prior to reaction execution
	int32_t iDepth{0};           // in mat conversion depth
	StdCopyStrBuf sConvertMat;// in mat conversion material (string)
	int32_t iConvertMat{-1};      // in mat conversion material; evaluated in CrossMapMaterials
	int32_t iCorrosionRate{100};   // chance of doing a corrosion

	C4MaterialReaction(C4MaterialReactionFunc pFunc) : pFunc(pFunc), fUserDefined(false), pScriptFunc(nullptr), iExecMask(~0u) {}
	C4MaterialReaction() : pFunc(&NoReaction),  iExecMask(~0u) { }

	void CompileFunc(StdCompiler *pComp);

	void ResolveScriptFuncs(const char *szMatName);

	bool operator ==(const C4MaterialReaction &rCmp) const { return false; } // never actually called; only comparing with empty vector of C4MaterialReactions
};

enum C4MaterialCoreShape
{
	C4M_Flat    = 0,
	C4M_TopFlat = 1,
	C4M_Smooth  = 2,
	C4M_Rough   = 3,
	C4M_Octagon = 4,
	C4M_Smoother= 5,
};

class C4MaterialCore
{
public:
	C4MaterialCore();
	~C4MaterialCore() { Clear(); }

	std::vector<C4MaterialReaction> CustomReactionList;

	char Name[C4M_MaxName+1];

	C4MaterialCoreShape MapChunkType;
	int32_t  Density;
	int32_t  Friction;
	int32_t  DigFree;
	int32_t  BlastFree;
	C4ID Dig2Object;
	int32_t  Dig2ObjectRatio;
	int32_t  Dig2ObjectCollect;
	C4ID Blast2Object;
	int32_t  Blast2ObjectRatio;
	int32_t  Blast2PXSRatio;
	int32_t  Instable;
	int32_t  MaxAirSpeed;
	int32_t  MaxSlide;
	int32_t  WindDrift;
	int32_t  Inflammable;
	int32_t  Incendiary;
	int32_t  Extinguisher;
	int32_t  Corrosive;
	int32_t  Corrode;
	int32_t  Soil;
	int32_t  Placement; // placement order for landscape shading
	int32_t  Light; // ambient light range for underground materials. currently only 1/0 for ambient light on/off
	StdCopyStrBuf sTextureOverlay; // overlayed texture for this material
	int32_t  OverlayType; // defines the way in which the overlay texture is applied
	StdCopyStrBuf sPXSGfx;      // newgfx: picture used for loose pxs
	C4TargetRect PXSGfxRt;          // newgfx: facet rect of pixture used for loose pixels
	int32_t  PXSGfxSize;
	StdCopyStrBuf sBlastShiftTo;
	StdCopyStrBuf sInMatConvert;
	StdCopyStrBuf sInMatConvertTo;
	int32_t  InMatConvertDepth;         // material converts only if it finds the same material above
	int32_t  BelowTempConvert;
	int32_t  BelowTempConvertDir;
	StdCopyStrBuf sBelowTempConvertTo;
	int32_t  AboveTempConvert;
	int32_t  AboveTempConvertDir;
	StdCopyStrBuf sAboveTempConvertTo;
	int32_t  TempConvStrength;
	int32_t  MinHeightCount; // minimum material thickness in order for it to be counted
	int32_t  SplashRate;
	bool KeepSinglePixels; // if true, single pixels are not destroyed (for vehicle)
	int32_t  AnimationSpeed; // frames per animation phase
	int32_t  LightAngle; // light angle at which we have maximum reflection
	int32_t  LightEmit[3]; // amount the material lights up itself
	int32_t  LightSpot[3]; // spot strength
	int32_t MinShapeOverlap; // if drawn with a texture with custom shapes, minimum overlap of map pixels over shape blocks in percent to force them being drawn

	void Clear();
	void Default();
	bool Load(C4Group &hGroup, const char *szEntryName);
	void CompileFunc(StdCompiler *pComp);
};

class C4Material: public C4MaterialCore
{
public:
	C4Material();
public:
	// Cross-mapped material values
	int32_t BlastShiftTo; // MatTex
	int32_t InMatConvertTo; // Mat
	int32_t BelowTempConvertTo; // MatTex
	int32_t AboveTempConvertTo; // MatTex
	int32_t DefaultMatTex;      // texture used for single pixel values

	C4Facet PXSFace;        // loose pixel facet

	void UpdateScriptPointers(); // set all material script pointers
};

class C4MaterialMap
{
public:
	C4MaterialMap();
	~C4MaterialMap();
public:
	int32_t Num;
	C4Material *Map;
	C4MaterialReaction **ppReactionMap;
	int32_t max_shape_width,max_shape_height; // maximum size of the largest polygon in any of the used shapes

	C4MaterialReaction DefReactConvert, DefReactPoof, DefReactCorrode, DefReactIncinerate, DefReactInsert;
public:
	// default reactions
	static bool mrfConvert(C4MaterialReaction *pReaction, int32_t &iX, int32_t &iY, int32_t iLSPosX, int32_t iLSPosY, C4Real &fXDir, C4Real &fYDir, int32_t &iPxsMat, int32_t iLsMat, MaterialInteractionEvent evEvent, bool *pfPosChanged);
	static bool mrfPoof   (C4MaterialReaction *pReaction, int32_t &iX, int32_t &iY, int32_t iLSPosX, int32_t iLSPosY, C4Real &fXDir, C4Real &fYDir, int32_t &iPxsMat, int32_t iLsMat, MaterialInteractionEvent evEvent, bool *pfPosChanged);
	static bool mrfCorrode(C4MaterialReaction *pReaction, int32_t &iX, int32_t &iY, int32_t iLSPosX, int32_t iLSPosY, C4Real &fXDir, C4Real &fYDir, int32_t &iPxsMat, int32_t iLsMat, MaterialInteractionEvent evEvent, bool *pfPosChanged);
	static bool mrfIncinerate(C4MaterialReaction *pReaction, int32_t &iX, int32_t &iY, int32_t iLSPosX, int32_t iLSPosY, C4Real &fXDir, C4Real &fYDir, int32_t &iPxsMat, int32_t iLsMat, MaterialInteractionEvent evEvent, bool *pfPosChanged);
	static bool mrfInsert (C4MaterialReaction *pReaction, int32_t &iX, int32_t &iY, int32_t iLSPosX, int32_t iLSPosY, C4Real &fXDir, C4Real &fYDir, int32_t &iPxsMat, int32_t iLsMat, MaterialInteractionEvent evEvent, bool *pfPosChanged);
	// user-defined actions
	static bool mrfScript(C4MaterialReaction *pReaction, int32_t &iX, int32_t &iY, int32_t iLSPosX, int32_t iLSPosY, C4Real &fXDir, C4Real &fYDir, int32_t &iPxsMat, int32_t iLsMat, MaterialInteractionEvent evEvent, bool *pfPosChanged);
public:
	void Default();
	void Clear();
	int32_t Load(C4Group &hGroup);
	bool HasMaterials(C4Group &hGroup) const;
	int32_t Get(const char *szMaterial);
	bool SaveEnumeration(C4Group &hGroup);
	bool LoadEnumeration(C4Group &hGroup);
	C4MaterialReaction *GetReactionUnsafe(int32_t iPXSMat, int32_t iLandscapeMat)
	{
		assert(ppReactionMap); assert(Inside<int32_t>(iPXSMat,-1,Num-1)); assert(Inside<int32_t>(iLandscapeMat,-1,Num-1));
		return ppReactionMap[(iLandscapeMat+1)*(Num+1) + iPXSMat+1];
	}
	C4MaterialReaction *GetReaction(int32_t iPXSMat, int32_t iLandscapeMat);
	void UpdateScriptPointers(); // set all material script pointers
	bool CrossMapMaterials(const char* szEarthMaterial);
protected:
	void SetMatReaction(int32_t iPXSMat, int32_t iLSMat, C4MaterialReaction *pReact);
	bool SortEnumeration(int32_t iMat, const char *szMatName);
};

extern C4MaterialMap MaterialMap;

extern int32_t MVehic,MTunnel,MWater,MEarth; // presearched materials
extern BYTE MCVehic; // precalculated material color
extern BYTE MCHalfVehic; // precalculated material color

inline bool MatValid(int32_t mat)
{
	return Inside<int32_t>(mat,0,::MaterialMap.Num-1);
}

inline bool MatVehicle(int32_t iMat)
{
	return iMat == MVehic;
}

inline bool IsMCVehicle(BYTE mat) {
	return mat == MCVehic;
}
inline bool IsMCHalfVehicle(BYTE mat) {
	return mat == MCHalfVehic;
}
inline bool IsSomeVehicle(BYTE mat) {
	return IsMCVehicle(mat) || IsMCHalfVehicle(mat);
}

inline BYTE MatTex2PixCol(int32_t tex)
{
	return BYTE(tex);
}

inline BYTE Mat2PixColDefault(int32_t mat)
{
	return ::MaterialMap.Map[mat].DefaultMatTex;
}

inline int32_t MatDensity(int32_t mat)
{
	if (!MatValid(mat)) return 0;
	return ::MaterialMap.Map[mat].Density;
}

inline int32_t MatPlacement(int32_t mat)
{
	if (!MatValid(mat)) return 0;
	return ::MaterialMap.Map[mat].Placement;
}

inline int32_t MatDigFree(int32_t mat)
{
	if (!MatValid(mat)) return 1;
	return ::MaterialMap.Map[mat].DigFree;
}

#endif

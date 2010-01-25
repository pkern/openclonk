/*
 * OpenClonk, http://www.openclonk.org
 *
 * Copyright (c) 2002, 2005-2006  Sven Eberhardt
 * Copyright (c) 2005-2009  Günther Brammer <gbrammer@gmx.de>
 * Copyright (c) 2007  Julian Raschke
 * Copyright (c) 2008  Matthes Bender
 * Copyright (c) 2001-2009, RedWolf Design GmbH, http://www.clonk.de
 * Copyright (c) 2009 Carl-Philip Hänsch <c-p.haensch@vr-web.de>
 *
 * Portions might be copyrighted by other authors who have contributed
 * to OpenClonk.
 *
 * Permission to use, copy, modify, and/or distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 * See isc_license.txt for full license and disclaimer.
 *
 * "Clonk" is a registered trademark of Matthes Bender.
 * See clonk_trademark_license.txt for full license.
 */

/* OpenGL implementation of NewGfx */

#include "C4Include.h"
#include <Standard.h>
#include <StdGL.h>
#include <StdSurface2.h>
#include <StdWindow.h>
#include "C4Rect.h"

#ifdef USE_GL

#include <stdio.h>
#include <math.h>
#include <limits.h>

static void glColorDw(DWORD dwClr)
	{
	glColor4ub(GLubyte(dwClr>>16), GLubyte(dwClr>>8), GLubyte(dwClr), GLubyte(dwClr>>24));
	}

// GLubyte (&r)[4] is a reference to an array of four bytes named r.
static void DwTo4UB(DWORD dwClr, GLubyte (&r)[4])
	{
	//unsigned char r[4];
	r[0] = GLubyte(dwClr>>16);
	r[1] = GLubyte(dwClr>>8);
	r[2] = GLubyte(dwClr);
	r[3] = GLubyte(dwClr>>24);
	}

CStdGL::CStdGL()
	{
	Default();
	// global ptr
	pGL = this;
	shaders[0] = 0;
	vbo = 0;
	}

CStdGL::~CStdGL()
	{
	Clear();
	pGL=NULL;
	}

void CStdGL::Clear()
	{
	#ifndef USE_SDL_MAINLOOP
	CStdDDraw::Clear();
	#endif
	NoPrimaryClipper();
	if (pTexMgr) pTexMgr->IntUnlock();
	InvalidateDeviceObjects();
	NoPrimaryClipper();
	// del main surfaces
	if (lpPrimary) delete lpPrimary;
	lpPrimary = lpBack = NULL;
	RenderTarget = NULL;
	if (lines_tex) { glDeleteTextures(1, &lines_tex); lines_tex = 0; }
	// clear context
	if (pCurrCtx) pCurrCtx->Deselect();
	MainCtx.Clear();
	pCurrCtx=NULL;
	#ifndef USE_SDL_MAINLOOP
	CStdDDraw::Clear();
	#endif
	}

bool CStdGL::PageFlip(RECT *pSrcRt, RECT *pDstRt, CStdWindow * pWindow)
	{
	// call from gfx thread only!
	if (!pApp || !pApp->AssertMainThread()) return false;
	// safety
	if (!pCurrCtx) return false;
	// end the scene and present it
	if (!pCurrCtx->PageFlip()) return false;
	// success!
  return true;
	}

void CStdGL::FillBG(DWORD dwClr)
	{
	if (!pCurrCtx) if (!MainCtx.Select()) return;
	glClearColor((float)GetBValue(dwClr)/255.0f, (float)GetGValue(dwClr)/255.0f, (float)GetRValue(dwClr)/255.0f, 0.0f);
	glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
	}

bool CStdGL::UpdateClipper()
	{
	// no render target? do nothing
	if (!RenderTarget || !Active) return true;
	// negative/zero?
	int iWdt=Min(iClipX2, RenderTarget->Wdt-1)-iClipX1+1;
	int iHgt=Min(iClipY2, RenderTarget->Hgt-1)-iClipY1+1;
	int iX=iClipX1; if (iX<0) { iWdt+=iX; iX=0; }
	int iY=iClipY1; if (iY<0) { iHgt+=iY; iY=0; }
	if (iWdt<=0 || iHgt<=0)
		{
		ClipAll=true;
		return true;
		}
	ClipAll=false;
	// set it
	glViewport(iX, RenderTarget->Hgt-iY-iHgt, iWdt, iHgt);
	glMatrixMode(GL_PROJECTION);
	glLoadIdentity();

	// Set clipping plane to -1000 and 1000 so that large meshes are not
	// clipped away.
	glOrtho((GLdouble) iX, (GLdouble) (iX+iWdt), (GLdouble) (iY+iHgt), (GLdouble) iY, -1000.0f, 1000.0f);
	//gluOrtho2D((GLdouble) iX, (GLdouble) (iX+iWdt), (GLdouble) (iY+iHgt), (GLdouble) iY);
	//gluOrtho2D((GLdouble) 0, (GLdouble) xRes, (GLdouble) yRes, (GLdouble) yRes-iHgt);
	return true;
	}

bool CStdGL::PrepareMaterial(StdMeshMaterial& mat)
{
	// select context, if not already done
	if (!pCurrCtx) if (!MainCtx.Select()) return false;

	for(unsigned int i = 0; i < mat.Techniques.size(); ++i)
	{
		StdMeshMaterialTechnique& technique = mat.Techniques[i];
		technique.Available = true;
		for(unsigned int j = 0; j < technique.Passes.size(); ++j)
		{
			StdMeshMaterialPass& pass = technique.Passes[j];
			
			GLint max_texture_units;
			glGetIntegerv(GL_MAX_TEXTURE_UNITS, &max_texture_units);
			assert(max_texture_units >= 1);
			if(pass.TextureUnits.size() > static_cast<unsigned int>(max_texture_units-1)) // One texture is reserved for FoW/ClrModMap
				technique.Available = false;
			
			for(unsigned int k = 0; k < pass.TextureUnits.size(); ++k)
			{
				StdMeshMaterialTextureUnit& texunit = pass.TextureUnits[k];
				if(texunit.HasTexture())
				{
					glBindTexture(GL_TEXTURE_2D, texunit.GetTexture().texName);
					switch(texunit.TexAddressMode)
					{
					case StdMeshMaterialTextureUnit::AM_Wrap:
						glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
						glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
						break;
					case StdMeshMaterialTextureUnit::AM_Border:
						glTexParameterfv(GL_TEXTURE_2D, GL_TEXTURE_BORDER_COLOR, texunit.TexBorderColor);
						// fallthrough
					case StdMeshMaterialTextureUnit::AM_Clamp:
						glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP);
						glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP);
						break;
					case StdMeshMaterialTextureUnit::AM_Mirror:
						glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_MIRRORED_REPEAT);
						glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_MIRRORED_REPEAT);
						break;
					}

					if(texunit.Filtering[2] == StdMeshMaterialTextureUnit::F_Point ||
						 texunit.Filtering[2] == StdMeshMaterialTextureUnit::F_Linear)
					{
						// If mipmapping is enabled, then autogenerate mipmap data.
						// In OGRE this is deactivated for several OS/graphics card
						// combinations because of known bugs...

						// This does work for me, but requires re-upload of texture data...
						// so the proper way would be to set this prior to the initial
						// upload, which would be the same place where we could also use
						// gluBuild2DMipmaps. GL_GENERATE_MIPMAP is probably still more
						// efficient though.
					
						// Disabled for now, until we find a better place for this (CTexRef?)
	#if 0
						if(GLEW_VERSION_1_4)
	{						glTexParameteri(GL_TEXTURE_2D, GL_GENERATE_MIPMAP, GL_TRUE); const_cast<CTexRef*>(&texunit.GetTexture())->Lock(); const_cast<CTexRef*>(&texunit.GetTexture())->Unlock(); }
						else
							technique.Available = false;
	#else
						// Disable mipmap for now as a workaround.
						texunit.Filtering[2] = StdMeshMaterialTextureUnit::F_None;
	#endif
					}

					switch(texunit.Filtering[0]) // min
					{
					case StdMeshMaterialTextureUnit::F_None:
						technique.Available = false;
						break;
					case StdMeshMaterialTextureUnit::F_Point:
						switch(texunit.Filtering[2]) // mip
						{
						case StdMeshMaterialTextureUnit::F_None:
							glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
							break;
						case StdMeshMaterialTextureUnit::F_Point:
							glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST_MIPMAP_NEAREST);
							break;
						case StdMeshMaterialTextureUnit::F_Linear:
							glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST_MIPMAP_LINEAR);
							break;
						case StdMeshMaterialTextureUnit::F_Anisotropic:
							technique.Available = false; // invalid
							break;
						}
						break;
					case StdMeshMaterialTextureUnit::F_Linear:
						switch(texunit.Filtering[2]) // mip
						{
						case StdMeshMaterialTextureUnit::F_None:
							glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
							break;
						case StdMeshMaterialTextureUnit::F_Point:
							glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_NEAREST);
							break;
						case StdMeshMaterialTextureUnit::F_Linear:
							glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
							break;
						case StdMeshMaterialTextureUnit::F_Anisotropic:
							technique.Available = false; // invalid
							break;
						}
						break;
					case StdMeshMaterialTextureUnit::F_Anisotropic:
						// unsupported
						technique.Available = false;
						break;
					}

					switch(texunit.Filtering[1]) // max
					{
					case StdMeshMaterialTextureUnit::F_None:
						technique.Available = false; // invalid
						break;
					case StdMeshMaterialTextureUnit::F_Point:
						glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
						break;
					case StdMeshMaterialTextureUnit::F_Linear:
						glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
						break;
					case StdMeshMaterialTextureUnit::F_Anisotropic:
						// unsupported
						technique.Available = false;
						break;
					}
				} // HasTexture

				// Check blending: Can only have one manual source color
				unsigned int manu_count = 0;
				if(texunit.ColorOpEx == StdMeshMaterialTextureUnit::BOX_AddSmooth || texunit.ColorOpEx == StdMeshMaterialTextureUnit::BOX_BlendManual)
					++manu_count;
				if(texunit.ColorOpSources[0] == StdMeshMaterialTextureUnit::BOS_PlayerColor || texunit.ColorOpSources[0] == StdMeshMaterialTextureUnit::BOS_Manual)
					++manu_count;
				if(texunit.ColorOpSources[1] == StdMeshMaterialTextureUnit::BOS_PlayerColor || texunit.ColorOpSources[1] == StdMeshMaterialTextureUnit::BOS_Manual)
					++manu_count;

				if(manu_count > 1)
					technique.Available = false;

				manu_count = 0;					
				if(texunit.ColorOpEx == StdMeshMaterialTextureUnit::BOX_AddSmooth || texunit.AlphaOpEx == StdMeshMaterialTextureUnit::BOX_BlendManual)
					++manu_count;
				if(texunit.AlphaOpSources[0] == StdMeshMaterialTextureUnit::BOS_PlayerColor || texunit.AlphaOpSources[0] == StdMeshMaterialTextureUnit::BOS_Manual)
					++manu_count;
				if(texunit.AlphaOpSources[1] == StdMeshMaterialTextureUnit::BOS_PlayerColor || texunit.AlphaOpSources[1] == StdMeshMaterialTextureUnit::BOS_Manual)
					++manu_count;

				if(manu_count > 1)
					technique.Available = false;
			}
		}

		if(technique.Available && mat.BestTechniqueIndex == -1)
			mat.BestTechniqueIndex = i;
	}

	return mat.BestTechniqueIndex != -1;
}

bool CStdGL::PrepareRendering(SURFACE sfcToSurface)
{
	// call from gfx thread only!
	if (!pApp || !pApp->AssertMainThread()) return false;
	// device?
	if (!pCurrCtx) if (!MainCtx.Select()) return false;
	// not ready?
	if (!Active)
		//if (!RestoreDeviceObjects())
			return false;
	// target?
	if (!sfcToSurface) return false;
	// target locked?
	if (sfcToSurface->Locked) return false;
	// target is already set as render target?
	if (sfcToSurface != RenderTarget)
		{
		// target is a render-target?
		if (!sfcToSurface->IsRenderTarget()) return false;
		// set target
		RenderTarget=sfcToSurface;
		// new target has different size; needs other clipping rect
		UpdateClipper();
		}
	// done
	return true;
	}

void CStdGL::SetupTextureEnv(bool fMod2, bool landscape)
	{
	if (shaders[0])
		{
		GLuint s = landscape ? 2 : (fMod2 ? 1 : 0);
		if (Saturation < 255)
			{
			s += 3;
			}
		if (fUseClrModMap)
			{
			s += 6;
			}
		glBindProgramARB(GL_FRAGMENT_PROGRAM_ARB, shaders[s]);
		if (Saturation < 255)
			{
			GLfloat bla[4] = { Saturation / 255.0f, Saturation / 255.0f, Saturation / 255.0f, 1.0f };
			glProgramLocalParameter4fvARB(GL_FRAGMENT_PROGRAM_ARB, 0, bla);
			}
		}
	// texture environment
	else
		{
		glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_COMBINE);
		glTexEnvi(GL_TEXTURE_ENV, GL_COMBINE_RGB, fMod2 ? GL_ADD_SIGNED : GL_MODULATE);
		glTexEnvf(GL_TEXTURE_ENV, GL_RGB_SCALE, fMod2 ? 2.0f : 1.0f);
		glTexEnvi(GL_TEXTURE_ENV, GL_COMBINE_ALPHA, GL_MODULATE);
		glTexEnvi(GL_TEXTURE_ENV, GL_SOURCE0_RGB, GL_TEXTURE);
		glTexEnvi(GL_TEXTURE_ENV, GL_SOURCE1_RGB, GL_PRIMARY_COLOR);
		glTexEnvi(GL_TEXTURE_ENV, GL_SOURCE0_ALPHA, GL_TEXTURE);
		glTexEnvi(GL_TEXTURE_ENV, GL_SOURCE1_ALPHA, GL_PRIMARY_COLOR);
		glTexEnvi(GL_TEXTURE_ENV, GL_OPERAND0_RGB, GL_SRC_COLOR);
		glTexEnvi(GL_TEXTURE_ENV, GL_OPERAND1_RGB, GL_SRC_COLOR);
		glTexEnvi(GL_TEXTURE_ENV, GL_OPERAND0_ALPHA, GL_SRC_ALPHA);
		glTexEnvi(GL_TEXTURE_ENV, GL_OPERAND1_ALPHA, GL_SRC_ALPHA);
		}
	// set modes
	glShadeModel((fUseClrModMap && !shaders[0] && !DDrawCfg.NoBoxFades) ? GL_SMOOTH : GL_FLAT);
	}

void CStdGL::PerformBlt(CBltData &rBltData, CTexRef *pTex, DWORD dwModClr, bool fMod2, bool fExact)
	{
	// clipping
	if (DDrawCfg.ClipManually && rBltData.pTransform) ClipPoly(rBltData);
	// global modulation map
	int i;
	bool fAnyModNotBlack = (dwModClr != 0xff000000);
	if (!shaders[0] && fUseClrModMap && dwModClr != 0xff000000)
		{
		fAnyModNotBlack = false;
		for (i=0; i<rBltData.byNumVertices; ++i)
			{
			float x = rBltData.vtVtx[i].ftx;
			float y = rBltData.vtVtx[i].fty;
			if (rBltData.pTransform)
				{
				rBltData.pTransform->TransformPoint(x,y);
				}
			DWORD c = pClrModMap->GetModAt(int(x), int(y));
			ModulateClr(c, dwModClr);
			if (c != 0xff000000) fAnyModNotBlack = true;
			DwTo4UB(c, rBltData.vtVtx[i].color);
			}
		}
	else
		{
		for (i=0; i<rBltData.byNumVertices; ++i)
			DwTo4UB(dwModClr, rBltData.vtVtx[i].color);
		}
	// reset MOD2 for completely black modulations
	fMod2 = fMod2 && fAnyModNotBlack;
	SetupTextureEnv(fMod2, false);
	glBindTexture(GL_TEXTURE_2D, pTex->texName);
	if (!fExact && !DDrawCfg.PointFiltering)
		{
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
		}

	glMatrixMode(GL_TEXTURE);
	float matrix[16];
	matrix[0]=rBltData.TexPos.mat[0];  matrix[1]=rBltData.TexPos.mat[3];  matrix[2]=0;  matrix[3]=rBltData.TexPos.mat[6];
	matrix[4]=rBltData.TexPos.mat[1];  matrix[5]=rBltData.TexPos.mat[4];  matrix[6]=0;  matrix[7]=rBltData.TexPos.mat[7];
	matrix[8]=0;                       matrix[9]=0;                       matrix[10]=1; matrix[11]=0;
	matrix[12]=rBltData.TexPos.mat[2]; matrix[13]=rBltData.TexPos.mat[5]; matrix[14]=0; matrix[15]=rBltData.TexPos.mat[8];
	glLoadMatrixf(matrix);

	if (shaders[0] && fUseClrModMap)
		{
		glActiveTexture(GL_TEXTURE3);
		glLoadIdentity();
		CSurface * pSurface = pClrModMap->GetSurface();
		glScalef(1.0f/(pClrModMap->GetResolutionX()*(*pSurface->ppTex)->iSize), 1.0f/(pClrModMap->GetResolutionY()*(*pSurface->ppTex)->iSize), 1.0f);
		glTranslatef(float(-pClrModMap->OffX), float(-pClrModMap->OffY), 0.0f);
		}
	if (rBltData.pTransform)
		{
		float * mat = rBltData.pTransform->mat;
		matrix[0]=mat[0];  matrix[1]=mat[3];  matrix[2]=0;  matrix[3]=mat[6];
		matrix[4]=mat[1];  matrix[5]=mat[4];  matrix[6]=0;  matrix[7]=mat[7];
		matrix[8]=0;       matrix[9]=0;       matrix[10]=1; matrix[11]=0;
		matrix[12]=mat[2]; matrix[13]=mat[5]; matrix[14]=0;	matrix[15]=mat[8];
		if (shaders[0] && fUseClrModMap)
			{
			glMultMatrixf(matrix);
			}
		glActiveTexture(GL_TEXTURE0);
		glMatrixMode(GL_MODELVIEW);
		glLoadMatrixf(matrix);
		}
	else
		{
		glActiveTexture(GL_TEXTURE0);
		glMatrixMode(GL_MODELVIEW);
		glLoadIdentity();
		}

	// draw polygon
	for (i=0; i<rBltData.byNumVertices; ++i)
		{
		rBltData.vtVtx[i].tx = rBltData.vtVtx[i].ftx;
		rBltData.vtVtx[i].ty = rBltData.vtVtx[i].fty;
		//if (rBltData.pTransform) rBltData.pTransform->TransformPoint(rBltData.vtVtx[i].ftx, rBltData.vtVtx[i].fty);
		rBltData.vtVtx[i].ftz = 0;
		}
	if (vbo)
		{
		glBufferDataARB(GL_ARRAY_BUFFER_ARB, rBltData.byNumVertices*sizeof(CBltVertex), rBltData.vtVtx, GL_STREAM_DRAW_ARB);
		glInterleavedArrays(GL_T2F_C4UB_V3F, sizeof(CBltVertex), 0);
		}
	else
		{
		glInterleavedArrays(GL_T2F_C4UB_V3F, sizeof(CBltVertex), rBltData.vtVtx);
		}
	if (shaders[0] && fUseClrModMap)
		{
		glClientActiveTexture(GL_TEXTURE3);
		glEnableClientState(GL_TEXTURE_COORD_ARRAY);
		glTexCoordPointer(2, GL_FLOAT, sizeof(CBltVertex), &rBltData.vtVtx[0].ftx);
		glClientActiveTexture(GL_TEXTURE0);
		}
	glDrawArrays(GL_POLYGON, 0, rBltData.byNumVertices);
	glLoadIdentity();
	if (!fExact && !DDrawCfg.PointFiltering)
		{
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
		}
	}

namespace
{
	inline void SetTexCombine(GLenum combine, StdMeshMaterialTextureUnit::BlendOpExType blendop)
	{
		switch(blendop)
		{
		case StdMeshMaterialTextureUnit::BOX_Source1:
		case StdMeshMaterialTextureUnit::BOX_Source2:
			glTexEnvi(GL_TEXTURE_ENV, combine, GL_REPLACE);
			break;
		case StdMeshMaterialTextureUnit::BOX_Modulate:
		case StdMeshMaterialTextureUnit::BOX_ModulateX2:
		case StdMeshMaterialTextureUnit::BOX_ModulateX4:
			glTexEnvi(GL_TEXTURE_ENV, combine, GL_MODULATE);
			break;
		case StdMeshMaterialTextureUnit::BOX_Add:
			glTexEnvi(GL_TEXTURE_ENV, combine, GL_ADD);
			break;
		case StdMeshMaterialTextureUnit::BOX_AddSigned:
			glTexEnvi(GL_TEXTURE_ENV, combine, GL_ADD_SIGNED);
			break;
		case StdMeshMaterialTextureUnit::BOX_AddSmooth:
			// b+c-b*c == a*c + b*(1-c) for a==1.
			glTexEnvi(GL_TEXTURE_ENV, combine, GL_INTERPOLATE);
			break;
		case StdMeshMaterialTextureUnit::BOX_Subtract:
			glTexEnvi(GL_TEXTURE_ENV, combine, GL_SUBTRACT);
			break;
		case StdMeshMaterialTextureUnit::BOX_BlendDiffuseAlpha:
		case StdMeshMaterialTextureUnit::BOX_BlendTextureAlpha:
		case StdMeshMaterialTextureUnit::BOX_BlendCurrentAlpha:
		case StdMeshMaterialTextureUnit::BOX_BlendManual:
			glTexEnvi(GL_TEXTURE_ENV, combine, GL_INTERPOLATE);
			break;
		case StdMeshMaterialTextureUnit::BOX_Dotproduct:
			glTexEnvi(GL_TEXTURE_ENV, combine, GL_DOT3_RGB);
			break;
		case StdMeshMaterialTextureUnit::BOX_BlendDiffuseColor:
			glTexEnvi(GL_TEXTURE_ENV, combine, GL_INTERPOLATE);
			break;
		}
	}

	inline void SetTexScale(GLenum scale, StdMeshMaterialTextureUnit::BlendOpExType blendop)
	{
		switch(blendop)
		{
		case StdMeshMaterialTextureUnit::BOX_Source1:
		case StdMeshMaterialTextureUnit::BOX_Source2:
		case StdMeshMaterialTextureUnit::BOX_Modulate:
			glTexEnvf(GL_TEXTURE_ENV, scale, 1.0f);
			break;
		case StdMeshMaterialTextureUnit::BOX_ModulateX2:
			glTexEnvf(GL_TEXTURE_ENV, scale, 2.0f);
			break;
		case StdMeshMaterialTextureUnit::BOX_ModulateX4:
			glTexEnvf(GL_TEXTURE_ENV, scale, 4.0f);
			break;
		case StdMeshMaterialTextureUnit::BOX_Add:
		case StdMeshMaterialTextureUnit::BOX_AddSigned:
		case StdMeshMaterialTextureUnit::BOX_AddSmooth:
		case StdMeshMaterialTextureUnit::BOX_Subtract:
		case StdMeshMaterialTextureUnit::BOX_BlendDiffuseAlpha:
		case StdMeshMaterialTextureUnit::BOX_BlendTextureAlpha:
		case StdMeshMaterialTextureUnit::BOX_BlendCurrentAlpha:
		case StdMeshMaterialTextureUnit::BOX_BlendManual:
		case StdMeshMaterialTextureUnit::BOX_Dotproduct:
		case StdMeshMaterialTextureUnit::BOX_BlendDiffuseColor:
			glTexEnvf(GL_TEXTURE_ENV, scale, 1.0f);
			break;
		}
	}
	
	inline void SetTexSource(GLenum source, StdMeshMaterialTextureUnit::BlendOpSourceType blendsource)
	{
		switch(blendsource)
		{
		case StdMeshMaterialTextureUnit::BOS_Current:
			glTexEnvi(GL_TEXTURE_ENV, source, GL_PREVIOUS);
			break;
		case StdMeshMaterialTextureUnit::BOS_Texture:
			glTexEnvi(GL_TEXTURE_ENV, source, GL_TEXTURE);
			break;
		case StdMeshMaterialTextureUnit::BOS_Diffuse:
		case StdMeshMaterialTextureUnit::BOS_Specular:
			glTexEnvi(GL_TEXTURE_ENV, source, GL_PRIMARY_COLOR);
			break;
		case StdMeshMaterialTextureUnit::BOS_PlayerColor:
			glTexEnvi(GL_TEXTURE_ENV, source, GL_CONSTANT);
			break;
		case StdMeshMaterialTextureUnit::BOS_Manual:
			glTexEnvi(GL_TEXTURE_ENV, source, GL_CONSTANT);
			break;
		}
	}
	
	inline void SetTexSource2(GLenum source, StdMeshMaterialTextureUnit::BlendOpExType blendop)
	{
		// Set Arg2 for interpolate (Arg0 for BOX_Add_Smooth)
		switch(blendop)
		{
		case StdMeshMaterialTextureUnit::BOX_AddSmooth:
			glTexEnvi(GL_TEXTURE_ENV, source, GL_CONSTANT); // 1.0, Set in SetTexColor
			break;
		case StdMeshMaterialTextureUnit::BOX_BlendDiffuseAlpha:
			glTexEnvi(GL_TEXTURE_ENV, source, GL_PRIMARY_COLOR);
			break;
		case StdMeshMaterialTextureUnit::BOX_BlendTextureAlpha:
			glTexEnvi(GL_TEXTURE_ENV, source, GL_TEXTURE);
			break;
		case StdMeshMaterialTextureUnit::BOX_BlendCurrentAlpha:
			glTexEnvi(GL_TEXTURE_ENV, source, GL_PREVIOUS);
			break;
		case StdMeshMaterialTextureUnit::BOX_BlendManual:
			glTexEnvi(GL_TEXTURE_ENV, source, GL_CONSTANT); // Set in SetTexColor
			break;
		case StdMeshMaterialTextureUnit::BOX_BlendDiffuseColor:
			// difference to BOX_Blend_Diffuse_Alpha is operand, see SetTexOperand2
			glTexEnvi(GL_TEXTURE_ENV, source, GL_PRIMARY_COLOR);
			break;
		default:
			// TODO
			break;
		}
	}

	inline void SetTexOperand2(GLenum operand, StdMeshMaterialTextureUnit::BlendOpExType blendop)
	{
		switch(blendop)
		{
		case StdMeshMaterialTextureUnit::BOX_Add:
		case StdMeshMaterialTextureUnit::BOX_AddSigned:
		case StdMeshMaterialTextureUnit::BOX_AddSmooth:
		case StdMeshMaterialTextureUnit::BOX_Subtract:
			glTexEnvi(GL_TEXTURE_ENV, operand, GL_SRC_COLOR);
			break;
		case StdMeshMaterialTextureUnit::BOX_BlendDiffuseAlpha:
		case StdMeshMaterialTextureUnit::BOX_BlendTextureAlpha:
		case StdMeshMaterialTextureUnit::BOX_BlendCurrentAlpha:
		case StdMeshMaterialTextureUnit::BOX_BlendManual:
			glTexEnvi(GL_TEXTURE_ENV, operand, GL_SRC_ALPHA);
			break;
		case StdMeshMaterialTextureUnit::BOX_Dotproduct:
		case StdMeshMaterialTextureUnit::BOX_BlendDiffuseColor:
			glTexEnvi(GL_TEXTURE_ENV, operand, GL_SRC_COLOR);
			break;
		default:
			// TODO
			break;
		}
	}

	inline void SetTexColor(const StdMeshMaterialTextureUnit& texunit, DWORD PlayerColor)
	{
		float Color[4] = { 0.0f, 0.0f, 0.0f, 0.0f };

		if(texunit.ColorOpEx == StdMeshMaterialTextureUnit::BOX_AddSmooth)
		{
			Color[0] = Color[1] = Color[2] = 1.0f;
		}
		else if(texunit.ColorOpEx == StdMeshMaterialTextureUnit::BOX_BlendManual)
		{
			// operand of GL_CONSTANT is set to alpha for this blend mode
			// see SetTexOperand2
			Color[3] = texunit.ColorOpManualFactor;
		}
		else if(texunit.ColorOpSources[0] == StdMeshMaterialTextureUnit::BOS_PlayerColor || texunit.ColorOpSources[1] == StdMeshMaterialTextureUnit::BOS_PlayerColor)
		{
			Color[0] = ((PlayerColor >> 16) & 0xff) / 255.0f;
			Color[1] = ((PlayerColor >>  8) & 0xff) / 255.0f;
			Color[2] = ((PlayerColor      ) & 0xff) / 255.0f;
		}
		else if(texunit.ColorOpSources[0] == StdMeshMaterialTextureUnit::BOS_Manual)
		{
			Color[0] = texunit.ColorOpManualColor1[0];
			Color[1] = texunit.ColorOpManualColor1[1];
			Color[2] = texunit.ColorOpManualColor1[2];
		}
		else if(texunit.ColorOpSources[1] == StdMeshMaterialTextureUnit::BOS_Manual)
		{
			Color[0] = texunit.ColorOpManualColor2[0];
			Color[1] = texunit.ColorOpManualColor2[1];
			Color[2] = texunit.ColorOpManualColor2[2];
		}

		if(texunit.AlphaOpEx == StdMeshMaterialTextureUnit::BOX_AddSmooth)
			Color[3] = 1.0f;
		else if(texunit.AlphaOpEx == StdMeshMaterialTextureUnit::BOX_BlendManual)
			Color[3] = texunit.AlphaOpManualFactor;
		else if(texunit.AlphaOpSources[0] == StdMeshMaterialTextureUnit::BOS_PlayerColor || texunit.AlphaOpSources[1] == StdMeshMaterialTextureUnit::BOS_PlayerColor)
			Color[3] = ((PlayerColor >> 24) & 0xff) / 255.0f;
		else if(texunit.AlphaOpSources[0] == StdMeshMaterialTextureUnit::BOS_Manual)
			Color[3] = texunit.AlphaOpManualAlpha1;
		else if(texunit.AlphaOpSources[1] == StdMeshMaterialTextureUnit::BOS_Manual)
			Color[3] = texunit.AlphaOpManualAlpha2;

		glTexEnvfv(GL_TEXTURE_ENV, GL_TEXTURE_ENV_COLOR, Color);
	}

	void RenderSubMeshImpl(StdMeshInstance& instance, unsigned int submesh_index, DWORD dwModClr, DWORD dwPlayerColor)
	{
		const StdSubMesh& submesh = instance.Mesh.GetSubMesh(submesh_index);
		const StdMeshMaterial& material = submesh.GetMaterial();
		assert(material.BestTechniqueIndex != -1);
		const StdMeshMaterialTechnique& technique = material.Techniques[material.BestTechniqueIndex];

		// Render each pass
		for(unsigned int i = 0; i < technique.Passes.size(); ++i)
		{
			const StdMeshMaterialPass& pass = technique.Passes[i];

			glDepthMask(pass.DepthWrite ? GL_TRUE : GL_FALSE);

			// Apply ClrMod to material
			// TODO: ClrModMap is not taken into account by this; can maybe be done
			// by a vertex shader if required.
			float Ambient[4], Diffuse[4], Specular[4];
			// TODO: We might also want to modulate emissive

			// TODO: Pass pass.Ambient, pass.Diffuse, pass.Specular directly to the GL if dwModClr==0xffffffff
			Ambient[0] = pass.Ambient[0] * ((dwModClr >> 16) & 0xff) / 255.0f;
			Ambient[1] = pass.Ambient[1] * ((dwModClr >>  8) & 0xff) / 255.0f;
			Ambient[2] = pass.Ambient[2] * ((dwModClr      ) & 0xff) / 255.0f;
			Ambient[3] = pass.Ambient[3] * ((dwModClr >> 24) & 0xff) / 255.0f;

			Diffuse[0] = pass.Diffuse[0] * ((dwModClr >> 16) & 0xff) / 255.0f;
			Diffuse[1] = pass.Diffuse[1] * ((dwModClr >>  8) & 0xff) / 255.0f;
			Diffuse[2] = pass.Diffuse[2] * ((dwModClr      ) & 0xff) / 255.0f;
			Diffuse[3] = pass.Diffuse[3] * ((dwModClr >> 24) & 0xff) / 255.0f;

			Specular[0] = pass.Specular[0] * ((dwModClr >> 16) & 0xff) / 255.0f;
			Specular[1] = pass.Specular[1] * ((dwModClr >>  8) & 0xff) / 255.0f;
			Specular[2] = pass.Specular[2] * ((dwModClr      ) & 0xff) / 255.0f;
			Specular[3] = pass.Specular[3] * ((dwModClr >> 24) & 0xff) / 255.0f;

			glMaterialfv(GL_FRONT, GL_AMBIENT, Ambient);
			glMaterialfv(GL_FRONT, GL_DIFFUSE, Diffuse);
			glMaterialfv(GL_FRONT, GL_SPECULAR, Specular);
			glMaterialfv(GL_FRONT, GL_EMISSION, pass.Emissive);
			glMaterialf(GL_FRONT, GL_SHININESS, pass.Shininess);

			// TODO: Use vbo if available.
			// Note that we need to do this before we do glTexCoordPointer for the
			// texture units below, otherwise the texcoordpointer is reset by this
			// call (or at least by my radeon driver), even though the documentation
			// states that "The texture coordinate state for other client texture units 
			// is not updated, regardless of whether the client texture unit is enabled
			// or not."
			glInterleavedArrays(GL_N3F_V3F, sizeof(StdMeshVertex), &instance.GetVertices(submesh_index)[0].nx);

			glMatrixMode(GL_TEXTURE);
			GLuint have_texture = 0;
			for(unsigned int j = 0; j < pass.TextureUnits.size(); ++j)
			{
				// Note that it is guaranteed that the GL_TEXTUREn
				// constants are contiguous.
				// GL_TEXTURE3 is reserved for FoW/ClrModMap
				if(j < 3) { glActiveTexture(GL_TEXTURE0+j); glClientActiveTexture(GL_TEXTURE0+j); }
				else { glActiveTexture(GL_TEXTURE4+j-3); glClientActiveTexture(GL_TEXTURE4+j-3); }
			
				const StdMeshMaterialTextureUnit& texunit = pass.TextureUnits[j];

				glEnable(GL_TEXTURE_2D);
				if(texunit.HasTexture())
				{
					have_texture = texunit.GetTexture().texName;
					glBindTexture(GL_TEXTURE_2D, texunit.GetTexture().texName);
				}
				else
				{
					// We need to bind a valid texture here, even if the texture unit
					// does not access the texture. TODO: Use a dummy texture. OGRE uses
					// a "warning" texture containing black and yellow stripes.
					glBindTexture(GL_TEXTURE_2D, have_texture);
				}
				glEnableClientState(GL_TEXTURE_COORD_ARRAY);
				glTexCoordPointer(2, GL_FLOAT, sizeof(StdMeshVertex), &instance.GetVertices(submesh_index)[0].u);
				glLoadIdentity();

				glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_COMBINE);

				// Combine
				SetTexCombine(GL_COMBINE_RGB, texunit.ColorOpEx);
				SetTexCombine(GL_COMBINE_ALPHA, texunit.AlphaOpEx);

				// Scale
				SetTexScale(GL_RGB_SCALE, texunit.ColorOpEx);
				SetTexScale(GL_ALPHA_SCALE, texunit.AlphaOpEx);

				if(texunit.ColorOpEx == StdMeshMaterialTextureUnit::BOX_Source2)
				{
					SetTexSource(GL_SOURCE0_RGB, texunit.ColorOpSources[1]);
					glTexEnvi(GL_TEXTURE_ENV, GL_OPERAND0_RGB, GL_SRC_COLOR);
				}
				else
				{
					if(texunit.ColorOpEx == StdMeshMaterialTextureUnit::BOX_AddSmooth)
					{
						// GL_SOURCE0 is GL_CONSTANT to achieve the desired effect with GL_INTERPOLATE
						SetTexSource2(GL_SOURCE0_RGB, texunit.ColorOpEx);
						SetTexSource(GL_SOURCE1_RGB, texunit.ColorOpSources[0]);
						SetTexSource(GL_SOURCE2_RGB, texunit.ColorOpSources[1]);

						SetTexOperand2(GL_OPERAND0_RGB, texunit.ColorOpEx);
						glTexEnvi(GL_TEXTURE_ENV, GL_OPERAND1_RGB, GL_SRC_COLOR);
						glTexEnvi(GL_TEXTURE_ENV, GL_OPERAND2_RGB, GL_SRC_COLOR);
					}
					else
					{
						SetTexSource(GL_SOURCE0_RGB, texunit.ColorOpSources[0]);
						glTexEnvi(GL_TEXTURE_ENV, GL_OPERAND0_RGB, GL_SRC_COLOR);

						if(texunit.ColorOpEx != StdMeshMaterialTextureUnit::BOX_Source1)
						{
							SetTexSource(GL_SOURCE1_RGB, texunit.ColorOpSources[1]);
							glTexEnvi(GL_TEXTURE_ENV, GL_OPERAND1_RGB, GL_SRC_COLOR);
						}

						SetTexSource2(GL_SOURCE2_RGB, texunit.ColorOpEx);
						SetTexOperand2(GL_OPERAND2_RGB, texunit.ColorOpEx);
					}
				}

				if(texunit.AlphaOpEx == StdMeshMaterialTextureUnit::BOX_Source2)
				{
					SetTexSource(GL_SOURCE0_ALPHA, texunit.AlphaOpSources[1]);
					glTexEnvi(GL_TEXTURE_ENV, GL_OPERAND0_ALPHA, GL_SRC_ALPHA);
				}
				else
				{
					if(texunit.AlphaOpEx == StdMeshMaterialTextureUnit::BOX_AddSmooth)
					{
						// GL_SOURCE0 is GL_CONSTANT to achieve the desired effect with GL_INTERPOLATE
						SetTexSource2(GL_SOURCE0_ALPHA, texunit.AlphaOpEx);
						SetTexSource(GL_SOURCE1_ALPHA, texunit.AlphaOpSources[0]);
						SetTexSource(GL_SOURCE2_ALPHA, texunit.AlphaOpSources[1]);

						glTexEnvi(GL_TEXTURE_ENV, GL_OPERAND0_ALPHA, GL_SRC_ALPHA);
						glTexEnvi(GL_TEXTURE_ENV, GL_OPERAND1_ALPHA, GL_SRC_ALPHA);
						glTexEnvi(GL_TEXTURE_ENV, GL_OPERAND2_ALPHA, GL_SRC_ALPHA);
					}
					else
					{
						SetTexSource(GL_SOURCE0_ALPHA, texunit.AlphaOpSources[0]);
						glTexEnvi(GL_TEXTURE_ENV, GL_OPERAND0_ALPHA, GL_SRC_ALPHA);

						if(texunit.AlphaOpEx != StdMeshMaterialTextureUnit::BOX_Source1)
						{
							SetTexSource(GL_SOURCE1_ALPHA, texunit.AlphaOpSources[1]);
							glTexEnvi(GL_TEXTURE_ENV, GL_OPERAND1_ALPHA, GL_SRC_ALPHA);
						}

						SetTexSource2(GL_SOURCE2_ALPHA, texunit.AlphaOpEx);
						glTexEnvi(GL_TEXTURE_ENV, GL_OPERAND2_ALPHA, GL_SRC_ALPHA);
					}
				}

				SetTexColor(texunit, dwPlayerColor);
			}
			glMatrixMode(GL_MODELVIEW);

			glDrawElements(GL_TRIANGLES, instance.GetNumFaces(submesh_index)*3, GL_UNSIGNED_INT, instance.GetFaces(submesh_index));

			for(unsigned int j = 0; j < pass.TextureUnits.size(); ++j)
			{
				if(j < 3) { glActiveTexture(GL_TEXTURE0+j); glClientActiveTexture(GL_TEXTURE0+j); }
				else { glActiveTexture(GL_TEXTURE4+j-3); glClientActiveTexture(GL_TEXTURE4+j-3); }
				glDisable(GL_TEXTURE_2D);
			}
		}
	}

	void RenderMeshImpl(StdMeshInstance& instance, DWORD dwModClr, DWORD dwPlayerColor)
	{
		const StdMesh& mesh = instance.Mesh;

		// Render each submesh
		for(unsigned int i = 0; i < mesh.GetNumSubMeshes(); ++i)
			RenderSubMeshImpl(instance, i, dwModClr, dwPlayerColor);

#if 0
		// Draw attached bone
		if(instance.GetAttachParent())
		{
			const StdMeshInstance::AttachedMesh* attached = instance.GetAttachParent();
			const StdMeshMatrix& own_trans = instance.GetBoneTransform(attached->ChildBone) * StdMeshMatrix::Transform(instance.Mesh.GetBone(attached->ChildBone).Transformation);

			glDisable(GL_DEPTH_TEST);
			glDisable(GL_LIGHTING);
			glColor4f(1.0f, 1.0f, 0.0f, 1.0f);
			GLUquadric* quad = gluNewQuadric();
			glPushMatrix();
			glTranslatef(own_trans(0,3), own_trans(1,3), own_trans(2,3));
			gluSphere(quad, 1.0f, 4, 4);
			glPopMatrix();
			gluDeleteQuadric(quad);
			glEnable(GL_DEPTH_TEST);
			glEnable(GL_LIGHTING);
		}
#endif

		// Render attached meshes
		for(StdMeshInstance::AttachedMeshIter iter = instance.AttachedMeshesBegin(); iter != instance.AttachedMeshesEnd(); ++iter)
		{
			// Convert matrix to column-major order, add fourth row
			const float attach_trans_gl[16] = {
				iter->AttachTrans(0,0), iter->AttachTrans(1,0), iter->AttachTrans(2,0), 0,
				iter->AttachTrans(0,1), iter->AttachTrans(1,1), iter->AttachTrans(2,1), 0,
				iter->AttachTrans(0,2), iter->AttachTrans(1,2), iter->AttachTrans(2,2), 0,
				iter->AttachTrans(0,3), iter->AttachTrans(1,3), iter->AttachTrans(2,3), 1
			};

			glPushMatrix();
			glMultMatrixf(attach_trans_gl);
			RenderMeshImpl(*iter->Child, dwModClr, dwPlayerColor);
			glPopMatrix();

#if 0
			const StdMeshMatrix& own_trans = instance.GetBoneTransform(iter->ParentBone)
		                                 * StdMeshMatrix::Transform(instance.Mesh.GetBone(iter->ParentBone).Transformation);

			// Draw attached bone
			glDisable(GL_DEPTH_TEST);
			glDisable(GL_LIGHTING);
			glColor4f(1.0f, 0.0f, 0.0f, 1.0f);
			GLUquadric* quad = gluNewQuadric();
			glPushMatrix();
			glTranslatef(own_trans(0,3), own_trans(1,3), own_trans(2,3));
			gluSphere(quad, 1.0f, 4, 4);
			glPopMatrix();
			gluDeleteQuadric(quad);
			glEnable(GL_DEPTH_TEST);
			glEnable(GL_LIGHTING);
#endif
		}
	}
}

void CStdGL::PerformMesh(StdMeshInstance &instance, float tx, float ty, float twdt, float thgt, DWORD dwPlayerColor, CBltTransform* pTransform)
{
	const StdMesh& mesh = instance.Mesh;
	const StdMeshBox& box = mesh.GetBoundingBox();

	glShadeModel(GL_SMOOTH);
	glEnable(GL_DEPTH_TEST);
	glEnable(GL_LIGHTING);
	glEnable(GL_BLEND); // TODO: Shouldn't this always be enabled? - blending does not work for meshes without this though.
	glEnable(GL_CULL_FACE);

	glMatrixMode(GL_MODELVIEW);
	glPushMatrix();
	glLoadIdentity();

	// Apply zoom
	glTranslatef(ZoomX, ZoomY, 0.0f);
	glScalef(Zoom, Zoom, 1.0f);
	glTranslatef(-ZoomX, -ZoomY, 0.0f);

	bool parity = true;
	if(pTransform)
	{
		const GLfloat transform[16] = { pTransform->mat[0], pTransform->mat[3], 0, pTransform->mat[6], pTransform->mat[1], pTransform->mat[4], 0, pTransform->mat[7], 0, 0, 1, 0, pTransform->mat[2], pTransform->mat[5], 0, pTransform->mat[8] };
		glMultMatrixf(transform);
		
		// Compute parity of the transformation matrix - if parity is swapped then
		// we need to cull front faces instead of back faces.
		const float det = transform[0]*transform[5]*transform[15]
		                + transform[4]*transform[13]*transform[3]
		                + transform[12]*transform[1]*transform[7]
		                - transform[0]*transform[13]*transform[7]
		                - transform[4]*transform[1]*transform[15]
		                - transform[12]*transform[5]*transform[3];
		if(det < 0) parity = false;
	}

	if(parity)
		glCullFace(GL_BACK);
	else
		glCullFace(GL_FRONT);

	// Scale so that the mesh fits in (tx,ty,twdt,thgt)
	const float rx = -box.x1 / (box.x2 - box.x1);
	const float ry = -box.y1 / (box.y2 - box.y1);
	const float dx = tx + rx*twdt;
	const float dy = ty + ry*thgt;
	const float scx = twdt/(box.x2 - box.x1);
	const float scy = thgt/(box.y2 - box.y1);

#if 0
	// Scale so that Z coordinate is between -1 and 1, otherwise parts of
	// the mesh could be clipped away by the near or far clipping plane.
	// This technique might also enable us not to clear the depth buffer
	// after every mesh rendering - we could simply scale the first mesh
	// of the scene so that it's Z coordinate is between 0 and 1, scale
	// the second mesh that it is between 1 and 2, and so on.
	// This of course requires an orthogonal projection so that the
	// meshes don't look distorted - if we should every decide to use
	// a perspective projection we need to think of something different.

	// TODO: This is currently commented out because it gets the lighting
	// wrong (objects are too dark). Instead we changed the clipping plane
	// to be large enough. I think this is still a got idea, though,
	// when we fix the lighting. Maybe we need to adapt the normals
	// somehow...
	const float scz = 1.0/(box.z2 - box.z1);
#else
	const float scz = 1.0;
#endif
	// Keep aspect ratio:
	//if(scx < scy) scy = scx;
	//else scx = scy;
	glTranslatef(dx, dy, 0.0f);
	glScalef(scx, scy, scz);

	// Put a light source in front of the object
	const GLfloat light_position[] = { 0.0f, 0.0f, box.z2 + 15.0f*Zoom, 1.0f };
	glLightfv(GL_LIGHT0, GL_POSITION, light_position);
	glEnable(GL_LIGHT0);

	DWORD dwModClr = BlitModulated ? BlitModulateClr : 0xffffffff;

#if 0
	// Create a transformation which transfers a vertex from mesh
	// coordinates to screen coordinates. This is basically the same
	// as the current GL modelview matrix, but we need it to access the
	// ClrModMap for each vertex with the correct coordinates.
	CBltTransform Transform;
	Transform.SetMoveScale(dx, dy, scx, scy);
	if(pTransform) Transform *= *pTransform;

	Transform.MoveScale(-ZoomX, -ZoomY, 1.0f, 1.0f);
	Transform.MoveScale(0.0f, 0.0f, Zoom, Zoom);
	Transform.MoveScale(ZoomX, ZoomY, 1.0f, 1.0f);

	CClrModAddMap* ClrModMap = fUseClrModMap ? pClrModMap : NULL;
#endif

	RenderMeshImpl(instance, dwModClr, dwPlayerColor);

	glActiveTexture(GL_TEXTURE0); // switch back to default
	glClientActiveTexture(GL_TEXTURE0); // switch back to default
	glDepthMask(GL_TRUE);

	glDisable(GL_LIGHT0);
	glDisable(GL_LIGHTING);
	glDisable(GL_DEPTH_TEST);
	glDisable(GL_CULL_FACE);
	//glDisable(GL_BLEND);
	glShadeModel(GL_FLAT);
	glPopMatrix();

	// TODO: glScissor, so that we only clear the area the mesh covered.
	glClear(GL_DEPTH_BUFFER_BIT);
}

void CStdGL::BlitLandscape(SURFACE sfcSource, float fx, float fy,
                           SURFACE sfcTarget, float tx, float ty, float wdt, float hgt, const SURFACE mattextures[])
	{
	//Blit(sfcSource, fx, fy, wdt, hgt, sfcTarget, tx, ty, wdt, hgt);return;
	// safety
	if (!sfcSource || !sfcTarget || !wdt || !hgt) return;
	assert(sfcTarget->IsRenderTarget());
	assert(!(dwBlitMode & C4GFXBLIT_MOD2));
	// Apply Zoom
	float twdt = wdt;
	float thgt = hgt;
	tx = (tx - ZoomX) * Zoom + ZoomX;
	ty = (ty - ZoomY) * Zoom + ZoomY;
	twdt *= Zoom;
	thgt *= Zoom;
	// bound
	if (ClipAll) return;
	// manual clipping? (primary surface only)
	if (DDrawCfg.ClipManuallyE)
		{
		int iOver;
		// Left
		iOver=int(tx)-iClipX1;
		if (iOver<0)
			{
			wdt+=iOver;
			twdt+=iOver*Zoom;
			fx-=iOver;
			tx=float(iClipX1);
			}
		// Top
		iOver=int(ty)-iClipY1;
		if (iOver<0)
			{
			hgt+=iOver;
			thgt+=iOver*Zoom;
			fy-=iOver;
			ty=float(iClipY1);
			}
		// Right
		iOver=iClipX2+1-int(tx+twdt);
		if (iOver<0)
			{
			wdt+=iOver/Zoom;
			twdt+=iOver;
			}
		// Bottom
		iOver=iClipY2+1-int(ty+thgt);
		if (iOver<0)
			{
			hgt+=iOver/Zoom;
			thgt+=iOver;
			}
		}
	// inside screen?
	if (wdt<=0 || hgt<=0) return;
	// prepare rendering to surface
	if (!PrepareRendering(sfcTarget)) return;
	// texture present?
	if (!sfcSource->ppTex)
		{
		return;
		}
	// get involved texture offsets
	int iTexSize=sfcSource->iTexSize;
	int iTexX=Max(int(fx/iTexSize), 0);
	int iTexY=Max(int(fy/iTexSize), 0);
	int iTexX2=Min((int)(fx+wdt-1)/iTexSize +1, sfcSource->iTexX);
	int iTexY2=Min((int)(fy+hgt-1)/iTexSize +1, sfcSource->iTexY);
	// blit from all these textures
	SetTexture();
	if (mattextures)
		{
		glActiveTexture(GL_TEXTURE1);
		glEnable(GL_TEXTURE_2D);
		glActiveTexture(GL_TEXTURE0);
		}
	DWORD dwModMask = 0;
	SetupTextureEnv(false, !!mattextures);
	glMatrixMode(GL_MODELVIEW);
	glLoadIdentity();
	glMatrixMode(GL_TEXTURE);
	glLoadIdentity();
	for (int iY=iTexY; iY<iTexY2; ++iY)
		{
		for (int iX=iTexX; iX<iTexX2; ++iX)
			{
			// blit
			DWORD dwModClr = BlitModulated ? BlitModulateClr : 0xffffffff;

			glActiveTexture(GL_TEXTURE0);
			CTexRef *pTex = *(sfcSource->ppTex + iY * sfcSource->iTexX + iX);
			glBindTexture(GL_TEXTURE_2D, pTex->texName);
			if (!mattextures && Zoom != 1.0)
				{
				glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
				glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
				}
			else
				{
				glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
				glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
				}

			// get current blitting offset in texture (beforing any last-tex-size-changes)
			int iBlitX=iTexSize*iX;
			int iBlitY=iTexSize*iY;
			// size changed? recalc dependant, relevant (!) values
			if (iTexSize != pTex->iSize)
				{
				iTexSize = pTex->iSize;
				}
			// get new texture source bounds
			FLOAT_RECT fTexBlt;
			// get new dest bounds
			FLOAT_RECT tTexBlt;
			// set up blit data as rect
			fTexBlt.left  = Max<float>((float)(fx - iBlitX), 0.0f);
			tTexBlt.left  = (fTexBlt.left  + iBlitX - fx) * Zoom + tx;
			fTexBlt.top   = Max<float>((float)(fy - iBlitY), 0.0f);
			tTexBlt.top   = (fTexBlt.top   + iBlitY - fy) * Zoom + ty;
			fTexBlt.right = Min<float>((float)(fx + wdt - iBlitX), (float)iTexSize);
			tTexBlt.right = (fTexBlt.right + iBlitX - fx) * Zoom + tx;
			fTexBlt.bottom= Min<float>((float)(fy + hgt - iBlitY), (float)iTexSize);
			tTexBlt.bottom= (fTexBlt.bottom+ iBlitY - fy) * Zoom + ty;
			CBltVertex Vtx[4];
			// blit positions
			Vtx[0].ftx = tTexBlt.left;  Vtx[0].fty = tTexBlt.top;
			Vtx[1].ftx = tTexBlt.right; Vtx[1].fty = tTexBlt.top;
			Vtx[2].ftx = tTexBlt.right; Vtx[2].fty = tTexBlt.bottom;
			Vtx[3].ftx = tTexBlt.left;  Vtx[3].fty = tTexBlt.bottom;
			// blit positions
			Vtx[0].tx = fTexBlt.left;  Vtx[0].ty = fTexBlt.top;
			Vtx[1].tx = fTexBlt.right; Vtx[1].ty = fTexBlt.top;
			Vtx[2].tx = fTexBlt.right; Vtx[2].ty = fTexBlt.bottom;
			Vtx[3].tx = fTexBlt.left;  Vtx[3].ty = fTexBlt.bottom;

			// color modulation
			// global modulation map
			if (shaders[0] && fUseClrModMap)
				{
				glActiveTexture(GL_TEXTURE3);
				glLoadIdentity();
				CSurface * pSurface = pClrModMap->GetSurface();
				glScalef(1.0f/(pClrModMap->GetResolutionX()*(*pSurface->ppTex)->iSize), 1.0f/(pClrModMap->GetResolutionY()*(*pSurface->ppTex)->iSize), 1.0f);
				glTranslatef(float(-pClrModMap->OffX), float(-pClrModMap->OffY), 0.0f);

				glClientActiveTexture(GL_TEXTURE3);
				glEnableClientState(GL_TEXTURE_COORD_ARRAY);
				glTexCoordPointer(2, GL_FLOAT, sizeof(CBltVertex), &Vtx[0].ftx);
				glClientActiveTexture(GL_TEXTURE0);
				}
			if (!shaders[0] && fUseClrModMap && dwModClr)
				{
				for (int i=0; i<4; ++i)
					{
					DWORD c = pClrModMap->GetModAt(int(Vtx[i].ftx), int(Vtx[i].fty));
					ModulateClr(c, dwModClr);
					DwTo4UB(c | dwModMask, Vtx[i].color);
					}
				}
			else
				{
				for (int i=0; i<4; ++i)
					DwTo4UB(dwModClr | dwModMask, Vtx[i].color);
				}
			for (int i=0; i<4; ++i)
				{
				Vtx[i].tx /= iTexSize;
				Vtx[i].ty /= iTexSize;
				Vtx[i].ftx += DDrawCfg.fBlitOff;
				Vtx[i].fty += DDrawCfg.fBlitOff;
				Vtx[i].ftz = 0;
				}
			if(mattextures)
				{
				GLfloat shaderparam[4];
				for (int cnt=1;cnt<127;cnt++)
					{
					if(mattextures[cnt])
						{
						shaderparam[0]=static_cast<GLfloat>(cnt)/255.0f;
						glProgramLocalParameter4fvARB(GL_FRAGMENT_PROGRAM_ARB, 1, shaderparam);
						//Bind Mat Texture
						glActiveTexture(GL_TEXTURE1);
						glBindTexture(GL_TEXTURE_2D, (*(mattextures[cnt]->ppTex))->texName);
						glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
						glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
						glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
						glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
						glActiveTexture(GL_TEXTURE0);

						glInterleavedArrays(GL_T2F_C4UB_V3F, sizeof(CBltVertex), Vtx);
						glDrawArrays(GL_QUADS, 0, 4);
						}
					}
				}
			else
				{
				glInterleavedArrays(GL_T2F_C4UB_V3F, sizeof(CBltVertex), Vtx);
				glDrawArrays(GL_QUADS, 0, 4);
				}

			}
		}
	if (mattextures)
		{
		glActiveTexture(GL_TEXTURE1);
		glDisable(GL_TEXTURE_2D);
		glActiveTexture(GL_TEXTURE0);
		}
	// reset texture
	ResetTexture();
	}

CStdGLCtx *CStdGL::CreateContext(CStdWindow * pWindow, CStdApp *pApp)
	{
	DebugLog("  gl: Create Context...");
	// safety
	if (!pWindow) return NULL;
	// create it
	CStdGLCtx *pCtx = new CStdGLCtx();
	if (!pCtx->Init(pWindow, pApp))
		{
		delete pCtx; Error("  gl: Error creating secondary context!"); return NULL;
		}
	// done
	return pCtx;
	}

#ifdef _WIN32
CStdGLCtx *CStdGL::CreateContext(HWND hWindow, CStdApp *pApp)
	{
	// safety
	if (!hWindow) return NULL;
	// create it
	CStdGLCtx *pCtx = new CStdGLCtx();
	if (!pCtx->Init(NULL, pApp, hWindow))
		{
		delete pCtx; Error("  gl: Error creating secondary context!"); return NULL;
		}
	// done
	return pCtx;
	}
#endif

bool CStdGL::CreatePrimarySurfaces(bool Playermode, unsigned int iXRes, unsigned int iYRes, int iColorDepth, unsigned int iMonitor)
	{
	//remember fullscreen setting
	fFullscreen = Playermode && !DDrawCfg.Windowed;


	DebugLog("  gl: SetVideoMode...");
	// Set window size only in playermode
	if (Playermode)
		{
#ifdef _WIN32
		// HACK: Disable window border
		SetWindowLong(pApp->pWindow->hWindow, GWL_STYLE,
			GetWindowLong(pApp->pWindow->hWindow, GWL_STYLE) & ~(WS_CAPTION|WS_THICKFRAME|WS_BORDER));
		SetWindowLong(pApp->pWindow->hWindow, GWL_EXSTYLE,
			GetWindowLong(pApp->pWindow->hWindow, GWL_EXSTYLE) | WS_EX_APPWINDOW);
#endif
		// Always search for display mode, in case the user decides to activate fullscreen later
		if (!pApp->SetVideoMode(iXRes, iYRes, iColorDepth, iMonitor, fFullscreen))
			{
			Error("  gl: No Display mode found; leaving current!");
			fFullscreen = false;
			}
		if (!fFullscreen)
			{
			pApp->pWindow->SetSize(iXRes, iYRes);
			}
		}

	// store options
	byByteCnt=4; iClrDpt=iColorDepth;

	// create lpPrimary and lpBack (used in first context selection)
	lpPrimary=lpBack=new CSurface();
	lpPrimary->AttachSfc(NULL, iXRes, iYRes);
	lpPrimary->byBytesPP=byByteCnt;

	// create+select gl context
	DebugLog("  gl: Create Main Context...");
	if (!MainCtx.Init(pApp->pWindow, pApp)) return Error("  gl: Error initializing context");

	// BGRA Pixel Formats, Multitexturing, Texture Combine Environment Modes
	if (!GLEW_VERSION_1_3)
		{
		return Error("  gl: OpenGL Version 1.3 or higher required.");
		}
	MaxTexSize = 64;
	GLint s = 0;
	glGetIntegerv(GL_MAX_TEXTURE_SIZE, &s);
	if (s>0) MaxTexSize = s;

	// lines texture
	glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
	glGenTextures(1, &lines_tex);
	glBindTexture(GL_TEXTURE_2D, lines_tex);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_MIRRORED_REPEAT);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_MIRRORED_REPEAT);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
	const char * linedata = byByteCnt == 2 ? "\xff\xf0\xff\xff" : "\xff\xff\xff\x00\xff\xff\xff\xff";
	glTexImage2D(GL_TEXTURE_2D, 0, 4, 1, 2, 0, GL_BGRA, byByteCnt == 2 ? GL_UNSIGNED_SHORT_4_4_4_4_REV : GL_UNSIGNED_INT_8_8_8_8_REV, linedata);
	return RestoreDeviceObjects();
	}

void CStdGL::DrawQuadDw(SURFACE sfcTarget, float *ipVtx, DWORD dwClr1, DWORD dwClr2, DWORD dwClr3, DWORD dwClr4)
	{
	// prepare rendering to target
	if (!PrepareRendering(sfcTarget)) return;
	// apply global modulation
	ClrByCurrentBlitMod(dwClr1);
	ClrByCurrentBlitMod(dwClr2);
	ClrByCurrentBlitMod(dwClr3);
	ClrByCurrentBlitMod(dwClr4);
	// apply modulation map
	if (fUseClrModMap)
		{
		ModulateClr(dwClr1, pClrModMap->GetModAt(int(ipVtx[0]), int(ipVtx[1])));
		ModulateClr(dwClr2, pClrModMap->GetModAt(int(ipVtx[2]), int(ipVtx[3])));
		ModulateClr(dwClr3, pClrModMap->GetModAt(int(ipVtx[4]), int(ipVtx[5])));
		ModulateClr(dwClr4, pClrModMap->GetModAt(int(ipVtx[6]), int(ipVtx[7])));
		}
	// no clr fading supported
	if (DDrawCfg.NoBoxFades)
		{
		NormalizeColors(dwClr1, dwClr2, dwClr3, dwClr4);
		glShadeModel(GL_FLAT);
		}
	else
		glShadeModel((dwClr1 == dwClr2 && dwClr1 == dwClr3 && dwClr1 == dwClr4) ? GL_FLAT : GL_SMOOTH);
	// set blitting state
	int iAdditive = dwBlitMode & C4GFXBLIT_ADDITIVE;
	glBlendFunc(GL_SRC_ALPHA, iAdditive ? GL_ONE : GL_ONE_MINUS_SRC_ALPHA);
	// draw two triangles
	glInterleavedArrays(GL_V2F, sizeof(float)*2, ipVtx);
	GLubyte colors[4][4];
	DwTo4UB(dwClr1,colors[0]);
	DwTo4UB(dwClr2,colors[1]);
	DwTo4UB(dwClr3,colors[2]);
	DwTo4UB(dwClr4,colors[3]);
	glColorPointer(4,GL_UNSIGNED_BYTE,0,colors);
	glEnableClientState(GL_COLOR_ARRAY);
	glDrawArrays(GL_POLYGON, 0, 4);
	glDisableClientState(GL_COLOR_ARRAY);
	glShadeModel(GL_FLAT);
	}

#ifdef _MSC_VER
static inline long int lrintf(float f)
{
	long int i;
	__asm {
		fld f
		fistp i
	};
	return i;
}
#endif

void CStdGL::PerformLine(SURFACE sfcTarget, float x1, float y1, float x2, float y2, DWORD dwClr)
	{
	// render target?
	if (sfcTarget->IsRenderTarget())
		{
		// prepare rendering to target
		if (!PrepareRendering(sfcTarget)) return;
		SetTexture();
		SetupTextureEnv(false, false);
		float offx = y1 - y2;
		float offy = x2 - x1;
		float l = sqrtf(offx * offx + offy * offy);
		// avoid division by zero
		l += 0.000000005f;
		offx /= l; offx *= Zoom;
		offy /= l; offy *= Zoom;
		CBltVertex vtx[4];
		vtx[0].ftx = x1 + offx; vtx[0].fty = y1 + offy; vtx[0].ftz = 0;
		vtx[1].ftx = x1 - offx; vtx[1].fty = y1 - offy; vtx[1].ftz = 0;
		vtx[2].ftx = x2 - offx; vtx[2].fty = y2 - offy; vtx[2].ftz = 0;
		vtx[3].ftx = x2 + offx; vtx[3].fty = y2 + offy; vtx[3].ftz = 0;
		// global clr modulation map
		DWORD dwClr1 = dwClr;
		glMatrixMode(GL_TEXTURE);
		glLoadIdentity();
		if (fUseClrModMap)
			{
			if (shaders[0])
				{
				glActiveTexture(GL_TEXTURE3);
				glLoadIdentity();
				CSurface * pSurface = pClrModMap->GetSurface();
				glScalef(1.0f/(pClrModMap->GetResolutionX()*(*pSurface->ppTex)->iSize), 1.0f/(pClrModMap->GetResolutionY()*(*pSurface->ppTex)->iSize), 1.0f);
				glTranslatef(float(-pClrModMap->OffX), float(-pClrModMap->OffY), 0.0f);

				glClientActiveTexture(GL_TEXTURE3);
				glEnableClientState(GL_TEXTURE_COORD_ARRAY);
				glTexCoordPointer(2, GL_FLOAT, sizeof(CBltVertex), &vtx[0].ftx);
				glClientActiveTexture(GL_TEXTURE0);
				}
			else
				{
				ModulateClr(dwClr1, pClrModMap->GetModAt(lrintf(x1), lrintf(y1)));
				ModulateClr(dwClr, pClrModMap->GetModAt(lrintf(x2), lrintf(y2)));
				}
			}
		glMatrixMode(GL_MODELVIEW);
		glLoadIdentity();
		DwTo4UB(dwClr1,vtx[0].color);
		DwTo4UB(dwClr1,vtx[1].color);
		DwTo4UB(dwClr,vtx[2].color);
		DwTo4UB(dwClr,vtx[3].color);
		vtx[0].tx = 0; vtx[0].ty = 0;
		vtx[1].tx = 0; vtx[1].ty = 2;
		vtx[2].tx = 1; vtx[2].ty = 2;
		vtx[3].tx = 1; vtx[3].ty = 0;
		// draw two triangles
		glActiveTexture(GL_TEXTURE0);
		glBindTexture(GL_TEXTURE_2D, lines_tex);
		glInterleavedArrays(GL_T2F_C4UB_V3F, sizeof(CBltVertex), vtx);
		glDrawArrays(GL_POLYGON, 0, 4);
		glClientActiveTexture(GL_TEXTURE3);
		glDisableClientState(GL_TEXTURE_COORD_ARRAY);
		glClientActiveTexture(GL_TEXTURE0);
		ResetTexture();
		}
	else
		{
		// emulate
		if (!LockSurfaceGlobal(sfcTarget)) return;
		ForLine((int32_t)x1,(int32_t)y1,(int32_t)x2,(int32_t)y2,&DLineSPix,(int) dwClr);
		UnLockSurfaceGlobal(sfcTarget);
		}
	}

void CStdGL::PerformPix(SURFACE sfcTarget, float tx, float ty, DWORD dwClr)
  {
	// render target?
	if (sfcTarget->IsRenderTarget())
		{
		if (!PrepareRendering(sfcTarget)) return;
		int iAdditive = dwBlitMode & C4GFXBLIT_ADDITIVE;
		// use a different blendfunc here because of GL_POINT_SMOOTH
		glBlendFunc(GL_SRC_ALPHA, iAdditive ? GL_ONE : GL_ONE_MINUS_SRC_ALPHA);
		// convert the alpha value for that blendfunc
		glBegin(GL_POINTS);
		glColorDw(dwClr);
		glVertex2f(tx + 0.5f, ty + 0.5f);
		glEnd();
		}
	else
		{
		// emulate
		sfcTarget->SetPixDw((int)tx, (int)ty, dwClr);
		}
  }

static void DefineShaderARB(const char * p, GLuint & s)
	{
	glBindProgramARB (GL_FRAGMENT_PROGRAM_ARB, s);
	glProgramStringARB (GL_FRAGMENT_PROGRAM_ARB, GL_PROGRAM_FORMAT_ASCII_ARB, strlen(p), p);
	if (GL_INVALID_OPERATION == glGetError())
		{
		GLint errPos; glGetIntegerv (GL_PROGRAM_ERROR_POSITION_ARB, &errPos);
		fprintf (stderr, "ARB program%d:%d: Error: %s\n", s, errPos, glGetString (GL_PROGRAM_ERROR_STRING_ARB));
		s = 0;
		}
	}

bool CStdGL::RestoreDeviceObjects()
	{
	// safety
	if (!lpPrimary) return false;
	// delete any previous objects
	InvalidateDeviceObjects();
	// restore primary/back
	RenderTarget=lpPrimary;

	// set states
	Active = pCurrCtx ? (pCurrCtx->Select()) : MainCtx.Select();
	// restore gamma if active
	if (Active)
		EnableGamma();
	// reset blit states
	dwBlitMode = 0;

	// Vertex Buffer Objects crash some versions of the free radeon driver. TODO: provide an option for them
	if (0 && GLEW_ARB_vertex_buffer_object)
		{
		glGenBuffersARB(1, &vbo);
		glBindBufferARB(GL_ARRAY_BUFFER_ARB, vbo);
		glBufferDataARB(GL_ARRAY_BUFFER_ARB, 8 * sizeof(CBltVertex), 0, GL_STREAM_DRAW_ARB);
		}

	if(!DDrawCfg.Shader)
		{
		}
	else if (!shaders[0] && GLEW_ARB_fragment_program)
		{
		glGenProgramsARB (sizeof(shaders)/sizeof(*shaders), shaders);
		const char * preface =
		"!!ARBfp1.0\n"
		"TEMP tmp;\n"
		// sample the texture
		"TXP tmp, fragment.texcoord[0], texture, 2D;\n";
		const char * alpha_mod =
		// perform the modulation
		"MUL tmp.rgba, tmp, fragment.color.primary;\n";
		const char * funny_add =
		// perform the modulation
		"ADD tmp.rgb, tmp, fragment.color.primary;\n"
		"MUL tmp.a, tmp, fragment.color.primary;\n"
		"MAD_SAT tmp, tmp, { 2.0, 2.0, 2.0, 1.0 }, { -1.0, -1.0, -1.0, 0.0 };\n";
		const char * grey =
		"TEMP grey;\n"
		"DP3 grey, tmp, { 0.299, 0.587, 0.114, 1.0 };\n"
		"LRP tmp.rgb, program.local[0], tmp, grey;\n";
		const char * landscape =
		"TEMP col;\n"
		"MOV col.x, program.local[1].x;\n" //Load color to indentify
		"ADD col.y, col.x, 0.001;\n"
		"SUB col.z, col.x, 0.001;\n"  //epsilon-range
		"SGE tmp.r, tmp.b, 0.5015;\n" //Tunnel?
		"MAD tmp.r, tmp.r, -0.5019, tmp.b;\n"
		"SGE col.z, tmp.r, col.z;\n" //mat identified?
		"SLT col.y, tmp.r, col.y;\n"
		"TEMP coo;\n"
		"MOV coo, fragment.texcoord;\n"
		"MUL coo.xy, coo, 3.0;\n"
		"TXP tmp, coo, texture[1], 2D;\n"
		"MUL tmp.a, col.y, col.z;\n";
		const char * fow =
		"TEMP fow;\n"
		// sample the texture
		"TXP fow, fragment.texcoord[3], texture[3], 2D;\n"
		"LRP tmp.rgb, fow.aaaa, tmp, fow;\n";
		const char * end =
		"MOV result.color, tmp;\n"
		"END\n";
		DefineShaderARB(FormatString("%s%s%s",       preface,            alpha_mod,            end).getData(), shaders[0]);
		DefineShaderARB(FormatString("%s%s%s",       preface,            funny_add,            end).getData(), shaders[1]);
		DefineShaderARB(FormatString("%s%s%s%s",     preface, landscape, alpha_mod,            end).getData(), shaders[2]);
		DefineShaderARB(FormatString("%s%s%s%s",     preface,            alpha_mod, grey,      end).getData(), shaders[3]);
		DefineShaderARB(FormatString("%s%s%s%s",     preface,            funny_add, grey,      end).getData(), shaders[4]);
		DefineShaderARB(FormatString("%s%s%s%s%s",   preface, landscape, alpha_mod, grey,      end).getData(), shaders[5]);
		DefineShaderARB(FormatString("%s%s%s%s",     preface,            alpha_mod,       fow, end).getData(), shaders[6]);
		DefineShaderARB(FormatString("%s%s%s%s",     preface,            funny_add,       fow, end).getData(), shaders[7]);
		DefineShaderARB(FormatString("%s%s%s%s%s",   preface, landscape, alpha_mod,       fow, end).getData(), shaders[8]);
		DefineShaderARB(FormatString("%s%s%s%s%s",   preface,            alpha_mod, grey, fow, end).getData(), shaders[9]);
		DefineShaderARB(FormatString("%s%s%s%s%s",   preface,            funny_add, grey, fow, end).getData(), shaders[10]);
		DefineShaderARB(FormatString("%s%s%s%s%s%s", preface, landscape, alpha_mod, grey, fow, end).getData(), shaders[11]);
		}
	// done
	return Active;
	}

bool CStdGL::InvalidateDeviceObjects()
{
	bool fSuccess=true;
	// clear gamma
	#ifndef USE_SDL_MAINLOOP
	DisableGamma();
	#endif
	// deactivate
	Active=false;
	// invalidate font objects
	// invalidate primary surfaces
	if (lpPrimary) lpPrimary->Clear();
	if (shaders[0])
		{
		glDeleteProgramsARB(sizeof(shaders)/sizeof(*shaders), shaders);
		shaders[0] = 0;
		}
	if (vbo)
		{
		glDeleteBuffersARB(1, &vbo);
		vbo = 0;
		}
	return fSuccess;
}

void CStdGL::SetTexture()
	{
	glBlendFunc(GL_SRC_ALPHA, (dwBlitMode & C4GFXBLIT_ADDITIVE) ? GL_ONE : GL_ONE_MINUS_SRC_ALPHA);
	if (shaders[0])
		{
		glEnable(GL_FRAGMENT_PROGRAM_ARB);
		if (fUseClrModMap)
			{
			glActiveTexture(GL_TEXTURE3);
			glEnable(GL_TEXTURE_2D);
			glBindTexture(GL_TEXTURE_2D, (*pClrModMap->GetSurface()->ppTex)->texName);
			glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
			glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
			glActiveTexture(GL_TEXTURE0);
			}
		}
	glEnable(GL_TEXTURE_2D);
	}

void CStdGL::ResetTexture()
	{
	// disable texturing
	if (shaders[0])
		{
		glDisable(GL_FRAGMENT_PROGRAM_ARB);
		glActiveTexture(GL_TEXTURE3);
		glDisable(GL_TEXTURE_2D);
		glActiveTexture(GL_TEXTURE0);
		}
	glDisable(GL_TEXTURE_2D);
	}

bool CStdGL::CheckGLError(const char *szAtOp)
	{
	GLenum err = glGetError();
	if (!err) return true;
	Log(szAtOp);
	switch (err)
		{
		case GL_INVALID_ENUM:				Log("GL_INVALID_ENUM"); break;
		case GL_INVALID_VALUE:			Log("GL_INVALID_VALUE"); break;
		case GL_INVALID_OPERATION:	Log("GL_INVALID_OPERATION"); break;
		case GL_STACK_OVERFLOW:			Log("GL_STACK_OVERFLOW"); break;
		case GL_STACK_UNDERFLOW:		Log("GL_STACK_UNDERFLOW"); break;
		case GL_OUT_OF_MEMORY:			Log("GL_OUT_OF_MEMORY"); break;
		default: Log("unknown error"); break;
		}
	return false;
	}

CStdGL *pGL=NULL;

void CStdGL::TaskOut()
	{
	// deactivate
	// backup textures
	if (pTexMgr && fFullscreen) pTexMgr->IntLock();
	if (pCurrCtx) pCurrCtx->Deselect();
	}

void CStdGL::TaskIn()
	{
	// restore gl
	//if (!DeviceReady()) MainCtx.Init(pWindow, pApp);
	// restore textures
	if (pTexMgr && fFullscreen) pTexMgr->IntUnlock();
	}

bool CStdGL::OnResolutionChanged(unsigned int iXRes, unsigned int iYRes)
	{
	lpPrimary->AttachSfc(NULL, iXRes, iYRes);
	lpPrimary->byBytesPP=byByteCnt;
	// Re-create primary clipper to adapt to new size.
	CreatePrimaryClipper(iXRes, iYRes);
	return true;
	}

void CStdGL::Default()
	{
	CStdDDraw::Default();
	//pCurrCtx = NULL;
	iPixelFormat=0;
	sfcFmt=0;
	iClrDpt=0;
	MainCtx.Clear();
	}

#endif // USE_GL
